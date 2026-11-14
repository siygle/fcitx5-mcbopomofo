//! Local voice daemon for the fcitx5-mcbopomofo proof of concept.
//!
//! The input method talks to this process over a Unix socket. Model weights
//! are downloaded on purpose (`voxkit model install`) and are not embedded.

pub mod audio;
pub mod convert;
pub mod engine;
pub mod models;
pub mod protocol;
pub mod server;
pub mod vad;
pub mod vocab;

pub use models::{default_socket, default_store};
