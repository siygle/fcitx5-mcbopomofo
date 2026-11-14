//! Microphone capture and WAV loading.
//!
//! cpal's callback only downmixes to mono and appends. Resampling to 16 kHz is
//! a linear interpolator, which is good enough for this proof of concept and
//! keeps the dependency list small (Hex uses rubato). The capture stream is
//! `!Send`; keep it on the thread that created it.

use std::sync::{Mutex, MutexGuard};

use anyhow::{anyhow, Context};
use cpal::traits::{DeviceTrait, HostTrait, StreamTrait};
use cpal::{SampleFormat, Stream, StreamConfig};

pub const TARGET_RATE: u32 = 16_000;
pub const MAX_CAPTURE_SECS: u32 = 60;

pub struct MicCapture {
    stream: Option<Stream>,
    samples: std::sync::Arc<Mutex<Vec<f32>>>,
    rate: u32,
}

impl MicCapture {
    pub fn start() -> anyhow::Result<Self> {
        let host = cpal::default_host();
        let device = host
            .default_input_device()
            .ok_or_else(|| anyhow!("no default input device"))?;
        let supported = device
            .default_input_config()
            .context("default input config")?;
        let rate = supported.sample_rate().0;
        let channels = supported.channels() as usize;
        if channels == 0 || rate == 0 {
            anyhow::bail!("input device reported an empty format");
        }
        let format = supported.sample_format();
        let config: StreamConfig = supported.into();
        let samples = std::sync::Arc::new(Mutex::new(Vec::with_capacity(rate as usize * 2)));
        let cap = rate as usize * MAX_CAPTURE_SECS as usize;
        let samples_cb = std::sync::Arc::clone(&samples);
        let stream = match format {
            SampleFormat::F32 => build::<f32>(&device, &config, channels, cap, samples_cb, |s| s)?,
            SampleFormat::I16 => build::<i16>(&device, &config, channels, cap, samples_cb, |s| {
                s as f32 / 32768.0
            })?,
            SampleFormat::U16 => build::<u16>(&device, &config, channels, cap, samples_cb, |s| {
                (s as f32 - 32768.0) / 32768.0
            })?,
            other => anyhow::bail!("unsupported capture format {other:?}"),
        };
        stream.play().context("start microphone")?;
        Ok(MicCapture {
            stream: Some(stream),
            samples,
            rate,
        })
    }

    pub fn rate(&self) -> u32 {
        self.rate
    }

    pub fn samples_handle(&self) -> std::sync::Arc<Mutex<Vec<f32>>> {
        std::sync::Arc::clone(&self.samples)
    }

    /// Stop the device callback and take the mono samples at the device rate.
    pub fn finish(&mut self) -> (Vec<f32>, u32) {
        self.stream.take();
        let samples = std::mem::take(&mut *lock(&self.samples));
        (samples, self.rate)
    }
}

fn build<T>(
    device: &cpal::Device,
    config: &StreamConfig,
    channels: usize,
    cap: usize,
    samples: std::sync::Arc<Mutex<Vec<f32>>>,
    to_f32: fn(T) -> f32,
) -> anyhow::Result<Stream>
where
    T: cpal::SizedSample + 'static,
{
    let stream = device
        .build_input_stream(
            config,
            move |data: &[T], _| {
                let mut buf = lock(&samples);
                if buf.len() >= cap {
                    return;
                }
                let frames = data.len() / channels;
                let room = cap - buf.len();
                let frames = frames.min(room);
                for frame in 0..frames {
                    let mut sum = 0.0f32;
                    for channel in 0..channels {
                        sum += to_f32(data[frame * channels + channel]);
                    }
                    buf.push(sum / channels as f32);
                }
            },
            |err| eprintln!("voxkit: microphone error: {err}"),
            None,
        )
        .context("open microphone stream")?;
    Ok(stream)
}

pub fn load_wav_16k(path: &std::path::Path) -> anyhow::Result<Vec<f32>> {
    let mut reader =
        hound::WavReader::open(path).with_context(|| format!("open wav {}", path.display()))?;
    let spec = reader.spec();
    if spec.channels == 0 || spec.sample_rate == 0 {
        anyhow::bail!("wav has an empty format");
    }
    let interleaved: Vec<f32> = match spec.sample_format {
        hound::SampleFormat::Float => reader.samples::<f32>().collect::<Result<Vec<_>, _>>()?,
        hound::SampleFormat::Int => {
            let scale = (1i64 << (spec.bits_per_sample.saturating_sub(1))) as f32;
            reader
                .samples::<i32>()
                .map(|sample| sample.map(|value| value as f32 / scale))
                .collect::<Result<Vec<_>, _>>()?
        }
    };
    let mono = downmix(&interleaved, spec.channels as usize);
    Ok(resample_linear(&mono, spec.sample_rate, TARGET_RATE))
}

pub fn prepare_16k(samples: &[f32], sample_rate: u32) -> Vec<f32> {
    let mut pcm = resample_linear(samples, sample_rate, TARGET_RATE);
    for sample in &mut pcm {
        *sample = sample.clamp(-1.0, 1.0);
    }
    pcm
}

pub fn downmix(interleaved: &[f32], channels: usize) -> Vec<f32> {
    if channels <= 1 {
        return interleaved.to_vec();
    }
    let frames = interleaved.len() / channels;
    let mut mono = Vec::with_capacity(frames);
    for frame in 0..frames {
        let mut sum = 0.0f32;
        for channel in 0..channels {
            sum += interleaved[frame * channels + channel];
        }
        mono.push(sum / channels as f32);
    }
    mono
}

pub fn resample_linear(input: &[f32], from: u32, to: u32) -> Vec<f32> {
    if input.is_empty() || from == 0 || to == 0 || from == to {
        return input.to_vec();
    }
    let out_len = ((input.len() as u64) * (to as u64) / (from as u64)) as usize;
    if out_len == 0 {
        return Vec::new();
    }
    let mut out = Vec::with_capacity(out_len);
    let last = input.len() - 1;
    for index in 0..out_len {
        let position = index as f64 * from as f64 / to as f64;
        let left = (position.floor() as usize).min(last);
        let right = (left + 1).min(last);
        let frac = (position - left as f64) as f32;
        let sample = input[left] + (input[right] - input[left]) * frac;
        out.push(sample);
    }
    out
}

fn lock<T>(mutex: &Mutex<T>) -> MutexGuard<'_, T> {
    mutex.lock().unwrap_or_else(|err| err.into_inner())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn downmix_averages_channels() {
        assert_eq!(downmix(&[1.0, 3.0, 5.0, 7.0], 2), vec![2.0, 6.0]);
    }

    #[test]
    fn resample_keeps_duration() {
        let input = vec![0.0f32; 4800];
        let output = resample_linear(&input, 48_000, 16_000);
        assert_eq!(output.len(), 1600);
        assert_eq!(resample_linear(&input, 16_000, 16_000).len(), input.len());
    }
}
