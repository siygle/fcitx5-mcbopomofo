//! Offline transcription: Silero (when installed) then one transcribe-cpp run.
//!
//! Streaming partials are intentionally not implemented. The authoritative
//! result is the offline transcript after push-to-talk ends, which is also
//! what Hex falls back to on CPU.

use std::path::Path;
use std::sync::OnceLock;

use anyhow::{anyhow, Context};
use transcribe_cpp::{
    init_backends_with, Backend, BackendMask, CancelToken, Feature, Model, ModelOptions,
    RunExtension, RunOptions, Session, SessionOptions, TimestampKind, VadOptions, VadSession,
    WhisperRunOptions,
};

use crate::audio::{prepare_16k, TARGET_RATE};
use crate::convert::maybe_convert;
use crate::models::{self, Kind};
use crate::vad::{self, passes_level_gate};
use crate::vocab::prompt_for;

pub enum Outcome {
    Text(String),
    NoSpeech,
    Cancelled,
}

struct Asr {
    session: Session,
    id: String,
    prompt_chars: usize,
    timestamps: TimestampKind,
    context_prompt: bool,
    whisper: bool,
    vocabulary: bool,
}

struct Vad {
    session: VadSession,
}

pub struct Transcriber {
    store: std::path::PathBuf,
    asr: Option<Asr>,
    vad: Option<Vad>,
    warned_energy_only: bool,
}

impl Transcriber {
    pub fn new(store: impl Into<std::path::PathBuf>) -> Self {
        Transcriber {
            store: store.into(),
            asr: None,
            vad: None,
            warned_energy_only: false,
        }
    }

    pub fn transcribe(
        &mut self,
        pcm_any_rate: &[f32],
        sample_rate: u32,
        model_id: &str,
        language: &str,
        terms: &[String],
        convert: &str,
        cancel: &CancelToken,
    ) -> anyhow::Result<Outcome> {
        let pcm = prepare_16k(pcm_any_rate, sample_rate);
        if pcm.is_empty() || !passes_level_gate(&pcm, TARGET_RATE) {
            return Ok(Outcome::NoSpeech);
        }
        if cancel.is_cancelled() {
            return Ok(Outcome::Cancelled);
        }
        self.ensure(model_id)?;
        let speech = self.speech_slice(&pcm)?;
        let Some(speech) = speech else {
            return Ok(Outcome::NoSpeech);
        };
        if cancel.is_cancelled() {
            return Ok(Outcome::Cancelled);
        }
        let asr = self.asr.as_mut().context("ASR model was not loaded")?;
        asr.session.set_cancel_token(cancel);
        let prompt = prompt_for(terms, asr.prompt_chars);
        let text = match run(asr, &speech, language, &prompt) {
            Err(transcribe_cpp::Error::Aborted { .. }) => return Ok(Outcome::Cancelled),
            Err(transcribe_cpp::Error::InvalidArgument(message)) if !prompt.is_empty() => {
                eprintln!("voxkit: prompt rejected ({message}); retrying without it");
                match run(asr, &speech, language, "") {
                    Ok(text) => text,
                    Err(transcribe_cpp::Error::Aborted { .. }) => return Ok(Outcome::Cancelled),
                    Err(err) => return Err(anyhow!(err.to_string())),
                }
            }
            Err(err) => return Err(anyhow!(err.to_string())),
            Ok(text) => text,
        };
        let text = text.trim().to_string();
        if text.is_empty() {
            return Ok(Outcome::NoSpeech);
        }
        Ok(Outcome::Text(maybe_convert(&text, convert)))
    }

    fn ensure(&mut self, model_id: &str) -> anyhow::Result<()> {
        ensure_runtime()?;
        if self.asr.as_ref().map(|asr| asr.id.as_str()) != Some(model_id) {
            let spec = models::get(model_id)
                .filter(|spec| spec.kind == Kind::Asr)
                .ok_or_else(|| anyhow!("unknown ASR model '{model_id}'"))?;
            let path = models::require_file(&self.store, spec.id)?;
            eprintln!("voxkit: loading {model_id}");
            let loaded = load_asr(spec.id, spec.prompt_chars, &path)?;
            self.asr = Some(loaded);
        }
        if self.vad.is_none() {
            match models::get("silero-vad") {
                Some(spec) if models::path_for(&self.store, spec).is_file() => {
                    let path = models::require_file(&self.store, "silero-vad")?;
                    match load_vad(&path) {
                        Ok(vad) => self.vad = Some(vad),
                        Err(err) => {
                            eprintln!(
                                "voxkit: Silero VAD failed to load ({err}); using the energy gate"
                            );
                            self.warned_energy_only = true;
                        }
                    }
                }
                _ if !self.warned_energy_only => {
                    eprintln!("voxkit: Silero VAD is not installed; using the -50 dBFS gate only");
                    self.warned_energy_only = true;
                }
                _ => {}
            }
        }
        Ok(())
    }

