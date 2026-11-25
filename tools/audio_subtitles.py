#!/usr/bin/env python3
"""Inspect, audit, and export Dead Space Mobile audio (soundBase.sb, .sps/.sph) and subtitles (subtitles.sb).

Usage:
  audio_subtitles.py dump SOUNDBASE_OR_ASSETS [--filter TEXT] [--type N] [--chains-only]
  audio_subtitles.py audit ASSETS_DIR
  audio_subtitles.py export-audio INPUT [--key KEY] [--chain] [--format wav|mp3] -o OUT
  audio_subtitles.py export-subtitles ASSETS_DIR [--lang en-us] [--key KEY] [--format srt|txt] [-o OUT | --dir DIR]

Formats:
  - published/sounds/soundBase.sb: sequential records of
      u32 name_len, char name[name_len] (NUL-terminated),
      u32 next_len, char next[next_len] (NUL-terminated; len 1 = empty),
      u32 type, u32 mode, u32 min_range, u32 max_range, u32 file_count,
      file_count * (u32 path_len, char path[path_len]).
  - .sps / .sph: EA SNS/SPS block stream with 0x48 header, 0x44 data blocks (EA XAS1 4-bit ADPCM,
    codec byte 0x14, 76 bytes per 128 samples per channel), and 0x45 end marker.
  - published/data/subtitles.sb & published/stringdata/<lang>/deadspace.bin: SBIN v2 databases.
"""
import argparse
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import wave

# Standard EA XAS1 4-bit ADPCM predictor filter coefficients.
_XAS1_COEF = (
    (0.0, 0.0),
    (0.9375, 0.0),
    (1.796875, -0.8125),
    (1.53125, -0.859375),
)


class ToolError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------------
# soundBase.sb parser / serializer
# ---------------------------------------------------------------------------------------------------------------

def parse_sound_base(raw: bytes):
    """Parse published/sounds/soundBase.sb into a list of record dicts."""
    if not raw:
        raise ToolError("soundBase.sb is empty")
    records = []
    off = 0
    total = len(raw)

    def read_cstr():
        nonlocal off
        if off + 4 > total:
            raise ToolError("truncated string length at offset %d" % off)
        ln, = struct.unpack_from("<I", raw, off)
        off += 4
        if ln == 0 or off + ln > total:
            raise ToolError("invalid string length %d at offset %d" % (ln, off - 4))
        text = raw[off:off + ln].rstrip(b"\x00").decode("ascii", "replace")
        off += ln
        return text

    while off < total:
        name = read_cstr()
        nxt = read_cstr()
        if off + 20 > total:
            raise ToolError("truncated record fields for %r at offset %d" % (name, off))
        stype, mode, min_r, max_r, count = struct.unpack_from("<5I", raw, off)
        off += 20
        if count > 1024:
            raise ToolError("implausible file count %d for %r" % (count, name))
        files = [read_cstr() for _ in range(count)]
        records.append({
            "name": name,
            "next": nxt,
            "type": stype,
            "mode": mode,
            "min_range": min_r,
            "max_range": max_r,
            "files": files,
        })
    return records


def serialize_sound_base(records) -> bytes:
    out = bytearray()

    def write_cstr(s: str):
        b = s.encode("ascii") + b"\x00"
        out.extend(struct.pack("<I", len(b)))
        out.extend(b)

    for r in records:
        write_cstr(r["name"])
        write_cstr(r.get("next", ""))
        files = r.get("files", [])
        out.extend(struct.pack(
            "<5I", r["type"], r["mode"], r["min_range"], r["max_range"], len(files)
        ))
        for f in files:
            write_cstr(f)
    return bytes(out)


def normalize_sound_rel(rel: str) -> str:
    norm = rel.replace("\\", "/")
    while norm.startswith("../"):
        norm = norm[3:]
    while norm.startswith("./"):
        norm = norm[2:]
    return norm


