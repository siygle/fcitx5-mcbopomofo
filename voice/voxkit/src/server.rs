//! Unix-socket server.
//!
//! The accept loop is non-blocking so a stop flag can end it. Each connection
//! has one worker thread: that thread owns the microphone (`cpal::Stream` is
//! not `Send`) and the transcribe-cpp session. The reader thread only parses
//! lines and can cancel an in-flight run.

use std::fs;
use std::io::{BufRead, BufReader, BufWriter, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

use serde_json::Value;
use transcribe_cpp::CancelToken;

use crate::audio::MicCapture;
use crate::engine::{Outcome, Transcriber};
use crate::models::{self, Kind};
use crate::protocol::{self, Command};
use crate::vad::rms_dbfs;

const TAIL: Duration = Duration::from_millis(200);

struct Writer {
    inner: Mutex<BufWriter<UnixStream>>,
}

impl Writer {
    fn send(&self, value: &Value) -> bool {
        let Ok(mut line) = serde_json::to_string(value) else {
            return false;
        };
        line.push('\n');
        let mut guard = self.inner.lock().unwrap_or_else(|err| err.into_inner());
        guard.write_all(line.as_bytes()).is_ok() && guard.flush().is_ok()
    }
}

struct Control {
    /// Bumped on start and on cancel. Stop commits only when this still equals
    /// the epoch captured as recording began.
    epoch: AtomicU64,
    token: CancelToken,
}

struct Active {
    req: u64,
    epoch: u64,
    capture: MicCapture,
    level_stop: Arc<AtomicBool>,
    level_thread: Option<JoinHandle<()>>,
}

impl Drop for Active {
    fn drop(&mut self) {
        self.level_stop.store(true, Ordering::SeqCst);
        if let Some(thread) = self.level_thread.take() {
            let _ = thread.join();
        }
    }
}

enum Job {
    Start {
        req: u64,
        language: String,
        vocabulary: Vec<String>,
        convert: String,
        model: Option<String>,
    },
    Stop {
        req: u64,
    },
    Cancel {
        req: u64,
    },
    SetVocabulary {
        vocabulary: Vec<String>,
    },
    Shutdown,
}

pub fn serve(
    socket: &Path,
    store: &Path,
    default_model: &str,
    shutdown: Arc<AtomicBool>,
) -> anyhow::Result<()> {
    if socket.exists() {
        match UnixStream::connect(socket) {
            Ok(_) => anyhow::bail!("voxkit is already listening on {}", socket.display()),
            Err(_) => fs::remove_file(socket)?,
        }
    }
    if let Some(parent) = socket.parent() {
        if !parent.as_os_str().is_empty() {
            fs::create_dir_all(parent)?;
        }
    }
    let listener = UnixListener::bind(socket)?;
    fs::set_permissions(socket, fs::Permissions::from_mode(0o600))?;
    listener.set_nonblocking(true)?;
    eprintln!("voxkit: listening on {}", socket.display());
    let _guard = SocketFile(socket.to_path_buf());
    while !shutdown.load(Ordering::SeqCst) {
        match listener.accept() {
            Ok((stream, _)) => {
                let store = store.to_path_buf();
                let default_model = default_model.to_string();
                thread::spawn(move || {
                    if let Err(err) = handle_client(stream, &store, &default_model) {
                        eprintln!("voxkit: client ended: {err:#}");
                    }
                });
            }
            Err(err) if err.kind() == std::io::ErrorKind::WouldBlock => {
                thread::sleep(Duration::from_millis(20));
            }
            Err(err) => return Err(err.into()),
        }
    }
    Ok(())
}

struct SocketFile(PathBuf);

impl Drop for SocketFile {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.0);
    }
}

