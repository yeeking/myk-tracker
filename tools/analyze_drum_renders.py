#!/usr/bin/env python3
"""Compare rendered drum-synth WAVs with the local 808/909 reference sets.

Expected render names are <kit>_<voice>_v<velocity>.wav, for example
808_snare_v120.wav.  Reports are peak-normalised, so source recording level
does not bias the synthesis decisions.
"""
import argparse
import json
import wave
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np

REFERENCE = {
    "snare": "snare/snare_head_note_038/snare_head_{velocity:03d}.wav",
    "hat": "hihat/hihat_closed_bow_note_042/hihat_closed_bow_{velocity:03d}.wav",
    "openhat": "hihat/hihat_open_bow_note_046/hihat_open_bow_{velocity:03d}.wav",
    "crash": "crash1/crash1_bow_note_049/crash1_bow_{velocity:03d}.wav",
    "ride": "ride/ride_bow_note_051/ride_bow_{velocity:03d}.wav",
}


def read_wav(path):
    with wave.open(str(path), "rb") as handle:
        rate, channels, width, frames = handle.getframerate(), handle.getnchannels(), handle.getsampwidth(), handle.getnframes()
        raw = handle.readframes(frames)
    if channels != 1 or width not in (2, 3, 4):
        raise ValueError(f"{path}: expected mono 16/24/32-bit PCM")
    if width == 3:
        values = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        values = values[:, 0] | (values[:, 1] << 8) | (values[:, 2] << 16)
        values = np.where(values & 0x800000, values - 0x1000000, values) / 8388608.0
    else:
        dtype, scale = (np.int16, 32768.0) if width == 2 else (np.int32, 2147483648.0)
        values = np.frombuffer(raw, dtype=dtype).astype(np.float64) / scale
    return rate, values.astype(np.float64)


def normalise(samples):
    return samples / max(float(np.max(np.abs(samples))), 1.0e-12)


def metrics(rate, samples):
    samples = normalise(samples)
    hop = max(1, int(rate * 0.005))
    frames = len(samples) // hop
    envelope = np.sqrt(np.mean(samples[:frames * hop].reshape(frames, hop) ** 2, axis=1))
    peak = max(float(envelope.max()), 1.0e-12)
    tails = {}
    for decibels in (20, 40, 60):
        indices = np.flatnonzero(envelope >= peak * 10 ** (-decibels / 20))
        tails[f"tail_{decibels}db_s"] = float((indices[-1] + 1) * hop / rate) if len(indices) else 0.0
    size = 1 << (len(samples) - 1).bit_length()
    spectrum = np.abs(np.fft.rfft(samples * np.hanning(len(samples)), n=size)) ** 2
    frequencies = np.fft.rfftfreq(size, 1.0 / rate)
    spectrum[0] = 0.0
    total = max(float(spectrum.sum()), 1.0e-20)
    centroid = float((frequencies * spectrum).sum() / total)
    cumulative = np.cumsum(spectrum)
    rolloff = float(frequencies[np.searchsorted(cumulative, total * 0.85)])
    bands = {f"band_{lo}_{hi}_pct": float(spectrum[(frequencies >= lo) & (frequencies < hi)].sum() * 100 / total)
             for lo, hi in ((0, 250), (250, 1000), (1000, 4000), (4000, 8000), (8000, 20000))}
    return {"duration_s": len(samples) / rate, "centroid_hz": centroid, "rolloff85_hz": rolloff, **tails, **bands}, envelope


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--references", required=True, type=Path, help="Directory containing 064_808 and 065_909")
    parser.add_argument("--renders", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--velocity", type=int, default=120)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {}
    for kit, source_dir in (("808", "064_808"), ("909", "065_909")):
        for voice, pattern in REFERENCE.items():
            reference = args.references / source_dir / pattern.format(velocity=args.velocity)
            rendered = args.renders / f"{kit}_{voice}_v{args.velocity:03d}.wav"
            if not reference.exists() or not rendered.exists():
                continue
            ref_rate, ref = read_wav(reference)
            render_rate, render = read_wav(rendered)
            ref_metrics, ref_envelope = metrics(ref_rate, ref)
            render_metrics, render_envelope = metrics(render_rate, render)
            key = f"{kit}_{voice}"
            report[key] = {"reference": ref_metrics, "render": render_metrics}
            seconds = max(len(ref_envelope) / 200.0, len(render_envelope) / 200.0)
            figure, axis = plt.subplots(figsize=(8, 3))
            axis.plot(np.arange(len(ref_envelope)) / 200.0, 20 * np.log10(np.maximum(ref_envelope, 1e-8)), label="reference")
            axis.plot(np.arange(len(render_envelope)) / 200.0, 20 * np.log10(np.maximum(render_envelope, 1e-8)), label="synth")
            axis.set(xlim=(0, seconds), ylim=(-80, 0), xlabel="seconds", ylabel="dB RMS", title=key)
            axis.legend()
            figure.tight_layout()
            figure.savefig(args.output / f"{key}_envelope.png", dpi=150)
            plt.close(figure)
            figure, axes = plt.subplots(2, 1, figsize=(8, 5), sharex=False)
            axes[0].specgram(normalise(ref) + 1.0e-12, NFFT=1024, Fs=ref_rate, noverlap=768, cmap="magma")
            axes[0].set(title=f"{key} reference", ylabel="Hz", ylim=(0, 16000))
            axes[1].specgram(normalise(render) + 1.0e-12, NFFT=1024, Fs=render_rate, noverlap=768, cmap="magma")
            axes[1].set(title=f"{key} synth", xlabel="seconds", ylabel="Hz", ylim=(0, 16000))
            figure.tight_layout()
            figure.savefig(args.output / f"{key}_spectrogram.png", dpi=150)
            plt.close(figure)
    (args.output / "drum-comparison.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
