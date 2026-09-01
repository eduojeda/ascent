#!/usr/bin/env python3
"""Placeholder SFX generator for the Ascent SDL3 port.

The original 2002 sound assets were lost; these are functional stand-ins.
Writes 16-bit mono PCM WAV files at 22050 Hz using only the stdlib.

Usage:
    gen_sfx.py                 write all synthesized SFX into assets/sounds/
    gen_sfx.py clicks OUT.wav  write only the double-click part used to build
                               clickclick-oh-shit.wav (see gen-sounds.sh)
"""

import math
import random
import struct
import sys
import wave
from pathlib import Path

SR = 22050
PEAK = 0.7
TWO_PI = 2.0 * math.pi


# ---------------------------------------------------------------- primitives

def sine(p):
    return math.sin(TWO_PI * p)


def square(p):
    return 1.0 if (p % 1.0) < 0.5 else -1.0


def saw(p):
    return 2.0 * (p % 1.0) - 1.0


def noise():
    return random.uniform(-1.0, 1.0)


def tone(freq, dur, wave_fn=sine):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for _ in range(n):
        phase += freq / SR
        out.append(wave_fn(phase))
    return out


def decay(samples, k):
    n = len(samples)
    return [s * math.exp(-k * i / n) for i, s in enumerate(samples)]


def env(samples, attack=0.005, release=0.02):
    na = min(int(attack * SR), len(samples) // 2)
    nr = min(int(release * SR), len(samples) // 2)
    for i in range(na):
        samples[i] *= i / na
    for i in range(nr):
        samples[-1 - i] *= i / nr
    return samples


def lowpass(samples, cutoff):
    """One-pole lowpass; cutoff is Hz, or a callable of normalized time."""
    out = []
    y = 0.0
    n = len(samples)
    for i, x in enumerate(samples):
        fc = cutoff(i / n) if callable(cutoff) else cutoff
        a = 1.0 - math.exp(-TWO_PI * fc / SR)
        y += a * (x - y)
        out.append(y)
    return out


def mix_at(buf, offset, part, gain=1.0, grow=True):
    if grow and len(buf) < offset + len(part):
        buf.extend([0.0] * (offset + len(part) - len(buf)))
    for i, s in enumerate(part):
        j = offset + i
        if j >= len(buf):
            break
        buf[j] += s * gain
    return buf


def bell(freq, dur, amps=(1.0, 0.4, 0.15), k=5.0):
    n = int(dur * SR)
    out = []
    for i in range(n):
        t = i / SR
        e = math.exp(-k * i / n)
        s = sum(a * math.sin(TWO_PI * freq * (h + 1) * t)
                for h, a in enumerate(amps))
        out.append(s * e)
    return out


def write_wav(path, samples):
    m = max(abs(s) for s in samples) or 1.0
    g = PEAK / m
    samples = [s * g for s in samples]
    env(samples, attack=0.004, release=0.004)
    frames = b"".join(
        struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32767))
        for s in samples)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(frames)


# -------------------------------------------------------------------- sounds

def laser(f0, f1, dur=0.15):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        f = f0 + (f1 - f0) * (t ** 0.7)
        phase += f / SR
        out.append(0.6 * square(phase) + 0.4 * saw(phase))
    return decay(out, 3.0)


def hit_laser(dur=0.3):
    n = int(dur * SR)
    out = []
    phase = 0.0
    mod = 0.0
    for i in range(n):
        t = i / n
        phase += (220.0 - 80.0 * t) / SR
        mod += 47.0 / SR
        s = square(phase) * (0.6 + 0.4 * square(mod))
        out.append(s + 0.25 * noise())
    return decay(out, 2.0)


def blip(f0, f1, dur=0.1):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        phase += (f0 + (f1 - f0) * t) / SR
        out.append(sine(phase) + 0.3 * sine(2 * phase))
    return env(out, 0.005, 0.03)


