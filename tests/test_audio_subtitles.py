#!/usr/bin/env python3
"""Synthetic tests for tools/audio_subtitles.py (no game assets or dialogue text used)."""
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import audio_subtitles as mod  # noqa: E402

TOOL = os.path.join(ROOT, "tools", "audio_subtitles.py")


def fnv1(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h = ((h * 0x01000193) & 0xFFFFFFFF) ^ b
    return h


def build_sbin(structs, fields, strs, ohdr, data) -> bytes:
    strs_buf = bytearray(struct.pack("<I", len(strs)))
    for s in strs:
        b = s.encode("utf-8")
        strs_buf += struct.pack("<I", len(b)) + b
    chunks = [
        ("ENUM", b""),
        ("STRU", b"".join(struct.pack("<3H", *x) for x in structs)),
        ("FIEL", b"".join(struct.pack("<4H", *x) for x in fields)),
        ("OHDR", b"".join(struct.pack("<I", h) for h in ohdr)),
        ("DATA", data),
        ("STRS", bytes(strs_buf)),
        ("SYMB", b"\x00\x00\x00\x00"),
    ]
    out = bytearray(b"SBIN" + struct.pack("<I", 2))
    for tag, body in chunks:
        out += tag.encode("ascii") + struct.pack("<II", len(body), fnv1(body)) + body
        out += b"\x00" * (-len(out) % 4)
    return bytes(out)


def make_synthetic_xas1_sps(sample_rate=22050, channels=1, num_frames=2, dc_value=1024):
    """Build a valid EA XAS1 (codec 0x14) .sps and matching 8-byte .sph header."""
    num_samples = num_frames * 128
    # 4 subblock headers: coef=0 (0.0, 0.0), hist1 = dc_value & ~0xF, hist0 = dc_value & ~0xF, shift = 12
    h0 = (dc_value & ~0xF) | 0
    h1 = (dc_value & ~0xF) | 12
    sub_hdr = struct.pack("<hh", h0, h1) * 4
    nibbles = bytes(60)  # all zero deltas
    one_block = sub_hdr + nibbles
    assert len(one_block) == 76

    ch_byte = (channels - 1) << 2
    sph = bytes([0x14, ch_byte, (sample_rate >> 8) & 0xFF, sample_rate & 0xFF]) + struct.pack(">I", 0x40000000 | num_samples)

    hdr_blk = bytes([0x48, 0x00, 0x00, 12]) + sph
    data_payload = struct.pack(">I", num_samples) + one_block * (num_frames * channels)
    dsz = 4 + len(data_payload)
    data_blk = bytes([0x44, (dsz >> 16) & 0xFF, (dsz >> 8) & 0xFF, dsz & 0xFF]) + data_payload
    end_blk = bytes([0x45, 0x00, 0x00, 4])
    return hdr_blk + data_blk + end_blk, sph


def make_synthetic_subtitles_sb(tracks):
    """tracks: [(audio_name, [(step_id, timeout_ms), ...]), ...]"""
    strs = ["Step", "ID", "timeout", "Subtitle", "AudioName", "Steps", "Subtitles"]

    def sidx(text):
        if text not in strs:
            strs.append(text)
        return strs.index(text) + 1

    # Struct 0: Step(ID:13@0, timeout:10@4); Struct 1: Subtitle(AudioName:13@0, Steps:17@4)
    structs = [(1, 0, 2), (4, 2, 2)]
    fields = [(2, 13, 0, 0), (3, 10, 4, 0), (5, 13, 0, 0), (6, 17, 4, 0)]

    data = bytearray(b"\x00\x00\x04\x00")
    ohdr = [0, 0]  # 0 = root placeholder, 1 = Subtitles array
    # Reserve root dict at offset 4 (trivial empty)
    ohdr[0] = (4 << 3) | 1
    data += struct.pack("<HH", 0, 4)

    # First reserve step arrays starting at object 2
    step_obj_indices = []
    sub_array_off = len(data)
    ohdr[1] = (sub_array_off << 3) | 2
    # Placeholder for Subtitles array header + entries
    data += struct.pack("<HHI", 16, 1, len(tracks))
    sub_entries_pos = len(data)
    data += b"\x00" * (8 * len(tracks))

    for i, (audio_name, steps) in enumerate(tracks):
        while len(data) & 3:
            data.append(0)
        obj_idx = len(ohdr)
        ohdr.append((len(data) << 3) | 2)
        data += struct.pack("<HHI", 16, 0, len(steps))
        for sid, timeout in steps:
            data += struct.pack("<HHf", sidx(sid), 0, float(timeout))
        struct.pack_into("<HHI", data, sub_entries_pos + i * 8, sidx(audio_name), 0, obj_idx)
        step_obj_indices.append(obj_idx)

    return build_sbin(structs, fields, strs, ohdr, bytes(data))


def make_synthetic_string_table_bin(pairs):
    """pairs: [(string_id, text), ...]"""
    strs = ["StringPair", "stringid", "string", "halversion"]

    def sidx(text):
        if text not in strs:
            strs.append(text)
        return strs.index(text) + 1

    structs = [(1, 0, 3)]
    fields = [(2, 13, 0, 0), (3, 13, 2, 0), (4, 5, 4, 0)]
    data = bytearray(b"\x00\x00\x04\x00")
    ohdr = [(4 << 3) | 1]
    data += struct.pack("<HH", 0, 4)
    arr_off = len(data)
    ohdr.append((arr_off << 3) | 2)
    data += struct.pack("<HHI", 16, 0, len(pairs))
    for sid, txt in pairs:
        data += struct.pack("<HHi", sidx(sid), sidx(txt), 0)
    return build_sbin(structs, fields, strs, ohdr, bytes(data))


def cli(*args):
    return subprocess.run([sys.executable, TOOL, *map(str, args)], capture_output=True, text=True)


class AudioSubtitlesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.assets = pathlib.Path(self.tmp.name) / "assets"
        (self.assets / "published/sounds/sfx/vo").mkdir(parents=True)
        (self.assets / "published/data").mkdir(parents=True)
        (self.assets / "published/stringdata/en-us").mkdir(parents=True)
        (self.assets / "published/maps").mkdir(parents=True)

        sps1, sph1 = make_synthetic_xas1_sps(22050, 1, num_frames=4, dc_value=512)
        sps2, sph2 = make_synthetic_xas1_sps(22050, 1, num_frames=2, dc_value=256)
        sps_orphan, sph_orphan = make_synthetic_xas1_sps(22050, 1, num_frames=2, dc_value=128)
        for stem, sps, sph in (("line_01", sps1, sph1), ("line_02", sps2, sph2), ("orphan_01", sps_orphan, sph_orphan)):
            (self.assets / "published/sounds/sfx/vo" / (stem + ".sps")).write_bytes(sps)
            (self.assets / "published/sounds/sfx/vo" / (stem + ".sph")).write_bytes(sph)

        records = [
            {"name": "deadspace/test/chained", "next": "deadspace/test/chained_a",
             "type": 2, "mode": 1, "min_range": 5, "max_range": 30, "files": ["../sfx/vo/line_01.sps"]},
            {"name": "deadspace/test/chained_a", "next": "",
             "type": 2, "mode": 1, "min_range": 5, "max_range": 30, "files": ["../sfx/vo/line_02.sps"]},
            {"name": "deadspace/test/broken", "next": "",
             "type": 2, "mode": 1, "min_range": 5, "max_range": 30, "files": ["../sfx/vo/line_01.sps"]},
            {"name": "deadspace/test/brokena", "next": "",
             "type": 2, "mode": 1, "min_range": 5, "max_range": 30, "files": ["../sfx/vo/line_02.sps"]},
        ]
        (self.assets / "published/sounds/soundBase.sb").write_bytes(mod.serialize_sound_base(records))

        sub_bytes = make_synthetic_subtitles_sb([
            ("deadspace/test/chained", [("LINE_1", 1500.0), ("LINE_2", 2000.0)]),
            ("deadspace/test/broken", [("LINE_1", 1500.0), ("LINE_2", 2000.0)]),
            ("deadspace/test/missing", [("LINE_1", -1.0)]),
        ])
        (self.assets / "published/data/subtitles.sb").write_bytes(sub_bytes)

        str_bytes = make_synthetic_string_table_bin([
            ("LINE_1", "First synthetic line."),
            ("LINE_2", "Second synthetic line."),
        ])
        (self.assets / "published/stringdata/en-us/deadspace.bin").write_bytes(str_bytes)

    def test_sound_base_round_trip(self):
        raw = (self.assets / "published/sounds/soundBase.sb").read_bytes()
        recs = mod.parse_sound_base(raw)
        self.assertEqual(len(recs), 4)
        self.assertEqual(mod.serialize_sound_base(recs), raw)

    def test_xas1_mono_and_stereo_decode(self):
        sps_mono, _ = make_synthetic_xas1_sps(22050, channels=1, num_frames=3, dc_value=512)
        ch, rate, pcm = mod.decode_sps_bytes(sps_mono)
        self.assertEqual((ch, rate, len(pcm)), (1, 22050, 3 * 128 * 2))

        sps_stereo, _ = make_synthetic_xas1_sps(22050, channels=2, num_frames=2, dc_value=256)
        ch2, rate2, pcm2 = mod.decode_sps_bytes(sps_stereo)
        self.assertEqual((ch2, rate2, len(pcm2)), (2, 22050, 2 * 128 * 2 * 2))

    def test_export_audio_single_and_chain_wav(self):
        out_single = pathlib.Path(self.tmp.name) / "single.wav"
        r1 = cli("export-audio", self.assets, "--key", "deadspace/test/chained", "-o", out_single)
        self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)
        with wave.open(str(out_single), "rb") as wf:
            self.assertEqual((wf.getnchannels(), wf.getframerate(), wf.getnframes()), (1, 22050, 4 * 128))

        out_chain = pathlib.Path(self.tmp.name) / "chain.wav"
        r2 = cli("export-audio", self.assets, "--key", "deadspace/test/chained", "--chain", "-o", out_chain)
        self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
        with wave.open(str(out_chain), "rb") as wf:
            self.assertEqual((wf.getnchannels(), wf.getframerate(), wf.getnframes()), (1, 22050, 6 * 128))

    def test_export_subtitles_srt_and_txt(self):
        r_srt = cli("export-subtitles", self.assets, "--key", "deadspace/test/chained", "--format", "srt")
        self.assertEqual(r_srt.returncode, 0, r_srt.stderr)
        self.assertIn("00:00:00,000 --> 00:00:01,500", r_srt.stdout)
        self.assertIn("First synthetic line.", r_srt.stdout)
        self.assertIn("00:00:01,500 --> 00:00:03,500", r_srt.stdout)
        self.assertIn("Second synthetic line.", r_srt.stdout)

        out_dir = pathlib.Path(self.tmp.name) / "subs_out"
        r_dir = cli("export-subtitles", self.assets, "--format", "srt", "--dir", out_dir)
        self.assertEqual(r_dir.returncode, 0, r_dir.stderr)
        self.assertTrue((out_dir / "deadspace_test_chained.srt").is_file())

        r_txt = cli("export-subtitles", self.assets, "--format", "txt")
        self.assertEqual(r_txt.returncode, 0, r_txt.stderr)
        self.assertIn("[deadspace/test/chained]", r_txt.stdout)

    def test_dump_and_audit_detect_issues(self):
        r_dump = cli("dump", self.assets, "--chains-only")
        self.assertEqual(r_dump.returncode, 0, r_dump.stderr)
        self.assertIn("deadspace/test/chained", r_dump.stdout)
        self.assertIn("chain=deadspace/test/chained -> deadspace/test/chained_a", r_dump.stdout)

        r_audit = cli("audit", self.assets)
        self.assertEqual(r_audit.returncode, 1)
        self.assertIn("[MISSING]", r_audit.stdout)
        self.assertIn("deadspace/test/missing", r_audit.stdout)
        self.assertIn("UNLINKED=deadspace/test/brokena", r_audit.stdout)
        self.assertIn("sfx/vo/orphan_01.sps", r_audit.stdout)


if __name__ == "__main__":
    unittest.main()
