//! Optional Traditional Chinese conversion.
//!
//! The daemon shells out to the system `opencc` binary with the `s2twp`
//! config (Taiwan phrasing) when it is installed. No dictionary is bundled.
//! If OpenCC is absent the original text is returned and a warning is printed.

use std::env;
use std::io::Write;
use std::path::PathBuf;
use std::process::{Command, Stdio};

pub fn maybe_convert(text: &str, mode: &str) -> String {
    if mode != "s2twp" || text.is_empty() {
        return text.to_string();
    }
    match convert_s2twp(text) {
        Ok(converted) => converted,
        Err(err) => {
            eprintln!("voxkit: OpenCC s2twp skipped ({err})");
            text.to_string()
        }
    }
}

pub fn convert_s2twp(text: &str) -> Result<String, String> {
    let bin = which("opencc").ok_or_else(|| "opencc is not on PATH".to_string())?;
    let config = opencc_config()
        .ok_or_else(|| "s2twp.json was not found (Debian/Ubuntu package: opencc)".to_string())?;
    let mut child = Command::new(bin)
        .arg("-c")
        .arg(&config)
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .map_err(|err| err.to_string())?;
    {
        let mut stdin = child.stdin.take().ok_or("opencc stdin")?;
        stdin
            .write_all(text.as_bytes())
            .map_err(|err| err.to_string())?;
    }
    let output = child.wait_with_output().map_err(|err| err.to_string())?;
    if !output.status.success() {
        let err = String::from_utf8_lossy(&output.stderr);
        return Err(format!("opencc exited {}: {err}", output.status));
    }
    let mut converted = String::from_utf8_lossy(&output.stdout).into_owned();
    if converted.ends_with('\n') && !text.ends_with('\n') {
        converted.pop();
        if converted.ends_with('\r') {
            converted.pop();
        }
    }
    Ok(converted)
}

fn opencc_config() -> Option<PathBuf> {
    for candidate in [
        "/usr/share/opencc/s2twp.json",
        "/usr/local/share/opencc/s2twp.json",
    ] {
        let path = PathBuf::from(candidate);
        if path.is_file() {
            return Some(path);
        }
    }
    None
}

fn which(name: &str) -> Option<PathBuf> {
    let path = env::var_os("PATH")?;
    for dir in env::split_paths(&path) {
        let candidate = dir.join(name);
        if candidate.is_file() {
            return Some(candidate);
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn none_mode_is_identity() {
        assert_eq!(maybe_convert("软件", "none"), "软件");
    }

    #[test]
    fn s2twp_converts_when_opencc_is_installed() {
        if which("opencc").is_none() || opencc_config().is_none() {
            return;
        }
        let converted = convert_s2twp("软件").unwrap();
        assert_eq!(converted, "軟體");
    }
}
