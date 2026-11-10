#!/usr/bin/env python3
"""Measure loudness-normalised DRUM adventure-control render sweeps."""

import argparse
import json
import wave
from pathlib import Path

import numpy as np


def read_wav(path):
    with wave.open(str(path), "rb") as handle:
        rate, channels, width, frames = (handle.getframerate(), handle.getnchannels(),
                                         handle.getsampwidth(), handle.getnframes())
        raw = handle.readframes(frames)
    if channels != 1 or width not in (2, 3, 4):
        raise ValueError(f"{path}: expected mono 16/24/32-bit PCM")
    if width == 3:
        samples = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        samples = samples[:, 0] | (samples[:, 1] << 8) | (samples[:, 2] << 16)
        samples = np.where(samples & 0x800000, samples - 0x1000000, samples) / 8388608.0
    else:
        dtype, scale = (np.int16, 32768.0) if width == 2 else (np.int32, 2147483648.0)
        samples = np.frombuffer(raw, dtype=dtype).astype(np.float64) / scale
    return rate, samples


def normalise(samples):
    return samples / max(float(np.max(np.abs(samples))), 1.0e-12)


def metrics(rate, samples):
    samples = normalise(samples)
    size = 1 << (len(samples) - 1).bit_length()
    spectrum = np.abs(np.fft.rfft(samples * np.hanning(len(samples)), n=size)) ** 2
    frequencies = np.fft.rfftfreq(size, 1.0 / rate)
    spectrum[0] = 0.0
    total = max(float(spectrum.sum()), 1.0e-20)
    centroid = float((frequencies * spectrum).sum() / total)
    rolloff = float(frequencies[np.searchsorted(np.cumsum(spectrum), total * 0.85)])
    bands = [float(spectrum[(frequencies >= lo) & (frequencies < hi)].sum() * 100.0 / total)
             for lo, hi in ((0, 1000), (1000, 4000), (4000, 8000), (8000, 20000))]
    hop = max(1, int(rate * 0.005))
    frames = len(samples) // hop
    envelope = np.sqrt(np.mean(samples[:frames * hop].reshape(frames, hop) ** 2, axis=1))
    threshold = max(float(envelope.max()), 1.0e-12) * 0.01
    active = np.flatnonzero(envelope >= threshold)
    tail = float((active[-1] + 1) * hop / rate) if len(active) else 0.0
    attack_end = max(1, int(rate * 0.008))
    body_begin, body_end = attack_end, max(attack_end + 1, int(rate * 0.080))
    attack_rms = float(np.sqrt(np.mean(samples[:attack_end] ** 2)))
    body_rms = float(np.sqrt(np.mean(samples[body_begin:body_end] ** 2)))
    onset_end = min(len(samples), max(1, int(rate * 0.250)))
    onset = samples[:onset_end]
    onset_size = 1 << (len(onset) - 1).bit_length()
    onset_spectrum = np.abs(np.fft.rfft(onset * np.hanning(len(onset)), n=onset_size)) ** 2
    onset_frequencies = np.fft.rfftfreq(onset_size, 1.0 / rate)
    onset_spectrum[0] = 0.0
    onset_total = max(float(onset_spectrum.sum()), 1.0e-20)
    onset_centroid = float((onset_frequencies * onset_spectrum).sum() / onset_total)
    onset_bands = [float(onset_spectrum[(onset_frequencies >= lo) & (onset_frequencies < hi)].sum()
                         * 100.0 / onset_total)
                   for lo, hi in ((0, 1000), (1000, 4000), (4000, 8000), (8000, 20000))]
    transient_end = min(len(samples), max(1, int(rate * 0.025)))
    transient = samples[:transient_end]
    transient_size = 1 << (len(transient) - 1).bit_length()
    transient_spectrum = np.abs(np.fft.rfft(transient * np.hanning(len(transient)), n=transient_size)) ** 2
    transient_frequencies = np.fft.rfftfreq(transient_size, 1.0 / rate)
    transient_spectrum[0] = 0.0
    transient_total = max(float(transient_spectrum.sum()), 1.0e-20)
    transient_centroid = float((transient_frequencies * transient_spectrum).sum() / transient_total)
    transient_bands = [float(transient_spectrum[(transient_frequencies >= lo) & (transient_frequencies < hi)].sum()
                             * 100.0 / transient_total)
                       for lo, hi in ((0, 1000), (1000, 4000), (4000, 8000), (8000, 20000))]
    signature = onset[np.linspace(0, onset_end - 1, 512, dtype=int)]
    return {"centroid_hz": centroid, "rolloff85_hz": rolloff,
            "bands_pct": bands, "tail_40db_s": tail,
            "attack_to_body_ratio": attack_rms / max(body_rms, 1.0e-12),
            "onset_centroid_hz": onset_centroid, "onset_bands_pct": onset_bands,
            "transient_centroid_hz": transient_centroid, "transient_bands_pct": transient_bands,
            "onset_signature": signature.tolist()}