fn handle_client(stream: UnixStream, store: &Path, default_model: &str) -> anyhow::Result<()> {
    stream.set_nonblocking(false)?;
    let writer = Arc::new(Writer {
        inner: Mutex::new(BufWriter::new(stream.try_clone()?)),
    });
    let control = Arc::new(Control {
        epoch: AtomicU64::new(0),
        token: CancelToken::new(),
    });
    let (tx, rx) = mpsc::channel();
    let worker = {
        let writer = Arc::clone(&writer);
        let control = Arc::clone(&control);
        let store = store.to_path_buf();
        let default_model = default_model.to_string();
        thread::spawn(move || worker_loop(rx, writer, control, store, default_model))
    };

    let mut reader = BufReader::new(stream);
    let mut line = String::new();
    loop {
        line.clear();
        if reader.read_line(&mut line)? == 0 {
            break;
        }
        let trimmed = line.trim();
        if trimmed.is_empty() {
            continue;
        }
        match protocol::parse_command(trimmed) {
            Ok(Command::Ping { req }) => {
                if !writer.send(&protocol::pong(req)) {
                    break;
                }
            }
            Ok(Command::Start {
                req,
                language,
                vocabulary,
                convert,
                model,
            }) => {
                if tx
                    .send(Job::Start {
                        req,
                        language,
                        vocabulary,
                        convert,
                        model,
                    })
                    .is_err()
                {
                    break;
                }
            }
            Ok(Command::Stop { req }) => {
                if tx.send(Job::Stop { req }).is_err() {
                    break;
                }
            }
            Ok(Command::Cancel { req }) => {
                control.epoch.fetch_add(1, Ordering::SeqCst);
                control.token.cancel();
                if tx.send(Job::Cancel { req }).is_err() {
                    break;
                }
            }
            Ok(Command::SetVocabulary { vocabulary, .. }) => {
                if tx.send(Job::SetVocabulary { vocabulary }).is_err() {
                    break;
                }
            }
            Err(message) => {
                writer.send(&protocol::error(0, "bad-request", &message));
            }
        }
    }
    let _ = tx.send(Job::Shutdown);
    let _ = worker.join();
    Ok(())
}

fn worker_loop(
    rx: Receiver<Job>,
    writer: Arc<Writer>,
    control: Arc<Control>,
    store: PathBuf,
    default_model: String,
) {
    let mut engine = Transcriber::new(store.clone());
    let mut vocabulary = Vec::new();
    let mut active: Option<Active> = None;
    let mut language = "zh".to_string();
    let mut convert = "none".to_string();
    let mut model = default_model;
    while let Ok(job) = rx.recv() {
        match job {
            Job::Shutdown => break,
            Job::SetVocabulary { vocabulary: terms } => vocabulary = terms,
            Job::Start {
                req,
                language: lang,
                vocabulary: terms,
                convert: mode,
                model: requested,
            } => {
                active = None;
                if !terms.is_empty() {
                    vocabulary = terms;
                }
                language = lang;
                convert = mode;
                if let Some(id) = requested {
                    model = id;
                }
                match begin_capture(&writer, &control, &store, req, &model) {
                    Ok(session) => active = Some(session),
                    Err((code, message)) => {
                        writer.send(&protocol::error(req, code, message));
                    }
                }
            }
            Job::Stop { req } => {
                let session = match active.take() {
                    Some(session) if session.req == req => session,
                    other => {
                        active = other;
                        writer.send(&protocol::error(
                            req,
                            "not-recording",
                            "stop without an active start",
                        ));
                        continue;
                    }
                };
                finish(
                    session,
                    &writer,
                    &control,
                    &mut engine,
                    &vocabulary,
                    &language,
                    &convert,
                    &model,
                );
            }
            Job::Cancel { req } => {
                if active.as_ref().is_some_and(|session| session.req == req) {
                    active = None;
                }
            }
        }
    }
}

fn begin_capture(
    writer: &Arc<Writer>,
    control: &Control,
    store: &Path,
    req: u64,
    model: &str,
) -> Result<Active, (&'static str, &'static str)> {
    let spec = models::get(model)
        .filter(|spec| spec.kind == Kind::Asr)
        .ok_or(("bad-model", "unknown model id"))?;
    if !models::path_for(store, spec).is_file() {
        return Err(("model-not-ready", "run: voxkit model install"));
    }
    let epoch = control.epoch.fetch_add(1, Ordering::SeqCst) + 1;
    control.token.reset();
    let capture = MicCapture::start().map_err(|err| {
        eprintln!("voxkit: microphone: {err:#}");
        ("no-input", "no microphone")
    })?;
    writer.send(&protocol::ready(req));
    writer.send(&protocol::partial(req, "", "聆聽中"));
    let level_stop = Arc::new(AtomicBool::new(false));
    let level_thread = spawn_levels(
        capture.samples_handle(),
        Arc::clone(&level_stop),
        Arc::clone(writer),
        req,
    );
    Ok(Active {
        req,
        epoch,
        capture,
        level_stop,
        level_thread: Some(level_thread),
    })
}

