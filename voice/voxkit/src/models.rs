//! Model catalog, checksums, and explicit downloads.
//!
//! Nothing is fetched unless `voxkit model install` is run. Checksums are
//! pinned. Silero and Qwen3-ASR match the Hex artifact list at commit
//! a7548271; Whisper tiny is the whisper.cpp `ggml-tiny.bin` file verified
//! against that URL (multilingual, small enough to run on CPU).

use std::env;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::time::Duration;

use anyhow::{anyhow, Context};
use sha2::{Digest, Sha256};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Asr,
    Vad,
}

pub struct ModelSpec {
    pub id: &'static str,
    pub kind: Kind,
    pub filename: &'static str,
    pub url: &'static str,
    pub bytes: u64,
    pub sha256: &'static str,
    /// Context-prompt budget. Zero for the VAD model.
    pub prompt_chars: usize,
    pub license: &'static str,
    pub blurb: &'static str,
}

const SPECS: &[ModelSpec] = &[
    ModelSpec {
        id: "whisper-tiny",
        kind: Kind::Asr,
        filename: "ggml-tiny.bin",
        url: "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin",
        bytes: 77_691_713,
        sha256: "be07e048e1e599ad46341c8d2a135645097a538221678b7acdd1b1919c6e1b21",
        prompt_chars: crate::vocab::WHISPER_PROMPT_CHARS,
        license: "MIT (OpenAI Whisper weights)",
        blurb: "Whisper tiny, multilingual. Default zh model for this PoC: it loads through transcribe-cpp's whisper.cpp .bin path and fits a CPU box. Chinese quality is weak; prefer qwen3-asr-0.6b when you can download ~850 MB.",
    },
    ModelSpec {
        id: "qwen3-asr-0.6b",
        kind: Kind::Asr,
        filename: "Qwen3-ASR-0.6B-Q8_0.gguf",
        url: "https://huggingface.co/handy-computer/Qwen3-ASR-0.6B-gguf/resolve/e4e16599b900eb0cb36e524514756bb92eb092b7/Qwen3-ASR-0.6B-Q8_0.gguf",
        bytes: 850_423_456,
        sha256: "f081b2d5e23bd669d92cc331d722a8a0681943b8e6f34b48996fd5c319b5acd8",
        prompt_chars: crate::vocab::QWEN_PROMPT_CHARS,
        license: "see the Qwen3-ASR model card (not redistributed here)",
        blurb: "Qwen3-ASR 0.6B Q8, the zh model Hex recommends. Checksum from anomalyco/hex a7548271. transcribe-cpp 0.4.1 knows the qwen3_asr architecture; this PoC has not been timed on CPU.",
    },
    ModelSpec {
        id: "silero-vad",
        kind: Kind::Vad,
        filename: "silero-vad-v6.2-F32.gguf",
        url: "https://huggingface.co/handy-computer/silero-vad-v6.2-gguf/resolve/a9b2d61c20ae4ddf3644edb34969e8185a485448/silero-vad-v6.2-F32.gguf",
        bytes: 1_241_056,
        sha256: "0d871a800c420fdd5d1b17a3aea4296d1c39608f0ff4a9295eb76457eb1c88e6",
        prompt_chars: 0,
        license: "MIT (Silero VAD)",
        blurb: "Silero VAD v6.2 F32 GGUF used by transcribe-cpp. Threshold 0.4 and min speech 160 ms, matching Hex.",
    },
];

pub fn specs() -> &'static [ModelSpec] {
    SPECS
}

pub fn get(id: &str) -> Option<&'static ModelSpec> {
    SPECS.iter().find(|spec| spec.id == id)
}

pub fn default_asr_id() -> &'static str {
    "whisper-tiny"
}

pub fn path_for(store: &Path, spec: &ModelSpec) -> PathBuf {
    store.join(spec.filename)
}

pub fn default_store() -> PathBuf {
    if let Some(dir) = env::var_os("VOXKIT_MODELS_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir);
        }
    }
    let base = env::var_os("XDG_DATA_HOME")
        .filter(|value| !value.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| home_dir().join(".local/share"));
    base.join("voxkit").join("models")
}

pub fn default_socket() -> PathBuf {
    if let Some(path) = env::var_os("VOXKIT_SOCKET") {
        if !path.is_empty() {
            return PathBuf::from(path);
        }
    }
    if let Some(dir) = env::var_os("XDG_RUNTIME_DIR") {
        if !dir.is_empty() {
            return PathBuf::from(dir).join("voxkit.sock");
        }
    }
    let uid = unsafe { libc::getuid() };
    PathBuf::from(format!("/tmp/voxkit-{uid}.sock"))
}

