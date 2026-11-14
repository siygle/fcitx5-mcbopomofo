use std::env;
use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use std::time::Instant;

use anyhow::{anyhow, Context};

use voxkit::audio::load_wav_16k;
use voxkit::engine::{Outcome, Transcriber};
use voxkit::models::{self, Kind};
use voxkit::server;
use voxkit::vocab;
use voxkit::{default_socket, default_store};

fn main() -> ExitCode {
    match run() {
        Ok(code) => code,
        Err(err) => {
            eprintln!("voxkit: {err:#}");
            ExitCode::from(1)
        }
    }
}

fn run() -> anyhow::Result<ExitCode> {
    let mut args = env::args().skip(1).collect::<Vec<_>>();
    if args.is_empty() || args[0] == "--help" || args[0] == "-h" || args[0] == "help" {
        print_help();
        return Ok(ExitCode::SUCCESS);
    }
    let cmd = args.remove(0);
    let flags = Flags::parse(&args)?;
    match cmd.as_str() {
        "serve" => {
            let shutdown = Arc::new(AtomicBool::new(false));
            server::serve(&flags.socket, &flags.store, &flags.model, shutdown)?;
            Ok(ExitCode::SUCCESS)
        }
        "model" => model_command(&flags),
        "transcribe" => transcribe_command(&flags),
        other => Err(anyhow!("unknown command '{other}'. {HELP}")),
    }
}

fn model_command(flags: &Flags) -> anyhow::Result<ExitCode> {
    let sub = flags
        .positionals
        .first()
        .map(String::as_str)
        .unwrap_or("list");
    match sub {
        "list" => {
            print_models(&flags.store);
            Ok(ExitCode::SUCCESS)
        }
        "install" => {
            let id = flags.positionals.get(1).map(String::as_str);
            models::install_requested(&flags.store, id)?;
            Ok(ExitCode::SUCCESS)
        }
        other => Err(anyhow!("unknown model command '{other}'")),
    }
}

fn print_models(store: &std::path::Path) {
    println!("store: {}", store.display());
    for spec in models::specs() {
        let installed = models::path_for(store, spec).is_file();
        let mark = if installed {
            "installed"
        } else {
            "not installed"
        };
        let kind = match spec.kind {
            Kind::Asr => "asr",
            Kind::Vad => "vad",
        };
        println!("{:<16} {:<13} {:<12} {}", spec.id, kind, mark, spec.blurb);
        println!("  {}", spec.license);
    }
}

fn transcribe_command(flags: &Flags) -> anyhow::Result<ExitCode> {
    let path = flags
        .positionals
        .first()
        .ok_or_else(|| anyhow!("usage: voxkit transcribe FILE.wav"))?;
    let wav = load_wav_16k(&PathBuf::from(path)).with_context(|| format!("read {path}"))?;
    let terms = match &flags.vocab_dir {
        Some(dir) => vocab::load_user_dir(dir),
        None => Vec::new(),
    };
    let started = Instant::now();
    let mut engine = Transcriber::new(&flags.store);
    let outcome = engine.transcribe(
        &wav,
        voxkit::audio::TARGET_RATE,
        &flags.model,
        &flags.language,
        &terms,
        &flags.convert,
        &transcribe_cpp::CancelToken::new(),
    )?;
    match outcome {
        Outcome::Text(text) => {
            eprintln!(
                "voxkit: {} in {:.1}s, {} chars",
                flags.model,
                started.elapsed().as_secs_f32(),
                text.chars().count()
            );
            println!("{text}");
            Ok(ExitCode::SUCCESS)
        }
        Outcome::NoSpeech => {
            eprintln!("voxkit: no speech");
            Ok(ExitCode::from(2))
        }
        Outcome::Cancelled => {
            eprintln!("voxkit: cancelled");
            Ok(ExitCode::from(2))
        }
    }
}

struct Flags {
    store: PathBuf,
    socket: PathBuf,
    model: String,
    language: String,
    convert: String,
    vocab_dir: Option<PathBuf>,
    positionals: Vec<String>,
}

impl Flags {
    fn parse(args: &[String]) -> anyhow::Result<Self> {
        let mut store = default_store();
        let mut socket = default_socket();
        let mut model =
            env::var("VOXKIT_MODEL").unwrap_or_else(|_| models::default_asr_id().to_string());
        let mut language = "zh".to_string();
        let mut convert = "none".to_string();
        let mut vocab_dir = None;
        let mut positionals = Vec::new();
        let mut index = 0;
        while index < args.len() {
            let arg = &args[index];
            let value = |index: &mut usize| -> anyhow::Result<String> {
                *index += 1;
                args.get(*index)
                    .cloned()
                    .ok_or_else(|| anyhow!("{arg} needs a value"))
            };
            match arg.as_str() {
                "--store" => store = PathBuf::from(value(&mut index)?),
                "--socket" => socket = PathBuf::from(value(&mut index)?),
                "--model" => model = value(&mut index)?,
                "--language" => language = value(&mut index)?,
                "--convert" => convert = value(&mut index)?,
                "--vocab-dir" => vocab_dir = Some(PathBuf::from(value(&mut index)?)),
                "--help" | "-h" => {
                    print_help();
                    std::process::exit(0);
                }
                other if other.starts_with('-') => return Err(anyhow!("unknown flag {other}")),
                other => positionals.push(other.to_string()),
            }
            index += 1;
        }
        if convert != "none" && convert != "s2twp" {
            return Err(anyhow!("--convert must be none or s2twp"));
        }
        Ok(Flags {
            store,
            socket,
            model,
            language,
            convert,
            vocab_dir,
            positionals,
        })
    }
}

const HELP: &str = "see `voxkit help`";

fn print_help() {
    eprintln!(
        r#"voxkit — local voice daemon for the fcitx5-mcbopomofo proof of concept

Usage:
  voxkit serve [--socket PATH] [--store DIR] [--model ID]
  voxkit model list [--store DIR]
  voxkit model install [ID] [--store DIR]
  voxkit transcribe FILE.wav [--model ID] [--language zh] [--convert none|s2twp]
                             [--vocab-dir DIR] [--store DIR]

The default model is whisper-tiny. `model install` with no id installs that
model and Silero VAD. Nothing is downloaded until install is explicit.

Paths:
  models   $VOXKIT_MODELS_DIR or $XDG_DATA_HOME/voxkit/models
  socket   $VOXKIT_SOCKET or $XDG_RUNTIME_DIR/voxkit.sock
"#
    );
}
