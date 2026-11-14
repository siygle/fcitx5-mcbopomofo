//! Line-delimited JSON between the input method and the daemon.
//!
//! One UTF-8 object per line. `v` is 1. Unknown fields are ignored.

use serde::Deserialize;
use serde_json::{json, Value};

#[derive(Debug, Clone, PartialEq)]
pub enum Command {
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
        req: u64,
        vocabulary: Vec<String>,
    },
    Ping {
        req: u64,
    },
}

#[derive(Debug, Deserialize)]
struct Incoming {
    #[serde(default)]
    v: u32,
    #[serde(rename = "type")]
    kind: String,
    #[serde(default)]
    req: u64,
    #[serde(default)]
    language: Option<String>,
    #[serde(default)]
    vocabulary: Vec<String>,
    #[serde(default)]
    convert: Option<String>,
    #[serde(default)]
    model: Option<String>,
}

pub fn parse_command(line: &str) -> Result<Command, String> {
    let incoming: Incoming = serde_json::from_str(line).map_err(|err| err.to_string())?;
    if incoming.v > 1 {
        return Err(format!("unsupported protocol version {}", incoming.v));
    }
    let req = incoming.req;
    match incoming.kind.as_str() {
        "start" => Ok(Command::Start {
            req,
            language: incoming
                .language
                .filter(|lang| !lang.is_empty())
                .unwrap_or_else(|| "zh".into()),
            vocabulary: incoming.vocabulary,
            convert: incoming.convert.unwrap_or_else(|| "none".into()),
            model: incoming.model.filter(|model| !model.is_empty()),
        }),
        "stop" => Ok(Command::Stop { req }),
        "cancel" => Ok(Command::Cancel { req }),
        "set_vocabulary" => Ok(Command::SetVocabulary {
            req,
            vocabulary: incoming.vocabulary,
        }),
        "ping" => Ok(Command::Ping { req }),
        other => Err(format!("unknown message type '{other}'")),
    }
}

pub fn ready(req: u64) -> Value {
    json!({"v": 1, "type": "ready", "req": req})
}

pub fn partial(req: u64, stable: &str, tentative: &str) -> Value {
    json!({"v": 1, "type": "partial", "req": req, "stable": stable, "tentative": tentative})
}

pub fn final_text(req: u64, text: &str) -> Value {
    json!({"v": 1, "type": "final", "req": req, "text": text})
}

pub fn discarded(req: u64, reason: &str) -> Value {
    json!({"v": 1, "type": "discarded", "req": req, "reason": reason})
}

pub fn error(req: u64, code: &str, message: &str) -> Value {
    json!({"v": 1, "type": "error", "req": req, "code": code, "message": message})
}

pub fn level(req: u64, rms_dbfs: f32) -> Value {
    json!({"v": 1, "type": "level", "req": req, "rms_dbfs": rms_dbfs})
}

pub fn pong(req: u64) -> Value {
    json!({"v": 1, "type": "pong", "req": req})
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn start_fills_defaults_and_keeps_vocabulary() {
        let command = parse_command(
            r#"{"v":1,"type":"start","req":7,"vocabulary":["小麥注音","OpenCode"],"convert":"s2twp"}"#,
        )
        .unwrap();
        match command {
            Command::Start {
                req,
                language,
                vocabulary,
                convert,
                model,
            } => {
                assert_eq!(req, 7);
                assert_eq!(language, "zh");
                assert_eq!(vocabulary, vec!["小麥注音", "OpenCode"]);
                assert_eq!(convert, "s2twp");
                assert!(model.is_none());
            }
            other => panic!("unexpected {other:?}"),
        }
    }

    #[test]
    fn events_are_one_object() {
        let line = serde_json::to_string(&final_text(3, "你好")).unwrap();
        let value: serde_json::Value = serde_json::from_str(&line).unwrap();
        assert_eq!(value["type"], "final");
        assert_eq!(value["text"], "你好");
        assert_eq!(value["req"], 3);
    }

    #[test]
    fn garbage_is_rejected() {
        assert!(parse_command("not json").is_err());
        assert!(parse_command(r#"{"v":9,"type":"ping"}"#).is_err());
        assert!(parse_command(r#"{"v":1,"type":"nope"}"#).is_err());
    }
}