fn spawn_levels(
    samples: Arc<Mutex<Vec<f32>>>,
    stop: Arc<AtomicBool>,
    writer: Arc<Writer>,
    req: u64,
) -> JoinHandle<()> {
    thread::spawn(move || {
        while !stop.load(Ordering::SeqCst) {
            thread::sleep(Duration::from_millis(100));
            if stop.load(Ordering::SeqCst) {
                break;
            }
            let guard = samples.lock().unwrap_or_else(|err| err.into_inner());
            let start = guard.len().saturating_sub(1600);
            let rms = rms_dbfs(&guard[start..]);
            drop(guard);
            if !writer.send(&protocol::level(req, rms)) {
                break;
            }
        }
    })
}

fn finish(
    mut session: Active,
    writer: &Writer,
    control: &Control,
    engine: &mut Transcriber,
    vocabulary: &[String],
    language: &str,
    convert: &str,
    model: &str,
) {
    let epoch = session.epoch;
    let req = session.req;
    let steps = (TAIL.as_millis() / 20) as u32;
    for _ in 0..steps {
        if control.epoch.load(Ordering::SeqCst) != epoch {
            return;
        }
        thread::sleep(Duration::from_millis(20));
    }
    if control.epoch.load(Ordering::SeqCst) != epoch {
        return;
    }
    session.level_stop.store(true, Ordering::SeqCst);
    if let Some(thread) = session.level_thread.take() {
        let _ = thread.join();
    }
    let (samples, rate) = session.capture.finish();
    drop(session);
    if control.epoch.load(Ordering::SeqCst) != epoch {
        return;
    }
    writer.send(&protocol::partial(req, "", "辨識中"));
    let outcome = engine.transcribe(
        &samples,
        rate,
        model,
        language,
        vocabulary,
        convert,
        &control.token,
    );
    if control.epoch.load(Ordering::SeqCst) != epoch || control.token.is_cancelled() {
        return;
    }
    match outcome {
        Ok(Outcome::Text(text)) => {
            writer.send(&protocol::final_text(req, &text));
        }
        Ok(Outcome::NoSpeech) => {
            writer.send(&protocol::discarded(req, "no_speech"));
        }
        Ok(Outcome::Cancelled) => {}
        Err(err) => {
            eprintln!("voxkit: {err:#}");
            let message = err.to_string();
            let code = if message.contains("not installed") {
                "model-not-ready"
            } else {
                "transcribe-failed"
            };
            writer.send(&protocol::error(req, code, &message));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ping_roundtrip() {
        let stamp = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let socket = std::env::temp_dir().join(format!("voxkit-ping-{stamp}.sock"));
        let store = std::env::temp_dir().join(format!("voxkit-store-{stamp}"));
        let shutdown = Arc::new(AtomicBool::new(false));
        let shutdown_flag = Arc::clone(&shutdown);
        let socket_for_server = socket.clone();
        let server =
            thread::spawn(move || serve(&socket_for_server, &store, "whisper-tiny", shutdown_flag));
        let mut connected = None;
        for _ in 0..100 {
            if let Ok(stream) = UnixStream::connect(&socket) {
                connected = Some(stream);
                break;
            }
            thread::sleep(Duration::from_millis(20));
        }
        let mut stream = connected.expect("server socket");
        stream
            .write_all(b"{\"v\":1,\"type\":\"ping\",\"req\":4}\n")
            .unwrap();
        stream.flush().unwrap();
        let mut reader = BufReader::new(stream);
        let mut line = String::new();
        reader.read_line(&mut line).unwrap();
        drop(reader);
        shutdown.store(true, Ordering::SeqCst);
        let value: Value = serde_json::from_str(line.trim()).unwrap();
        assert_eq!(value["type"], "pong");
        assert_eq!(value["req"], 4);
        server.join().unwrap().unwrap();
    }
}