def trace_chain(by_name, start_key: str):
    chain = []
    cur = start_key
    seen = set()
    while cur and cur not in seen:
        seen.add(cur)
        rec = by_name.get(cur)
        if rec is None:
            chain.append({"name": cur, "missing": True})
            break
        chain.append(rec)
        cur = rec["next"]
    return chain


# ---------------------------------------------------------------------------------------------------------------
# .sph / .sps (EA XAS1) decoding
# ---------------------------------------------------------------------------------------------------------------

def parse_sph_header(raw: bytes):
    """Return (codec, channels, sample_rate, num_samples) from an 8-byte .sph header."""
    if len(raw) < 8:
        raise ToolError("short .sph header (%d bytes)" % len(raw))
    codec = raw[0]
    channels = (raw[1] >> 2) + 1
    rate = (raw[2] << 8) | raw[3]
    num_samples = struct.unpack_from(">I", raw, 4)[0] & 0x1FFFFFFF
    return codec, channels, rate, num_samples


def sph_duration_seconds(sps_path: pathlib.Path) -> float:
    sph_path = sps_path.with_suffix(".sph")
    if sph_path.is_file():
        _, _, rate, ns = parse_sph_header(sph_path.read_bytes())
        return ns / float(rate) if rate else 0.0
    if sps_path.is_file():
        raw = sps_path.read_bytes()
        if len(raw) >= 12 and raw[0] == 0x48:
            _, _, rate, ns = parse_sph_header(raw[4:12])
            return ns / float(rate) if rate else 0.0
    return 0.0


def decode_xas1_block(blk: bytes):
    """Decode one 76-byte EA XAS1 block into 128 signed 16-bit PCM samples."""
    if len(blk) != 76:
        raise ToolError("XAS1 block must be 76 bytes, got %d" % len(blk))
    out = [0] * 128
    for g in range(4):
        h0, h1 = struct.unpack_from("<hh", blk, g * 4)
        c0, c1 = _XAS1_COEF[h0 & 3]
        s1 = float(h0 & ~0xF)
        shift = h1 & 0xF
        s0 = float(h1 & ~0xF)
        scale = float(1 << (12 - shift)) if shift <= 12 else (4096.0 / float(1 << shift))
        base = g * 32
        out[base] = max(-32768, min(32767, int(round(s1))))
        out[base + 1] = max(-32768, min(32767, int(round(s0))))
        prev1, prev2 = s0, s1
        idx = base + 2
        for row in range(15):
            byte = blk[16 + row * 4 + g]
            for nib in ((byte >> 4) & 0xF, byte & 0xF):
                if nib >= 8:
                    nib -= 16
                samp = prev1 * c0 + prev2 * c1 + nib * scale
                if samp > 32767.0:
                    samp = 32767.0
                elif samp < -32768.0:
                    samp = -32768.0
                q = int(round(samp))
                out[idx] = q
                prev2 = prev1
                prev1 = float(q)
                idx += 1
    return out


def decode_sps_bytes(raw: bytes):
    """Decode an EA .sps (codec 0x14 / XAS1) stream -> (channels, sample_rate, interleaved_int16_bytes)."""
    off = 0
    total = len(raw)
    channels = 1
    rate = 22050
    total_samples = 0
    pcm_ch = None
    saw_header = False

    while off + 4 <= total:
        btype = raw[off]
        bsz = (raw[off + 1] << 16) | (raw[off + 2] << 8) | raw[off + 3]
        if bsz < 4 or off + bsz > total:
            raise ToolError("invalid SPS block size %d at offset %d" % (bsz, off))
        payload = raw[off + 4:off + bsz]
        if btype == 0x48:
            codec, channels, rate, total_samples = parse_sph_header(payload)
            if codec != 0x14:
                raise ToolError("unsupported SPS codec 0x%02x (only EA XAS1 0x14 is supported)" % codec)
            pcm_ch = [[] for _ in range(channels)]
            saw_header = True
        elif btype == 0x44:
            if not saw_header:
                raise ToolError("SPS data block before header block")
            if len(payload) < 4:
                raise ToolError("truncated SPS data block at offset %d" % off)
            blk_samples, = struct.unpack_from(">I", payload, 0)
            pos = 4
            n_frames = (len(payload) - 4) // (76 * channels)
            got = 0
            for _ in range(n_frames):
                for ch in range(channels):
                    samps = decode_xas1_block(payload[pos:pos + 76])
                    pos += 76
                    take = min(128, blk_samples - got)
                    if take > 0:
                        pcm_ch[ch].extend(samps[:take])
                got += 128
        elif btype == 0x45:
            break
        off += bsz

    if not saw_header or pcm_ch is None:
        raise ToolError("missing SPS header block (0x48)")
    avail = min(len(c) for c in pcm_ch)
    n = min(total_samples, avail) if total_samples else avail
    if channels == 1:
        inter = struct.pack("<%dh" % n, *pcm_ch[0][:n])
    else:
        flat = [pcm_ch[ch][i] for i in range(n) for ch in range(channels)]
        inter = struct.pack("<%dh" % len(flat), *flat)
    return channels, rate, inter