def comparison(reference, candidate):
    centroid_change = abs(candidate["centroid_hz"] / max(reference["centroid_hz"], 1.0e-12) - 1.0)
    rolloff_change = abs(candidate["rolloff85_hz"] / max(reference["rolloff85_hz"], 1.0e-12) - 1.0)
    band_change = max(abs(a - b) for a, b in zip(reference["bands_pct"], candidate["bands_pct"]))
    tail_ratio = max(candidate["tail_40db_s"], 1.0e-12) / max(reference["tail_40db_s"], 1.0e-12)
    tail_change = max(tail_ratio, 1.0 / tail_ratio)
    attack_ratio = max(candidate["attack_to_body_ratio"], 1.0e-12) / max(reference["attack_to_body_ratio"], 1.0e-12)
    attack_change = max(attack_ratio, 1.0 / attack_ratio)
    waveform_difference = float(np.sqrt(np.mean((np.array(reference["onset_signature"])
                                                   - np.array(candidate["onset_signature"])) ** 2)))
    onset_centroid_change = abs(candidate["onset_centroid_hz"]
                                / max(reference["onset_centroid_hz"], 1.0e-12) - 1.0)
    onset_band_change = max(abs(a - b) for a, b in zip(reference["onset_bands_pct"],
                                                        candidate["onset_bands_pct"]))
    transient_centroid_change = abs(candidate["transient_centroid_hz"]
                                    / max(reference["transient_centroid_hz"], 1.0e-12) - 1.0)
    transient_band_change = max(abs(a - b) for a, b in zip(reference["transient_bands_pct"],
                                                            candidate["transient_bands_pct"]))
    passes = sum((centroid_change >= 0.25 or rolloff_change >= 0.25,
                  band_change >= 10.0, tail_change >= 2.0))
    passes += attack_change >= 1.5
    passes += waveform_difference >= 0.10
    passes += onset_centroid_change >= 0.25 or onset_band_change >= 10.0
    passes += transient_centroid_change >= 0.25 or transient_band_change >= 10.0
    return {"centroid_or_rolloff_change": max(centroid_change, rolloff_change),
            "maximum_band_change_points": band_change, "tail_ratio": tail_ratio,
            "attack_ratio": attack_ratio, "onset_waveform_difference": waveform_difference,
            "onset_centroid_or_band_change": max(onset_centroid_change, onset_band_change / 100.0),
            "transient_centroid_or_band_change": max(transient_centroid_change, transient_band_change / 100.0),
            "substantial_metric_count": passes, "passes": passes >= 2}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--renders", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {}
    controls = {"kick": "PUNCH", "snare": "RING", "hat": "METAL",
                "crash": "EDGE", "ride": "BELL", "tom": "BEND", "clap": "ROOM"}
    for kit in ("808", "909"):
        for voice, control in controls.items():
            paths = {value: args.renders / f"{kit}_{voice}_{control}_{value}.wav"
                     for value in (-10, -5, 0, 5, 10)}
            if not all(path.exists() for path in paths.values()):
                continue
            rate, neutral = read_wav(paths[0])
            neutral_metrics = metrics(rate, neutral)
            report[f"{kit}_{voice}"] = {
                "neutral": neutral_metrics,
                "negative_endpoint": comparison(neutral_metrics, metrics(*read_wav(paths[-10]))),
                "positive_endpoint": comparison(neutral_metrics, metrics(*read_wav(paths[10]))),
                "positions": {str(value): metrics(*read_wav(path)) for value, path in paths.items()},
            }
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    failures = [name for name, entry in report.items()
                if not entry["negative_endpoint"]["passes"] or not entry["positive_endpoint"]["passes"]]
    print(f"Analysed {len(report)} sweeps; endpoint metric failures: {', '.join(failures) or 'none'}")


if __name__ == "__main__":
    main()
