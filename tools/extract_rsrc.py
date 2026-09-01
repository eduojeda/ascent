#!/usr/bin/env python3
"""Extracts every resource from a Classic Mac resource fork.

Usage: extract_rsrc.py <file-with-fork | raw-fork-dump> <outdir>

Reads the fork via ..namedfork/rsrc when present, else treats the file
itself as raw fork bytes. Writes <outdir>/<TYPE>/<id>[-<name>].bin and
prints an inventory. 'snd ' resources are additionally decoded to WAV.
"""

import os
import struct
import sys
import wave


def read_fork(path):
    forkpath = os.path.join(path, "..namedfork", "rsrc")
    if os.path.exists(forkpath) and os.path.getsize(forkpath) > 0:
        with open(forkpath, "rb") as f:
            return f.read()
    with open(path, "rb") as f:
        return f.read()


def parse(fork):
    data_off, map_off, data_len, map_len = struct.unpack_from(">IIII", fork, 0)
    type_list_off, name_list_off = struct.unpack_from(">HH", fork,
                                                      map_off + 24)
    tl = map_off + type_list_off
    nl = map_off + name_list_off
    (ntypes,) = struct.unpack_from(">H", fork, tl)
    ntypes = (ntypes + 1) & 0xFFFF
    out = []
    for t in range(ntypes):
        rtype, cnt, ref_off = struct.unpack_from(">4sHH", fork, tl + 2 + 8 * t)
        for r in range(cnt + 1):
            base = tl + ref_off + 12 * r
            rid, name_off = struct.unpack_from(">hh", fork, base)
            (attr_and_off,) = struct.unpack_from(">I", fork, base + 4)
            off = attr_and_off & 0xFFFFFF
            name = None
            if name_off >= 0:
                ln = fork[nl + name_off]
                name = fork[nl + name_off + 1:nl + name_off + 1 + ln].decode(
                    "macroman")
            (rlen,) = struct.unpack_from(">I", fork, data_off + off)
            payload = fork[data_off + off + 4:data_off + off + 4 + rlen]
            out.append((rtype.decode("macroman"), rid, name, payload))
    return out


def snd_to_wav(payload, outpath):
    """Decodes a format 1/2 'snd ' resource holding sampled sound."""
    pos = 0
    (fmt,) = struct.unpack_from(">H", payload, pos)
    pos += 2
    if fmt == 1:
        (nmods,) = struct.unpack_from(">H", payload, pos)
        pos += 2 + 6 * nmods
    elif fmt == 2:
        pos += 2  # reference count
    else:
        return "unknown snd format %d" % fmt
    (ncmds,) = struct.unpack_from(">H", payload, pos)
    pos += 2
    sound_off = None
    for _ in range(ncmds):
        cmd, p1, p2 = struct.unpack_from(">HHI", payload, pos)
        pos += 8
        if cmd & 0x7FFF in (0x8050 & 0x7FFF, 0x8051 & 0x7FFF, 80, 81):
            sound_off = p2
    if sound_off is None:
        return "no buffer/sound command"
    # SoundHeader
    ptr, length, rate, loop_s, loop_e, enc, base = struct.unpack_from(
        ">IIIIIBB", payload, sound_off)
    rate_hz = rate / 65536.0
    if enc == 0x00:  # standard: 8-bit mono
        frames = payload[sound_off + 22:sound_off + 22 + length]
        channels, width = 1, 1
    elif enc == 0xFF:  # extended header
        num_channels, = struct.unpack_from(">I", payload, sound_off + 4)
        num_frames, = struct.unpack_from(">I", payload, sound_off + 22)
        bits, = struct.unpack_from(">H", payload, sound_off + 48)
        hdr = 64
        channels, width = num_channels, bits // 8
        frames = payload[sound_off + hdr:
                         sound_off + hdr + num_frames * channels * width]
    else:
        return "compressed snd (enc 0x%02x), not handled" % enc
    with wave.open(outpath, "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(width)
        w.setframerate(int(rate_hz))
        if width == 1:
            w.writeframes(frames)  # 8-bit WAV is unsigned, same as snd
        else:
            # 16-bit snd is big-endian signed; WAV wants little-endian
            w.writeframes(bytearray(b for i in range(0, len(frames), 2)
                                    for b in (frames[i + 1], frames[i])))
    return "ok %.0f Hz %dch %d-bit %.2fs" % (
        rate_hz, channels, width * 8,
        len(frames) / (rate_hz * channels * width))


def main():
    src, outdir = sys.argv[1], sys.argv[2]
    fork = read_fork(src)
    resources = parse(fork)
    counts = {}
    for rtype, rid, name, payload in resources:
        counts.setdefault(rtype, []).append(rid)
        safe_type = rtype.strip().replace("#", "_") or "blank"
        d = os.path.join(outdir, safe_type)
        os.makedirs(d, exist_ok=True)
        fname = f"{rid}" + (f"-{name}" if name else "")
        fname = "".join(c if c.isalnum() or c in " .!-_'" else "_"
                        for c in fname)
        with open(os.path.join(d, fname + ".bin"), "wb") as f:
            f.write(payload)
        if rtype == "snd ":
            msg = snd_to_wav(payload,
                             os.path.join(d, fname + ".wav"))
            print(f"snd {rid:6d} {name or '':30s} {msg}")
    print()
    for rtype in sorted(counts):
        ids = counts[rtype]
        print(f"{rtype!r} x{len(ids)}: {sorted(ids)[:20]}"
              f"{' ...' if len(ids) > 20 else ''}")


if __name__ == "__main__":
    main()