def write_wav(path: pathlib.Path, channels: int, rate: int, pcm: bytes):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(rate)
        wf.writeframes(pcm)


def write_audio(path: pathlib.Path, channels: int, rate: int, pcm: bytes, fmt: str):
    if fmt == "wav":
        write_wav(path, channels, rate, pcm)
        return
    if fmt == "mp3":
        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            raise ToolError("ffmpeg was not found on PATH (needed for --format mp3; use --format wav instead)")
        path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory() as tmp:
            tmp_wav = pathlib.Path(tmp) / "clip.wav"
            write_wav(tmp_wav, channels, rate, pcm)
            r = subprocess.run(
                [ffmpeg, "-y", "-v", "error", "-i", str(tmp_wav), "-codec:a", "libmp3lame", "-q:a", "2", str(path)],
                capture_output=True, text=True,
            )
            if r.returncode != 0:
                raise ToolError("ffmpeg failed: %s" % (r.stderr.strip() or "exit %d" % r.returncode))
        return
    raise ToolError("unsupported audio format %r" % fmt)


# ---------------------------------------------------------------------------------------------------------------
# SBIN v2 readers for subtitles.sb, stringdata/<lang>/deadspace.bin, and maps/*.sb
# ---------------------------------------------------------------------------------------------------------------

def parse_sbin_chunks(raw: bytes):
    if len(raw) < 8 or raw[:4] != b"SBIN":
        raise ToolError("not an SBIN database")
    chunks = {}
    pos = 8
    while pos + 12 <= len(raw):
        tag = raw[pos:pos + 4].decode("latin-1")
        sz, _ = struct.unpack_from("<II", raw, pos + 4)
        if pos + 12 + sz > len(raw):
            raise ToolError("truncated SBIN chunk %s" % tag)
        chunks[tag] = raw[pos + 12:pos + 12 + sz]
        pos = (pos + 12 + sz + 3) & ~3
    return chunks


def parse_sbin_strings(chunks):
    p = chunks.get("STRS")
    if p is None or len(p) < 4:
        return []
    n, = struct.unpack_from("<I", p, 0)
    q = 4
    out = []
    for _ in range(n):
        ln, = struct.unpack_from("<I", p, q)
        out.append(p[q + 4:q + 4 + ln].decode("utf-8", "replace"))
        q += 4 + ln
    return out