def explosion(dur=1.0):
    n = int(dur * SR)
    body = lowpass([noise() for _ in range(n)],
                   lambda t: 3000.0 - 2850.0 * t)
    body = decay(body, 5.0)
    thump_n = int(0.35 * SR)
    thump = []
    phase = 0.0
    for i in range(thump_n):
        t = i / thump_n
        phase += (100.0 - 70.0 * t) / SR
        thump.append(sine(phase))
    thump = decay(thump, 4.0)
    return mix_at(body, 0, thump, gain=1.2, grow=False)


def bullethit(dur=0.15):
    n = int(dur * SR)
    out = []
    freqs = (1270.0, 1830.0, 2510.0)
    for i in range(n):
        t = i / SR
        ring = sum(math.sin(TWO_PI * f * t) for f in freqs) / 3.0
        ring *= math.exp(-i / (0.03 * SR))
        crack = 0.8 * noise() * math.exp(-i / (0.008 * SR))
        out.append(ring + crack)
    return out


def missile(dur=0.8):
    n = int(dur * SR)
    swept = lowpass([noise() for _ in range(n)],
                    lambda t: 250.0 + 3500.0 * t * t)
    return [s * (0.15 + 0.85 * (i / n) ** 1.3) for i, s in enumerate(swept)]


def rocket(dur=1.0):
    n = int(dur * SR)
    body = lowpass([noise() for _ in range(n)],
                   lambda t: 180.0 + 700.0 * t)
    out = []
    phase = 0.0
    wob = 1.0
    for i, s in enumerate(body):
        t = i / n
        if i % 64 == 0:
            wob = 0.7 + 0.6 * random.random()
        phase += 42.0 / SR
        a = 0.25 + 0.75 * min(1.0, t / 0.3)
        if t > 0.7:
            a *= 1.0 - 0.6 * (t - 0.7) / 0.3
        out.append((0.8 * s + 0.5 * sine(phase) * wob) * a)
    return out


def bounce(dur=0.12):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        phase += (150.0 - 90.0 * t) / SR
        out.append(sine(phase))
    return decay(out, 6.0)


def magneto(dur=0.8):
    n = int(dur * SR)
    out = []
    phase = 0.0
    lfo = 0.0
    for i in range(n):
        t = i / n
        lfo += (5.0 + 20.0 * t) / SR
        depth = 0.02 + 0.08 * t
        f = 130.0 * (1.0 + depth * math.sin(TWO_PI * lfo))
        phase += f / SR
        out.append(0.5 * square(phase) + 0.35 * sine(2 * phase)
                   + 0.15 * sine(3 * phase))
    return env(out, 0.02, 0.15)


def servo(f0, f1, dur=0.5):
    n = int(dur * SR)
    out = []
    phase = 0.0
    buzz = 0.0
    for i in range(n):
        t = i / n
        phase += (f0 + (f1 - f0) * t) / SR
        buzz += 55.0 / SR
        am = 0.7 + 0.3 * square(buzz)
        out.append(saw(phase) * am + 0.12 * noise())
    return env(out, 0.02, 0.05)


def shieldreload():
    out = []
    for k, f in enumerate((523.25, 659.25, 783.99, 1046.50)):
        mix_at(out, int(k * 0.12 * SR), bell(f, 0.25, k=4.0))
    return out


def ballshot(dur=0.3):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        phase += (320.0 - 250.0 * (t ** 0.6)) / SR
        out.append(math.tanh(3.0 * (0.7 * saw(phase) + 0.3 * square(phase))))
    return decay(out, 2.2)


def ratchet(f0, f1, dur=0.4, clicks=8):
    n = int(dur * SR)
    out = [0.0] * n
    m = int(0.012 * SR)
    for c in range(clicks):
        start = int(c * n / clicks)
        f = f0 + (f1 - f0) * c / (clicks - 1)
        phase = 0.0
        for j in range(m):
            if start + j >= n:
                break
            phase += f / SR
            e = math.exp(-j / (0.003 * SR))
            out[start + j] += (0.8 * square(phase) + 0.4 * noise()) * e
    return out


def bass(freq, dur=0.25):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for _ in range(n):
        phase += freq / SR
        s = 0.6 * sine(phase) + 0.3 * saw(phase) + 0.15 * sine(2 * phase)
        out.append(math.tanh(1.5 * s))
    return decay(out, 2.5)


