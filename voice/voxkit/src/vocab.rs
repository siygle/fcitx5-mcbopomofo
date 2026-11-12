//! Vocabulary prompt built from McBopomofo user files.
//!
//! Hex feeds Whisper and Qwen3-ASR a context prompt (terms joined by `", "`),
//! not a post-hoc string replace. Limits follow that design: 64 characters per
//! term, 500 characters for Whisper, 1000 for Qwen3-ASR. Bopomofo readings are
//! not `sounds_like` hints and are not included.

use std::collections::HashSet;
use std::fs;
use std::path::Path;

pub const MAX_TERM_CHARS: usize = 64;
pub const MAX_TERMS: usize = 200;
pub const WHISPER_PROMPT_CHARS: usize = 500;
pub const QWEN_PROMPT_CHARS: usize = 1000;

pub fn prompt_for(terms: &[String], max_chars: usize) -> String {
    let mut out = String::new();
    let mut seen = HashSet::new();
    for term in terms {
        let clipped = clip_term(term);
        if clipped.is_empty() || !seen.insert(clipped.to_lowercase()) {
            continue;
        }
        let extra = clipped.chars().count() + if out.is_empty() { 0 } else { 2 };
        if out.chars().count() + extra > max_chars {
            break;
        }
        if !out.is_empty() {
            out.push_str(", ");
        }
        out.push_str(&clipped);
    }
    out
}

pub fn clip_term(term: &str) -> String {
    term.trim().chars().take(MAX_TERM_CHARS).collect()
}

/// `data.txt` word column, then `plus-quick-phrases.txt`, then `plus-english.txt`.
/// Comment lines (`#`) are skipped. The built-in 70k-word English lexicon is
/// not read; only the user directory is.
pub fn load_user_dir(dir: &Path) -> Vec<String> {
    let mut terms = Vec::new();
    let mut seen = HashSet::new();
    if let Some(text) = read_lossy(&dir.join("data.txt")) {
        for line in text.lines() {
            if terms.len() >= MAX_TERMS {
                break;
            }
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            if let Some(word) = line.split_whitespace().next() {
                push_term(&mut terms, &mut seen, word);
            }
        }
    }
    for name in ["plus-quick-phrases.txt", "plus-english.txt"] {
        let Some(text) = read_lossy(&dir.join(name)) else {
            continue;
        };
        for line in text.lines() {
            if terms.len() >= MAX_TERMS {
                break;
            }
            let line = line.trim();
            if line.is_empty() || line.starts_with('#') {
                continue;
            }
            push_term(&mut terms, &mut seen, line);
        }
    }
    terms
}

fn push_term(terms: &mut Vec<String>, seen: &mut HashSet<String>, raw: &str) {
    if terms.len() >= MAX_TERMS {
        return;
    }
    let clipped = clip_term(raw);
    if clipped.is_empty() || !seen.insert(clipped.to_lowercase()) {
        return;
    }
    terms.push(clipped);
}

fn read_lossy(path: &Path) -> Option<String> {
    fs::read(path)
        .ok()
        .map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    #[test]
    fn prompt_stops_at_the_character_budget() {
        let terms = vec!["小麥注音".into(), "OpenCode".into(), "x".repeat(40)];
        let prompt = prompt_for(&terms, 20);
        assert_eq!(prompt, "小麥注音, OpenCode");
        assert!(prompt.chars().count() <= 20);
    }

    #[test]
    fn prompt_dedups_case_insensitively_and_clips() {
        let long = format!("{}TAIL", "詞".repeat(70));
        let terms = vec!["OpenCode".into(), "opencode".into(), long];
        let prompt = prompt_for(&terms, 10_000);
        assert!(prompt.starts_with("OpenCode, "));
        assert_eq!(
            prompt.matches("OpenCode").count() + prompt.matches("opencode").count(),
            1
        );
        let second = prompt.split(", ").nth(1).unwrap();
        assert_eq!(second.chars().count(), MAX_TERM_CHARS);
        assert!(!second.contains("TAIL"));
    }

    #[test]
    fn user_dir_reads_the_word_column_and_skips_comments() {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let dir = std::env::temp_dir().join(format!("voxkit-vocab-{stamp}"));
        fs::create_dir_all(&dir).unwrap();
        fs::write(
            dir.join("data.txt"),
            "# comment\n小麥注音 ㄒㄧㄠˇ-ㄇㄞˋ\n\nOpenCode ㄛ\n",
        )
        .unwrap();
        fs::write(dir.join("plus-quick-phrases.txt"), "早安\n# no\n").unwrap();
        fs::write(dir.join("plus-english.txt"), "fcitx\nOpenCode\n").unwrap();
        let terms = load_user_dir(&dir);
        let _ = fs::remove_dir_all(&dir);
        assert_eq!(terms, vec!["小麥注音", "OpenCode", "早安", "fcitx"]);
    }
}