fn home_dir() -> PathBuf {
    env::var_os("HOME")
        .filter(|value| !value.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/tmp"))
}

pub fn install(store: &Path, id: &str) -> anyhow::Result<PathBuf> {
    let spec = get(id).ok_or_else(|| anyhow!("unknown model '{id}'. try `voxkit model list`"))?;
    fs::create_dir_all(store)?;
    let _lock = DownloadLock::acquire(store)?;
    let dest = path_for(store, spec);
    if dest.is_file() && file_matches(&dest, spec)? {
        eprintln!("voxkit: {id} already installed ({})", dest.display());
        return Ok(dest);
    }
    eprintln!("voxkit: downloading {id}");
    eprintln!("voxkit: {}", spec.blurb);
    eprintln!("voxkit: license: {}", spec.license);
    let partial = store.join(format!("{}.partial", spec.filename));
    download(&spec.url, &partial, spec.bytes)?;
    let digest = sha256_file(&partial)?;
    let size = fs::metadata(&partial)?.len();
    if size != spec.bytes || !digest.eq_ignore_ascii_case(spec.sha256) {
        let _ = fs::remove_file(&partial);
        anyhow::bail!(
            "checksum mismatch for {id}: got {size} bytes {digest}, expected {} {}",
            spec.bytes,
            spec.sha256
        );
    }
    fs::rename(&partial, &dest).with_context(|| format!("rename {}", dest.display()))?;
    eprintln!("voxkit: installed {id} -> {}", dest.display());
    Ok(dest)
}

/// Install an ASR model and Silero, or Silero alone.
pub fn install_requested(store: &Path, id: Option<&str>) -> anyhow::Result<()> {
    let id = id.unwrap_or_else(|| default_asr_id());
    let spec = get(id).ok_or_else(|| anyhow!("unknown model '{id}'"))?;
    install(store, spec.id)?;
    if spec.kind == Kind::Asr {
        install(store, "silero-vad")?;
    }
    Ok(())
}

pub fn require_file(store: &Path, id: &str) -> anyhow::Result<PathBuf> {
    let spec = get(id).ok_or_else(|| anyhow!("unknown model '{id}'"))?;
    let dest = path_for(store, spec);
    if !dest.is_file() {
        anyhow::bail!("model {id} is not installed. run: voxkit model install {id}");
    }
    if !file_matches(&dest, spec)? {
        anyhow::bail!("model {id} failed checksum. run: voxkit model install {id}");
    }
    Ok(dest)
}

fn file_matches(path: &Path, spec: &ModelSpec) -> anyhow::Result<bool> {
    let size = fs::metadata(path)?.len();
    if size != spec.bytes {
        return Ok(false);
    }
    let digest = sha256_file(path)?;
    Ok(digest.eq_ignore_ascii_case(spec.sha256))
}

fn sha256_file(path: &Path) -> anyhow::Result<String> {
    let mut file = File::open(path)?;
    let mut hasher = Sha256::new();
    let mut buf = vec![0u8; 1024 * 1024];
    loop {
        let n = file.read(&mut buf)?;
        if n == 0 {
            break;
        }
        hasher.update(&buf[..n]);
    }
    Ok(hex::encode(hasher.finalize()))
}

fn download(url: &str, dest: &Path, expected: u64) -> anyhow::Result<()> {
    if dest.exists() {
        let _ = fs::remove_file(dest);
    }
    let agent = ureq::AgentBuilder::new()
        .timeout(Duration::from_secs(600))
        .redirects(10)
        .build();
    let response = agent
        .get(url)
        .set("User-Agent", "voxkit/0.1")
        .call()
        .map_err(|err| anyhow!("download {url}: {err}"))?;
    let mut reader = response.into_reader();
    let mut file = File::create(dest)?;
    let mut buf = vec![0u8; 64 * 1024];
    let mut got = 0u64;
    let mut next_report = 8 * 1024 * 1024;
    loop {
        let n = reader.read(&mut buf)?;
        if n == 0 {
            break;
        }
        file.write_all(&buf[..n])?;
        got += n as u64;
        if got >= next_report || (expected > 0 && got == expected) {
            eprint!("\rvoxkit: {got} / {expected} bytes");
            let _ = std::io::stderr().flush();
            next_report = got + 8 * 1024 * 1024;
        }
    }
    eprintln!();
    file.flush()?;
    Ok(())
}

struct DownloadLock {
    file: File,
}

impl DownloadLock {
    fn acquire(dir: &Path) -> anyhow::Result<Self> {
        let file = fs::OpenOptions::new()
            .create(true)
            .read(true)
            .write(true)
            .open(dir.join(".download.lock"))?;
        let rc = unsafe { libc::flock(file.as_raw_fd(), libc::LOCK_EX) };
        if rc != 0 {
            anyhow::bail!(
                "could not lock {}: {}",
                dir.display(),
                std::io::Error::last_os_error()
            );
        }
        Ok(DownloadLock { file })
    }
}

impl Drop for DownloadLock {
    fn drop(&mut self) {
        unsafe { libc::flock(self.file.as_raw_fd(), libc::LOCK_UN) };
    }
}

use std::os::unix::io::AsRawFd;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn catalog_checksums_are_pinned_hex() {
        assert!(get(default_asr_id()).is_some());
        assert!(get("silero-vad").unwrap().kind == Kind::Vad);
        for spec in specs() {
            assert_eq!(spec.sha256.len(), 64, "{}", spec.id);
            assert!(spec.sha256.chars().all(|c| c.is_ascii_hexdigit()));
            assert!(spec.bytes > 0, "{}", spec.id);
            assert!(spec.url.starts_with("https://"));
        }
    }
}