def parse_subtitles_sb(raw: bytes):
    """Parse published/data/subtitles.sb -> list of (audio_name, [(step_id, timeout_ms), ...])."""
    chunks = parse_sbin_chunks(raw)
    strs = parse_sbin_strings(chunks)
    ohdr_raw = chunks["OHDR"]
    data = chunks["DATA"]
    ohdr = [struct.unpack_from("<I", ohdr_raw, i)[0] for i in range(0, len(ohdr_raw), 4)]

    def get_str(idx1):
        return strs[idx1 - 1] if 0 < idx1 <= len(strs) else ""

    def obj_bounds(idx):
        h = ohdr[idx]
        kind = h & 7
        off = h >> 3
        hdr_sz = (2, 4, 8)[min(kind, 2)]
        return off, off + hdr_sz, kind

    # Find the array of Subtitle structs (AudioName + Steps)
    arr_idx = 1 if len(ohdr) > 1 and (ohdr[1] & 7) == 2 else None
    if arr_idx is None:
        raise ToolError("could not locate Subtitles array in subtitles.sb")
    raw_off, body_off, _ = obj_bounds(arr_idx)
    _, _, cnt = struct.unpack_from("<HHI", data, raw_off)
    tracks = []
    for i in range(cnt):
        an_idx, _, steps_obj = struct.unpack_from("<HHI", data, body_off + i * 8)
        s_raw, s_body, _ = obj_bounds(steps_obj)
        _, _, s_cnt = struct.unpack_from("<HHI", data, s_raw)
        steps = []
        for j in range(s_cnt):
            id_idx, _, timeout = struct.unpack_from("<HHf", data, s_body + j * 8)
            steps.append((get_str(id_idx), timeout))
        tracks.append((get_str(an_idx), steps))
    return tracks


def parse_string_table_bin(raw: bytes):
    """Parse published/stringdata/<lang>/deadspace.bin -> dict[string_id, text]."""
    chunks = parse_sbin_chunks(raw)
    strs = parse_sbin_strings(chunks)
    ohdr_raw = chunks["OHDR"]
    data = chunks["DATA"]
    ohdr = [struct.unpack_from("<I", ohdr_raw, i)[0] for i in range(0, len(ohdr_raw), 4)]

    def get_str(idx1):
        return strs[idx1 - 1] if 0 < idx1 <= len(strs) else ""

    # Locate the StringPair array (kind == 2, elem_type == 16)
    out = {}
    for h in ohdr:
        if (h & 7) != 2:
            continue
        off = h >> 3
        et, _, cnt = struct.unpack_from("<HHI", data, off)
        if et != 16:
            continue
        # StringPair layout: u16 stringid @0, u16 string @2, i32 halversion @4 (stride 8)
        body = off + 8
        for i in range(cnt):
            sid, txt, _ = struct.unpack_from("<HHi", data, body + i * 8)
            key = get_str(sid)
            if key:
                out[key] = get_str(txt)
        break
    return out


# ---------------------------------------------------------------------------------------------------------------
# Helpers for resolving paths & SRT formatting
# ---------------------------------------------------------------------------------------------------------------

def resolve_sound_base_and_root(path_str: str):
    p = pathlib.Path(path_str)
    if p.is_dir():
        sb = p / "published" / "sounds" / "soundBase.sb"
        if sb.is_file():
            return sb, p / "published" / "sounds"
        sb2 = p / "soundBase.sb"
        if sb2.is_file():
            return sb2, p
        raise ToolError("could not find soundBase.sb under %s" % path_str)
    if p.is_file():
        return p, p.parent
    raise ToolError("path does not exist: %s" % path_str)


def format_srt_time(ms: float) -> str:
    ms_int = max(0, int(round(ms)))
    hours, rem = divmod(ms_int, 3600000)
    minutes, rem = divmod(rem, 60000)
    seconds, millis = divmod(rem, 1000)
    return "%02d:%02d:%02d,%03d" % (hours, minutes, seconds, millis)


def format_subtitles_srt(steps, strings, fallback_ms=3000.0) -> str:
    lines = []
    cur = 0.0
    for idx, (sid, timeout) in enumerate(steps, 1):
        dur = timeout if timeout > 0 else fallback_ms
        text = strings.get(sid, "<%s>" % sid)
        lines.append(str(idx))
        lines.append("%s --> %s" % (format_srt_time(cur), format_srt_time(cur + dur)))
        lines.append(text)
        lines.append("")
        cur += dur
    return "\n".join(lines)