    fn speech_slice(&mut self, pcm: &[f32]) -> anyhow::Result<Option<Vec<f32>>> {
        let Some(vad) = self.vad.as_mut() else {
            return Ok(Some(pcm.to_vec()));
        };
        let mut options = VadOptions::default();
        options.threshold = 0.4;
        options.min_speech_ms = 160;
        let result = vad
            .session
            .run(pcm, &options)
            .map_err(|err| anyhow!(err.to_string()))?;
        let spans: Vec<(i64, i64)> = result
            .segments
            .iter()
            .map(|segment| (segment.start_sample, segment.end_sample))
            .collect();
        let Some((start, end)) = vad::span_bounds(pcm.len(), &spans, TARGET_RATE as usize / 10)
        else {
            return Ok(None);
        };
        Ok(Some(pcm[start..end].to_vec()))
    }
}

fn load_asr(id: &str, prompt_chars: usize, path: &Path) -> anyhow::Result<Asr> {
    let options = ModelOptions {
        backend: Backend::Cpu,
        ..ModelOptions::default()
    };
    let threads = std::thread::available_parallelism()
        .map(|n| n.get() as i32)
        .unwrap_or(2)
        .clamp(1, 4);
    let model = Model::load_with(path, &options).map_err(|err| anyhow!(err.to_string()))?;
    let session = model
        .session_with(&SessionOptions {
            n_threads: threads,
            ..SessionOptions::default()
        })
        .map_err(|err| anyhow!(err.to_string()))?;
    let arch = model.arch().to_ascii_lowercase();
    let timestamps = model
        .capabilities()
        .map(|caps| caps.max_timestamp_kind)
        .unwrap_or(TimestampKind::None);
    let context_prompt = model.supports(Feature::ContextPrompt);
    let vocabulary = model.supports(Feature::Vocabulary);
    // `session` keeps the native model alive after `model` is dropped.
    Ok(Asr {
        session,
        context_prompt,
        vocabulary,
        whisper: arch.contains("whisper"),
        id: id.to_string(),
        prompt_chars,
        timestamps,
    })
}

fn load_vad(path: &Path) -> anyhow::Result<Vad> {
    let options = ModelOptions {
        backend: Backend::Cpu,
        ..ModelOptions::default()
    };
    let model = Model::load_with(path, &options).map_err(|err| anyhow!(err.to_string()))?;
    let session = model
        .vad_session()
        .map_err(|err| anyhow!(err.to_string()))?;
    Ok(Vad { session })
}

fn run(
    asr: &mut Asr,
    pcm: &[f32],
    language: &str,
    prompt: &str,
) -> Result<String, transcribe_cpp::Error> {
    let mut options = RunOptions {
        language: if language.is_empty() {
            None
        } else {
            Some(language.to_string())
        },
        timestamps: asr.timestamps,
        ..RunOptions::default()
    };
    apply_prompt(&mut options, asr, prompt);
    match asr.session.run(pcm, &options) {
        Ok(transcript) => Ok(transcript.text),
        Err(transcribe_cpp::Error::Unsupported(message))
            if options.language.is_some() && message.to_ascii_lowercase().contains("language") =>
        {
            options.language = None;
            asr.session
                .run(pcm, &options)
                .map(|transcript| transcript.text)
        }
        Err(transcribe_cpp::Error::OutputTruncated { partial, .. })
        | Err(transcribe_cpp::Error::OutputRepetition { partial, .. }) => Ok(partial
            .map(|transcript| transcript.text)
            .unwrap_or_default()),
        Err(err) => Err(err),
    }
}

fn apply_prompt(options: &mut RunOptions, asr: &Asr, prompt: &str) {
    if prompt.is_empty() {
        return;
    }
    if asr.context_prompt {
        options.prompt = Some(prompt.to_string());
    } else if asr.whisper {
        options.family = Some(RunExtension::Whisper(WhisperRunOptions {
            initial_prompt: Some(prompt.to_string()),
            ..WhisperRunOptions::default()
        }));
    } else if asr.vocabulary {
        options.vocabulary = prompt.split(", ").map(str::to_string).collect();
    }
}

fn ensure_runtime() -> anyhow::Result<()> {
    static READY: OnceLock<Result<(), String>> = OnceLock::new();
    READY
        .get_or_init(|| {
            transcribe_cpp::disable_logging();
            init_backends_with(None::<&Path>, BackendMask::CPU).map_err(|err| err.to_string())
        })
        .clone()
        .map_err(|err| anyhow!(err))
}
