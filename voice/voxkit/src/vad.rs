//! Silence gate. Silero itself lives in the transcribe-cpp session; this module
//! is the cheap energy check Hex runs before that model, plus helpers for
//! trimming a clip to the speech spans Silero returns.

/// Hex drops a dictation clip when the loudest 30 ms window is under this.
pub const LEVEL_GATE_DBFS: f32 = -50.0;
pub const LEVEL_WINDOW_MS: u32 = 30;

pub fn rms_dbfs(samples: &[f32]) -> f32 {
    if samples.is_empty() {
        return -100.0;
    }
    let mut sum = 0.0f32;
    for sample in samples {
        sum += sample * sample;
    }
    let rms = (sum / samples.len() as f32).sqrt();
    if rms < 1.0e-8 {
        -100.0
    } else {
        20.0 * rms.log10()
    }
}

/// Loudest non-overlapping window, in dBFS. Short tails use whatever is left.
pub fn peak_window_dbfs(samples: &[f32], sample_rate: u32, window_ms: u32) -> f32 {
    if samples.is_empty() || sample_rate == 0 {
        return -100.0;
    }
    let window = ((sample_rate as u64 * window_ms as u64) / 1000).max(1) as usize;
    let mut peak = -100.0f32;
    let mut offset = 0;
    while offset < samples.len() {
        let end = (offset + window).min(samples.len());
        peak = peak.max(rms_dbfs(&samples[offset..end]));
        offset = end;
    }
    peak
}

pub fn passes_level_gate(samples: &[f32], sample_rate: u32) -> bool {
    peak_window_dbfs(samples, sample_rate, LEVEL_WINDOW_MS) >= LEVEL_GATE_DBFS
}

/// Union of `[start, end)` sample spans, expanded by `pad` samples and clamped.
pub fn span_bounds(len: usize, spans: &[(i64, i64)], pad: usize) -> Option<(usize, usize)> {
    let mut start = i64::MAX;
    let mut end = i64::MIN;
    for &(s, e) in spans {
        if e > s {
            start = start.min(s);
            end = end.max(e);
        }
    }
    if start == i64::MAX || len == 0 {
        return None;
    }
    let start = (start - pad as i64).max(0) as usize;
    let end = ((end + pad as i64) as usize).clamp(start, len);
    if start >= end {
        None
    } else {
        Some((start.min(len), end))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sine(rate: u32, seconds: f32, amplitude: f32) -> Vec<f32> {
        let n = (rate as f32 * seconds) as usize;
        (0..n)
            .map(|i| amplitude * (i as f32 * 440.0 * std::f32::consts::TAU / rate as f32).sin())
            .collect()
    }

    #[test]
    fn silence_fails_the_level_gate() {
        let samples = vec![0.0f32; 16_000];
        assert!(!passes_level_gate(&samples, 16_000));
        assert!(peak_window_dbfs(&samples, 16_000, 30) <= -100.0);
    }

    #[test]
    fn a_spoken_level_passes_the_gate() {
        let samples = sine(16_000, 0.5, 0.2);
        assert!(passes_level_gate(&samples, 16_000));
    }

    #[test]
    fn span_bounds_pad_and_clamp() {
        assert_eq!(
            span_bounds(1000, &[(100, 200), (400, 500)], 50),
            Some((50, 550))
        );
        assert_eq!(span_bounds(1000, &[(0, 10)], 50), Some((0, 60)));
        assert_eq!(span_bounds(100, &[(90, 100)], 50), Some((40, 100)));
        assert_eq!(span_bounds(10, &[], 1), None);
    }
}