def format_subtitles_txt(audio_name: str, steps, strings, fallback_ms=3000.0) -> str:
    lines = ["[%s]" % audio_name]
    cur = 0.0
    for sid, timeout in steps:
        dur = timeout if timeout > 0 else fallback_ms
        text = strings.get(sid, "<%s>" % sid)
        lines.append("  %6.2fs - %6.2fs  %-12s  %s" % (cur / 1000.0, (cur + dur) / 1000.0, sid, text))
        cur += dur
    return "\n".join(lines)


# ---------------------------------------------------------------------------------------------------------------
# Subcommands
# ---------------------------------------------------------------------------------------------------------------

def cmd_dump(args):
    sb_path, sounds_root = resolve_sound_base_and_root(args.path)
    records = parse_sound_base(sb_path.read_bytes())
    by_name = {r["name"]: r for r in records}
    shown = 0
    for r in records:
        if args.type is not None and r["type"] != args.type:
            continue
        if args.chains_only and not r["next"]:
            continue
        if args.filter and args.filter.lower() not in r["name"].lower():
            continue
        durs = []
        for f in r["files"]:
            norm = normalize_sound_rel(f)
            dur = sph_duration_seconds(sounds_root / norm)
            durs.append("%s (%.2fs)" % (norm, dur))
        chain_str = ""
        if r["next"]:
            chain = trace_chain(by_name, r["name"])
            chain_str = "  chain=" + " -> ".join(c["name"] for c in chain)
        print("%-42s type=%d mode=%d range=%d..%d files=[%s]%s" % (
            r["name"], r["type"], r["mode"], r["min_range"], r["max_range"], ", ".join(durs), chain_str
        ))
        shown += 1
    print("(%d record(s) shown out of %d)" % (shown, len(records)))
    return 0


def cmd_audit(args):
    assets = pathlib.Path(args.assets_dir)
    sb_path = assets / "published" / "sounds" / "soundBase.sb"
    sub_path = assets / "published" / "data" / "subtitles.sb"
    sounds_root = assets / "published" / "sounds"
    maps_dir = assets / "published" / "maps"

    if not sb_path.is_file():
        raise ToolError("missing %s" % sb_path)
    if not sub_path.is_file():
        raise ToolError("missing %s" % sub_path)

    records = parse_sound_base(sb_path.read_bytes())
    by_name = {r["name"]: r for r in records}
    tracks = parse_subtitles_sb(sub_path.read_bytes())

    # Collect map references
    key_to_maps = {}
    if maps_dir.is_dir():
        for mp in sorted(maps_dir.glob("*.sb")):
            for s in set(parse_sbin_strings(parse_sbin_chunks(mp.read_bytes()))):
                if s.startswith("deadspace/"):
                    key_to_maps.setdefault(s, []).append(mp.name)

    # Check referenced files and orphans on disk
    referenced = set()
    missing_files = []
    for r in records:
        for f in r["files"]:
            norm = normalize_sound_rel(f)
            referenced.add(norm.lower())
            if not (sounds_root / norm).is_file():
                missing_files.append((r["name"], norm))

    orphans = []
    if sounds_root.is_dir():
        for p in sorted(sounds_root.rglob("*.sps")):
            rel = p.relative_to(sounds_root).as_posix()
            if rel.lower() not in referenced:
                orphans.append((rel, sph_duration_seconds(p)))

    issues = 0
    print("soundBase.sb: %d records | subtitles.sb: %d tracks" % (len(records), len(tracks)))

    if missing_files:
        print("\nMissing .sps files referenced by soundBase.sb (%d):" % len(missing_files))
        for k, f in missing_files:
            print("  %s -> %s" % (k, f))
            issues += 1

    print("\nSubtitle vs soundBase.sb mismatches:")
    for audio_name, steps in tracks:
        maps = key_to_maps.get(audio_name, [])
        rec = by_name.get(audio_name)
        if rec is None:
            print("  [MISSING] %-38s steps=%d maps=%s" % (audio_name, len(steps), maps))
            issues += 1
            continue
        chain = trace_chain(by_name, audio_name)
        flags = []
        if all(not c.get("files") for c in chain if not c.get("missing")):
            flags.append("NO_FILES")
        if any(c.get("missing") for c in chain):
            flags.append("BROKEN_NEXT")
        if any(c.get("type") != 2 for c in chain if not c.get("missing")):
            flags.append("TYPE!=2")
        if "cinematics" not in audio_name and len(steps) != len(chain):
            flags.append("STEPS(%d)!=CLIPS(%d)" % (len(steps), len(chain)))
        unlinked = [cand for cand in (audio_name + "a", audio_name + "_1")
                    if cand in by_name and not any(c["name"] == cand for c in chain)]
        if unlinked:
            flags.append("UNLINKED=%s" % ",".join(unlinked))
        if flags:
            print("  [%-38s] %-34s maps=%s" % (" | ".join(flags), audio_name, maps))
            issues += 1

    if orphans:
        print("\nOrphan .sps files on disk not in soundBase.sb (%d):" % len(orphans))
        for rel, dur in orphans:
            print("  %s (%.2fs)" % (rel, dur))
            issues += 1

    print("\nAudit complete: %d issue(s) found" % issues)
    return 1 if issues else 0