def pause_sound():
    out = env(decay(tone(620.0, 0.07, square), 1.0), 0.004, 0.01)
    out += [0.0] * int(0.04 * SR)
    out += env(decay(tone(930.0, 0.09, square), 1.5), 0.004, 0.02)
    return out


def goodie_bounce(dur=0.2):
    n = int(dur * SR)
    out = []
    phase = 0.0
    for i in range(n):
        t = i / SR
        f = 260.0 + 240.0 * math.exp(-10.0 * t) * math.cos(TWO_PI * 22.0 * t)
        phase += f / SR
        out.append(sine(phase))
    return decay(out, 2.5)


def goodie_catch():
    out = bell(1046.50, 0.40, k=4.0)
    return mix_at(out, int(0.05 * SR), bell(1318.51, 0.35, k=4.0), gain=0.8)


def goodie_appears():
    out = []
    for k, f in enumerate((1567.98, 1975.53, 2349.32, 2793.83, 3520.0)):
        part = bell(f, 0.2, amps=(1.0, 0.25), k=6.0)
        detune = bell(f * 1.01, 0.2, amps=(1.0, 0.25), k=6.0)
        mix_at(out, int(k * 0.07 * SR), part, gain=0.7)
        mix_at(out, int(k * 0.07 * SR), detune, gain=0.35)
    return out


def clap_burst():
    m = int(0.025 * SR)
    burst = [noise() * math.exp(-j / (0.004 * SR)) for j in range(m)]
    return lowpass(burst, random.uniform(1500.0, 4000.0))


def applause(dur=2.5):
    n = int(dur * SR)
    out = [0.0] * n
    t = 0.01
    while t < dur - 0.1:
        pos = t / dur
        density = min(1.0, pos / 0.15)
        if pos > 0.75:
            density *= max(0.0, (1.0 - pos) / 0.25)
        if random.random() < density:
            mix_at(out, int(t * SR), clap_burst(),
                   gain=0.3 + 0.7 * random.random(), grow=False)
        t += random.uniform(0.01, 0.05)
    return out


def clicks_part():
    out = [0.0] * int(0.32 * SR)
    m = int(0.008 * SR)
    for t0 in (0.03, 0.13):
        start = int(t0 * SR)
        burst = [noise() * math.exp(-j / (0.0015 * SR)) for j in range(m)]
        mix_at(out, start, lowpass(burst, 5000.0), grow=False)
    return out


SOUNDS = {
    "redshot": lambda: laser(750.0, 140.0),
    "greenshot": lambda: laser(950.0, 220.0),
    "hit-laser": hit_laser,
    "catch": lambda: blip(500.0, 1100.0),
    "release": lambda: blip(1100.0, 500.0),
    "explosion": explosion,
    "bullethit": bullethit,
    "missile": missile,
    "rocket": rocket,
    "bounce": bounce,
    "magneto": magneto,
    "base-opening": lambda: servo(150.0, 650.0),
    "base-closing": lambda: servo(650.0, 150.0),
    "shieldreload": shieldreload,
    "ballshot": ballshot,
    "ballspawneropening": lambda: ratchet(400.0, 900.0),
    "ballspawnerclosing": lambda: ratchet(900.0, 400.0),
    "low-bass": lambda: bass(55.0),
    "high-bass": lambda: bass(82.41),
    "pause": pause_sound,
    "goodie-bounce": goodie_bounce,
    "goodie-catch": goodie_catch,
    "goodie-appears": goodie_appears,
    "applause": applause,
}


def main():
    random.seed(1226)
    if len(sys.argv) >= 2 and sys.argv[1] == "clicks":
        write_wav(sys.argv[2], clicks_part())
        return
    out_dir = Path(__file__).resolve().parent.parent / "assets" / "sounds"
    out_dir.mkdir(parents=True, exist_ok=True)
    for name, fn in SOUNDS.items():
        write_wav(out_dir / f"{name}.wav", fn())
        print(f"  {name}.wav")


if __name__ == "__main__":
    main()