def cmd_export_audio(args):
    inp = pathlib.Path(args.input)
    out = pathlib.Path(args.output)
    fmt = args.format
    if fmt is None:
        fmt = "mp3" if out.suffix.lower() == ".mp3" else "wav"

    sps_files = []
    if args.key:
        sb_path, sounds_root = resolve_sound_base_and_root(args.input)
        records = parse_sound_base(sb_path.read_bytes())
        by_name = {r["name"]: r for r in records}
        if args.key not in by_name:
            raise ToolError("sound key %r not found in %s" % (args.key, sb_path))
        chain = trace_chain(by_name, args.key) if args.chain else [by_name[args.key]]
        for rec in chain:
            if rec.get("missing"):
                raise ToolError("broken next chain: %r not found in soundBase.sb" % rec["name"])
            if not rec["files"]:
                raise ToolError("record %r has no audio files" % rec["name"])
            sps_files.append(sounds_root / normalize_sound_rel(rec["files"][0]))
    else:
        if not inp.is_file():
            raise ToolError("give an .sps file, or pass --key with soundBase.sb / assets dir")
        sps_files.append(inp)

    pcm_parts = []
    channels = None
    rate = None
    for sps in sps_files:
        if not sps.is_file():
            raise ToolError("missing .sps file: %s" % sps)
        ch, rt, pcm = decode_sps_bytes(sps.read_bytes())
        if channels is None:
            channels, rate = ch, rt
        elif (ch, rt) != (channels, rate):
            raise ToolError("cannot concatenate mismatched stream formats (%dch %dHz vs %dch %dHz)" % (
                channels, rate, ch, rt))
        pcm_parts.append(pcm)

    merged = b"".join(pcm_parts)
    write_audio(out, channels, rate, merged, fmt)
    dur = (len(merged) // (2 * channels)) / float(rate) if rate else 0.0
    print("wrote %s (%s, %d ch, %d Hz, %.2fs from %d clip(s))" % (out, fmt, channels, rate, dur, len(sps_files)))
    return 0


def cmd_export_subtitles(args):
    assets = pathlib.Path(args.assets_dir)
    sub_path = assets / "published" / "data" / "subtitles.sb"
    str_path = assets / "published" / "stringdata" / args.lang / "deadspace.bin"
    sb_path = assets / "published" / "sounds" / "soundBase.sb"
    sounds_root = assets / "published" / "sounds"

    if not sub_path.is_file():
        raise ToolError("missing %s" % sub_path)
    if not str_path.is_file():
        raise ToolError("missing %s" % str_path)

    tracks = parse_subtitles_sb(sub_path.read_bytes())
    strings = parse_string_table_bin(str_path.read_bytes())
    by_name = parse_sound_base(sb_path.read_bytes()) if sb_path.is_file() else []
    by_name_map = {r["name"]: r for r in by_name}

    def track_fallback_ms(audio_name):
        rec = by_name_map.get(audio_name)
        if rec and rec["files"]:
            dur = sph_duration_seconds(sounds_root / normalize_sound_rel(rec["files"][0]))
            if dur > 0:
                return dur * 1000.0
        return 3000.0

    if args.key:
        tracks = [t for t in tracks if t[0] == args.key]
        if not tracks:
            raise ToolError("subtitle key %r not found in %s" % (args.key, sub_path))

    if args.dir:
        out_dir = pathlib.Path(args.dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        ext = ".srt" if args.format == "srt" else ".txt"
        for audio_name, steps in tracks:
            safe = audio_name.replace("/", "_").replace("\\", "_")
            fb = track_fallback_ms(audio_name)
            body = format_subtitles_srt(steps, strings, fb) if args.format == "srt" else format_subtitles_txt(audio_name, steps, strings, fb)
            (out_dir / (safe + ext)).write_text(body + ("\n" if not body.endswith("\n") else ""), encoding="utf-8")
        print("exported %d subtitle file(s) to %s" % (len(tracks), out_dir))
        return 0

    if args.format == "srt":
        if len(tracks) > 1:
            raise ToolError("SRT export needs either --key KEY (for one file) or --dir DIR (for all tracks)")
        audio_name, steps = tracks[0]
        body = format_subtitles_srt(steps, strings, track_fallback_ms(audio_name))
    else:
        body = "\n\n".join(format_subtitles_txt(an, st, strings, track_fallback_ms(an)) for an, st in tracks) + "\n"

    if args.output:
        out_p = pathlib.Path(args.output)
        out_p.parent.mkdir(parents=True, exist_ok=True)
        out_p.write_text(body, encoding="utf-8")
        print("wrote %s" % out_p)
    else:
        sys.stdout.write(body)
    return 0


def build_parser():
    ap = argparse.ArgumentParser(prog="audio_subtitles.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("dump", help="inspect soundBase.sb records and dialogue chains")
    p.add_argument("path", help="soundBase.sb file or extracted assets directory")
    p.add_argument("--filter", help="case-insensitive substring filter on sound key")
    p.add_argument("--type", type=int, help="filter by SoundObject type (0=SFX, 2=VO)")
    p.add_argument("--chains-only", action="store_true", help="only show records with a next link")

    p = sub.add_parser("audit", help="cross-check subtitles.sb, soundBase.sb, maps/*.sb, and .sps files")
    p.add_argument("assets_dir", help="extracted assets directory containing published/")

    p = sub.add_parser("export-audio", help="decode .sps (or a soundBase.sb key/chain) to WAV or MP3")
    p.add_argument("input", help=".sps file, soundBase.sb, or extracted assets directory")
    p.add_argument("--key", help="soundBase.sb key to export (instead of a direct .sps path)")
    p.add_argument("--chain", action="store_true", help="concatenate the full next chain for --key")
    p.add_argument("--format", choices=("wav", "mp3"), help="output format (defaults from -o extension, else wav)")
    p.add_argument("-o", "--output", required=True, help="output .wav or .mp3 path")

    p = sub.add_parser("export-subtitles", help="export subtitles.sb + localization strings to SRT or plain text")
    p.add_argument("assets_dir", help="extracted assets directory containing published/")
    p.add_argument("--lang", default="en-us", help="language directory under published/stringdata/ (default: en-us)")
    p.add_argument("--key", help="export only one subtitle AudioName key")
    p.add_argument("--format", choices=("srt", "txt"), default="txt", help="output format (default: txt)")
    dest = p.add_mutually_exclusive_group()
    dest.add_argument("-o", "--output", help="write output to a single file (default: stdout)")
    dest.add_argument("--dir", help="export one file per subtitle track into this directory")
    return ap


def main(argv=None):
    args = build_parser().parse_args(sys.argv[1:] if argv is None else argv)
    try:
        return {
            "dump": cmd_dump,
            "audit": cmd_audit,
            "export-audio": cmd_export_audio,
            "export-subtitles": cmd_export_subtitles,
        }[args.cmd](args)
    except ToolError as e:
        print("error: %s" % e, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
