#!/usr/bin/env python3
"""Tests for tools/sbin.py. Every database is built here from scratch: no game file, no save and no asset is used."""
import io
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import sbin  # noqa: E402

TOOL = os.path.join(ROOT, "tools", "sbin.py")
PAD = b"\xcd"  # the game leaves its alignment padding uninitialised: 0xCD


class Db:
    """Writes SBIN v2 files the way the game's own saves are laid out: DATA starts with an empty object, every
    object is aligned to 4 bytes with 0xCD fill, and an unstructured object's length excludes its last padding."""

    def __init__(self):
        self.strs, self.structs, self.fields, self.objs, self.gaps = [], [], [], [], {}

    def s(self, text):
        if text not in self.strs:
            self.strs.append(text)
        return self.strs.index(text) + 1

    def reserve(self):
        self.objs.append(None)
        return len(self.objs) - 1

    def put(self, idx, kind, body):
        self.objs[idx] = (kind, body)
        return idx

    def add(self, kind, body):
        self.objs.append((kind, body))
        return len(self.objs) - 1

    def struct_def(self, name, fields):
        first = len(self.fields)
        for fname, typ, off in fields:
            self.fields.append((self.s(fname), typ, off, 0))
        self.structs.append((self.s(name), first, len(fields)))
        return len(self.structs) - 1

    @staticmethod
    def scalar(typ, v):
        return {5: lambda: struct.pack("<i", v), 6: lambda: struct.pack("<I", v), 9: lambda: bytes([v]),
                10: lambda: struct.pack("<f", v), 15: lambda: struct.pack("<I", v), 17: lambda: struct.pack("<i", v)}[typ]()

    def dict_body(self, entries):
        """entries: (key, type, value); a string value is given as text."""
        out, pos, end = bytearray(), 4, 4
        for key, typ, v in entries:
            size = sbin.TYPE_SIZE[typ]
            val = struct.pack("<H", self.s(v)) if typ == 13 else self.scalar(typ, v)
            out += struct.pack("<4H", self.s(key), typ, pos + 8, 0) + val.ljust((size + 1) & ~1, b"\0")
            end = pos + 8 + size
            pos += 8 + ((size + 1) & ~1)
        return struct.pack("<HH", len(entries), end) + bytes(out)

    def array_body(self, et, count, data, struct_index=0):
        return struct.pack("<HHI", et, struct_index, count) + data

    def build(self, gap_after=None, dead_copy=None):
        data, offsets = bytearray(b"\x00\x00\x04\x00"), []
        for i, (kind, body) in enumerate(self.objs):
            data += PAD * (-len(data) % 4)
            offsets.append(len(data))
            data += body
            if gap_after == i:
                data += b"\x12\x34\x56\x78\x9a\xbc\xde"  # bytes no object explains
            if dead_copy is not None and dead_copy[0] == i:
                data += PAD * (-len(data) % 4) + dead_copy[1]  # an object the game abandoned when it grew
        strs = struct.pack("<I", len(self.strs)) + b"".join(struct.pack("<I", len(t)) + t.encode() for t in self.strs)
        chunks = [("STRU", b"".join(struct.pack("<HHH", *x) for x in self.structs)),
                  ("FIEL", b"".join(struct.pack("<4H", *x) for x in self.fields)),
                  ("ENUM", b""),
                  ("OHDR", b"".join(struct.pack("<I", (o << 3) | self.objs[i][0]) for i, o in enumerate(offsets))),
                  ("DATA", bytes(data)), ("STRS", strs), ("SYMB", b"\0\0\0\0")]
        out = bytearray(b"SBIN" + struct.pack("<I", 2))
        for tag, body in chunks:
            out += tag.encode() + struct.pack("<II", len(body), sbin.fnv1(body)) + body
            out += b"\0" * (-len(out) % 4)
        return bytes(out)


def save_like(credits=53400, clip=16, gap_after=None, dead_copy=None):
    """A small database shaped like a level save: nested dicts, every scalar type, arrays of floats, strings, refs
    and structs."""
    db = Db()
    item = db.struct_def("item", [("type", 13, 0), ("count", 5, 4)])
    root, player, weapons, ripper, inv, items = (db.reserve() for _ in range(6))
    transform, upgrades = db.reserve(), db.reserve()
    db.put(root, 1, db.dict_body([("player", 15, player), ("savetype", 13, "checkpoint"), ("plus", 9, 0)]))
    db.put(player, 1, db.dict_body([("transform", 15, transform), ("weapons", 15, weapons), ("hp", 10, 225.0),
                                    ("inventory", 15, inv), ("upgrades", 15, upgrades)]))
    db.put(transform, 2, db.array_body(10, 12, struct.pack("<12f", *range(12))))
    db.put(weapons, 1, db.dict_body([("ripper", 15, ripper)]))
    db.put(ripper, 1, db.dict_body([("clip", 6, clip), ("available", 9, 1), ("slot", 5, -3)]))
    db.put(inv, 1, db.dict_body([("credits", 5, credits), ("items", 15, items)]))
    elems = b"".join(struct.pack("<Hxxi", db.s(t), n) for t, n in (("AMMO_RIPPER", 15), ("AMMO_LINE_GUN", 4)))
    db.put(items, 2, db.array_body(16, 2, elems, item))
    db.put(upgrades, 2, db.array_body(13, 2, struct.pack("<2H", db.s("BTN_CAP1"), db.s("BTN_CAP2"))))
    return db.build(gap_after=gap_after, dead_copy=dead_copy)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def cli(*args, check=False):
    r = subprocess.run([sys.executable, TOOL, *map(str, args)], capture_output=True, text=True)
    if check:
        assert r.returncode == 0, r.stdout + r.stderr
    return r


class SbinTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, data):
        path = os.path.join(self.tmp.name, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    # ---- format basics -------------------------------------------------------------------------------------
    def test_fnv1_reference_values(self):
        self.assertEqual(sbin.fnv1(b""), 0x811C9DC5)
        self.assertEqual(sbin.fnv1(b"a"), 0x050C5D7E)
        self.assertEqual(sbin.fnv1(b"foobar"), 0x31F0B262)

    def test_decodes_every_scalar_type(self):
        db = sbin.SBin(save_like())
        flat = {k: n for k, n in sbin.flat(db).items()}
        self.assertEqual(flat["player.inventory.credits"].value, 53400)
        self.assertEqual(flat["player.weapons.ripper.clip"].value, 16)
        self.assertEqual(flat["player.weapons.ripper.slot"].value, -3)        # int32 is signed
        self.assertEqual(flat["player.weapons.ripper.available"].value, 1)
        self.assertEqual(flat["player.hp"].value, 225.0)
        self.assertEqual(flat["savetype"].value, "checkpoint")
        self.assertEqual(flat["player.transform[11]"].value, 11.0)
        self.assertEqual(flat["player.upgrades[1]"].value, "BTN_CAP2")
        self.assertEqual(flat["player.inventory.items[1].type"].value, "AMMO_LINE_GUN")
        self.assertEqual(flat["player.inventory.items[1].count"].value, 4)

    def test_unsigned_values_above_int_max_are_not_negative(self):
        db = sbin.SBin(save_like(clip=4000000000))
        self.assertEqual(sbin.flat(db)["player.weapons.ripper.clip"].value, 4000000000)

    def test_paths(self):
        self.assertEqual(sbin.parse_path("a.b[2].c"), ["a", "b", 2, "c"])
        self.assertEqual(sbin.parse_path('x["we.ird"].y'), ["x", "we.ird", "y"])
        self.assertEqual(sbin.parse_path('["root.key"][0].a'), ["root.key", 0, "a"])
        self.assertEqual(sbin.format_path(["a", "we.ird", 3]), 'a["we.ird"][3]')
        for bad in ("", ".a", "a.", "a..b", "items[0]count", "a.[0]"):
            with self.assertRaises(sbin.Problem, msg=bad):
                sbin.parse_path(bad)
        with self.assertRaises(sbin.Problem):
            sbin.find(sbin.build_tree(sbin.SBin(save_like())), ["player", "nope"])

    # ---- dump / diff -------------------------------------------------------------------------------------
    def test_dump_prints_a_tree_and_subtrees(self):
        path = self.write("a.sb", save_like())
        text = cli("dump", path, check=True).stdout
        self.assertIn("player:", text)
        self.assertIn("    clip = 16", text)
        self.assertIn("transform[12] = 0 1 2 3", text)
        self.assertIn("type = 'AMMO_RIPPER'", cli("dump", path, "player.inventory.items[0]", check=True).stdout)
        self.assertIn("[uint32]", cli("dump", path, "player.weapons", "--types", check=True).stdout)
        self.assertEqual(cli("dump", path, "player.nope").returncode, 2)

    def test_diff_reports_changes_and_defaults_to_the_bak(self):
        old = self.write("s.sb.bak", save_like(credits=100, clip=16))
        new = self.write("s.sb", save_like(credits=250, clip=13))
        text = cli("diff", new).stdout
        self.assertIn("~ player.inventory.credits: 100 -> 250", text)
        self.assertIn("~ player.weapons.ripper.clip: 16 -> 13", text)
        self.assertIn("2 differences", text)
        self.assertEqual(cli("diff", new, new).returncode, 0)
        self.assertEqual(cli("diff", old, new).returncode, 1)
        os.remove(old)
        self.assertEqual(cli("diff", new).returncode, 2)

    # ---- verify ----------------------------------------------------------------------------------------------
    def test_a_good_file_verifies_clean(self):
        path = self.write("a.sb", save_like())
        r = cli("verify", path, "--strict")
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("0 error(s), 0 warning(s)", r.stdout)

    def test_padding_placeholder_and_abandoned_copies_are_explained(self):
        body = Db().dict_body([("player", 15, 1), ("plus", 9, 0)])  # any valid kind-1 object can be the abandoned copy
        path = self.write("a.sb", save_like(dead_copy=(3, body)))
        r = cli("verify", path, "--coverage", "--strict")
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("placeholder for object offset 0", r.stdout)
        self.assertIn("alignment padding", r.stdout)
        self.assertIn("abandoned copy of an object", r.stdout)

    def test_unexplained_bytes_are_flagged(self):
        r = cli("verify", self.write("a.sb", save_like(gap_after=2)))
        self.assertEqual(r.returncode, 0)
        self.assertIn("not explained by any object", r.stdout)
        self.assertEqual(cli("verify", self.write("b.sb", save_like(gap_after=2)), "--strict").returncode, 1)

    def test_corruption_is_caught(self):
        good = save_like()
        d_off = sbin.SBin(good).c["DATA"][0]
        flipped = bytearray(good)
        flipped[d_off + 20] ^= 0xFF
        self.assertIn("hash", cli("verify", self.write("flip.sb", bytes(flipped))).stdout)
        self.assertEqual(cli("verify", self.write("cut.sb", good[:len(good) // 2])).returncode, 1)
        self.assertEqual(cli("verify", self.write("junk.sb", b"hello world, not a database")).returncode, 1)
        self.assertEqual(cli("verify", os.path.join(self.tmp.name, "missing.sb")).returncode, 1)

    def test_structural_errors_are_caught_even_when_the_hash_is_fixed(self):
        good = save_like()
        db = sbin.SBin(good)
        d_off, d_size, _ = db.c["DATA"]

        def patched(offset, data):
            b = bytearray(good)
            b[d_off + offset:d_off + offset + len(data)] = data
            struct.pack_into("<I", b, d_off - 4, sbin.fnv1(bytes(b[d_off:d_off + d_size])))
            return bytes(b)

        root = db.obj(0)[1] - d_off
        bad_length = patched(root + 2, struct.pack("<H", 99))  # the length field must match the entries
        self.assertIn("length field 99", cli("verify", self.write("len.sb", bad_length)).stdout)
        first_entry = root + 4
        unknown_type = patched(first_entry + 2, struct.pack("<H", 14))
        self.assertIn("unknown type 14", cli("verify", self.write("type.sb", unknown_type)).stdout)
        dangling = patched(first_entry + 8, struct.pack("<I", 4000))  # player -> object 4000
        self.assertIn("missing object 4000", cli("verify", self.write("ref.sb", dangling)).stdout)
        bad_string = patched(root + 4 + 12 + 8, struct.pack("<H", 500))  # savetype -> string 500
        self.assertIn("string index 500", cli("verify", self.write("str.sb", bad_string)).stdout)

    def test_verify_handles_null_array_refs_and_struct_inner_fields(self):
        # Signed -1 array reference (type 17) is valid and must verify clean.
        db = Db()
        item = db.struct_def("item", [("type", 13, 0), ("extra_arr", 17, 4)])
        root, items = db.reserve(), db.reserve()
        db.put(root, 1, db.dict_body([("null_arr", 17, -1), ("items", 15, items)]))
        db.put(items, 2, db.array_body(16, 1, struct.pack("<Hxxi", db.s("AMMO"), -1), item))
        good = db.build()
        r = cli("verify", self.write("nullref.sb", good), "--strict")
        self.assertEqual(r.returncode, 0, r.stdout)

        # Corrupt the string index inside the struct array element and re-hash DATA.
        parsed = sbin.SBin(good)
        d_off, d_size, _ = parsed.c["DATA"]
        _, _, _, dp, _ = sbin.array_info(parsed, items)
        bad = bytearray(good)
        struct.pack_into("<H", bad, dp, 999)
        struct.pack_into("<I", bad, d_off - 4, sbin.fnv1(bytes(bad[d_off:d_off + d_size])))
        r_bad = cli("verify", self.write("badstructstr.sb", bytes(bad)))
        self.assertEqual(r_bad.returncode, 1)
        self.assertIn("string index 999", r_bad.stdout)

        # Corrupt the array ref inside the struct array element to a positive out-of-range index.
        bad_ref = bytearray(good)
        struct.pack_into("<i", bad_ref, dp + 4, 42)
        struct.pack_into("<I", bad_ref, d_off - 4, sbin.fnv1(bytes(bad_ref[d_off:d_off + d_size])))
        r_ref = cli("verify", self.write("badstructref.sb", bytes(bad_ref)))
        self.assertEqual(r_ref.returncode, 1)
        self.assertIn("missing object 42", r_ref.stdout)

    def test_diff_detects_small_float_changes_and_empty_containers(self):
        def make_float_db(val, with_empty=False):
            db = Db()
            root = db.reserve()
            entries = [("hp", 10, val)]
            if with_empty:
                empty_d, empty_a = db.reserve(), db.reserve()
                db.put(empty_d, 1, db.dict_body([]))
                db.put(empty_a, 2, db.array_body(5, 0, b""))
                entries.extend([("actors", 15, empty_d), ("triggers", 15, empty_a)])
            db.put(root, 1, db.dict_body(entries))
            return db.build()

        # Differ past 6 significant digits (would both format as '100000' with %g)
        a = self.write("f1.sb", make_float_db(100000.125, with_empty=False))
        b = self.write("f2.sb", make_float_db(100000.25, with_empty=True))
        r = cli("diff", a, b)
        self.assertEqual(r.returncode, 1)
        self.assertIn("~ hp: 100000.125 -> 100000.25", r.stdout)
        self.assertIn("+ actors = {}", r.stdout)
        self.assertIn("+ triggers = []", r.stdout)
        self.assertIn("3 differences", r.stdout)

    def test_diff_directories(self):
        d1 = os.path.join(self.tmp.name, "before")
        d2 = os.path.join(self.tmp.name, "after")
        os.makedirs(d1)
        os.makedirs(d2)
        for p, data in (
            (os.path.join(d1, "same.sb"), save_like(credits=10)),
            (os.path.join(d2, "same.sb"), save_like(credits=10)),
            (os.path.join(d1, "changed.sb"), save_like(credits=10)),
            (os.path.join(d2, "changed.sb"), save_like(credits=20)),
            (os.path.join(d1, "only_a.sb"), save_like(credits=1)),
            (os.path.join(d2, "only_b.sb"), save_like(credits=2)),
        ):
            with open(p, "wb") as f:
                f.write(data)

        r = cli("diff", d1, d2)
        self.assertEqual(r.returncode, 1)
        self.assertIn("- only in %s: only_a.sb" % d1, r.stdout)
        self.assertIn("+ only in %s: only_b.sb" % d2, r.stdout)
        self.assertIn("~ player.inventory.credits: 10 -> 20", r.stdout)
        self.assertNotIn("same.sb", r.stdout)

        # Single-dir diff compares *.sb against *.sb.bak siblings.
        bak_dir = os.path.join(self.tmp.name, "live")
        os.makedirs(bak_dir)
        with open(os.path.join(bak_dir, "00_LevelSave.sb.bak"), "wb") as f:
            f.write(save_like(credits=50))
        with open(os.path.join(bak_dir, "00_LevelSave.sb"), "wb") as f:
            f.write(save_like(credits=75))
        r_bak = cli("diff", bak_dir)
        self.assertEqual(r_bak.returncode, 1)
        self.assertIn("~ player.inventory.credits: 50 -> 75", r_bak.stdout)
        self.assertEqual(cli("diff", d1, os.path.join(d2, "same.sb")).returncode, 2)

    # ---- set -------------------------------------------------------------------------------------------------
    def test_set_changes_values_fixes_the_hash_and_keeps_everything_else(self):
        src = self.write("a.sb", save_like())
        out = os.path.join(self.tmp.name, "b.sb")
        r = cli("set", src, "player.inventory.credits=99999", "player.weapons.ripper.clip=0x10", "player.hp=300.5",
                "savetype=checkpoint", "plus=true", "player.weapons.ripper.slot=-7", "-o", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertEqual(cli("verify", out, "--strict").returncode, 0)
        flat = sbin.flat(sbin.SBin(read(out)))
        self.assertEqual(flat["player.inventory.credits"].value, 99999)
        self.assertEqual(flat["player.weapons.ripper.clip"].value, 16)
        self.assertEqual(flat["player.hp"].value, 300.5)
        self.assertEqual(flat["plus"].value, 1)
        self.assertEqual(flat["player.weapons.ripper.slot"].value, -7)
        diff = cli("diff", src, out).stdout
        self.assertIn("4 differences", diff)  # credits, hp, plus, slot; clip (0x10 = 16) and savetype are unchanged
        self.assertEqual(len(read(src)), len(read(out)))

    def test_set_refuses_bad_values_and_writes_nothing(self):
        src = self.write("a.sb", save_like())
        before = read(src)
        for bad in ("player.weapons.ripper.clip=-1", "player.weapons.ripper.clip=4294967296",
                    "player.inventory.credits=2147483648", "plus=256", "plus=2", "plus=-1",
                    "savetype=never_seen_before", "player.hp=abc", "player.hp=nan", "player.hp=inf",
                    "player.weapons=1", "player.nope=1", "player.transform=1"):
            r = cli("set", src, bad, "--in-place")
            self.assertEqual(r.returncode, 2, bad + r.stdout)
            self.assertEqual(read(src), before)
        self.assertFalse([n for n in os.listdir(self.tmp.name) if ".preedit" in n])
        # -o and --in-place are mutually exclusive.
        r_conflict = cli("set", src, "player.inventory.credits=1", "-o", src + ".out", "--in-place")
        self.assertNotEqual(r_conflict.returncode, 0)
        self.assertEqual(read(src), before)

    def test_set_repeated_assignment_tracks_intermediate_value(self):
        src = self.write("a.sb", save_like(credits=100))
        out = os.path.join(self.tmp.name, "b.sb")
        r = cli("set", src, "player.inventory.credits=200", "player.inventory.credits=300", "-o", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("player.inventory.credits: 100 -> 200", r.stdout)
        self.assertIn("player.inventory.credits: 200 -> 300", r.stdout)
        self.assertEqual(sbin.flat(sbin.SBin(read(out)))["player.inventory.credits"].value, 300)

    def test_clean_errors_on_missing_or_corrupt_files(self):
        missing = os.path.join(self.tmp.name, "does_not_exist.sb")
        junk = self.write("junk.sb", b"not an sbin")
        for argv in (("dump", missing), ("dump", junk), ("diff", junk, junk), ("set", missing, "a=1", "--dry-run")):
            r = cli(*argv)
            self.assertEqual(r.returncode, 2, argv)
            self.assertIn("error:", r.stderr)
            self.assertNotIn("Traceback", r.stderr)

    def test_set_in_place_keeps_the_original(self):
        src = self.write("a.sb", save_like())
        before = read(src)
        self.assertEqual(cli("set", src, "player.inventory.credits=1", "--in-place").returncode, 0)
        self.assertEqual(read(src + ".preedit"), before)
        self.assertEqual(cli("set", src, "player.inventory.credits=2", "--in-place").returncode, 0)
        self.assertNotEqual(read(src + ".preedit1"), before)  # the second original is kept too
        self.assertEqual(cli("set", src, "player.inventory.credits=3").returncode, 2)  # neither -o nor --in-place
        self.assertEqual(cli("set", src, "player.inventory.credits=3", "--dry-run").returncode, 0)

    # ---- census --------------------------------------------------------------------------------------------
    def test_census_groups_by_schema_path_and_supports_by_leaf(self):
        def with_actors_and_objectives(credits):
            db = Db()
            root, inv, actors, a1, objectives, o1 = (db.reserve() for _ in range(6))
            db.put(root, 1, db.dict_body([("inventory", 15, inv), ("actors", 15, actors), ("objectives", 15, objectives)]))
            db.put(inv, 1, db.dict_body([("credits", 5, credits)]))
            db.put(actors, 1, db.dict_body([("door_01", 15, a1)]))
            db.put(a1, 1, db.dict_body([("state", 5, 7)]))
            db.put(objectives, 1, db.dict_body([("obj_01", 15, o1)]))
            db.put(o1, 1, db.dict_body([("state", 5, 2)]))
            return db.build()

        self.write("a.sb", with_actors_and_objectives(100))
        self.write("b.sb", with_actors_and_objectives(900))
        out = cli("census", self.tmp.name, check=True).stdout
        self.assertIn("2 file(s)", out)
        credits = [l for l in out.splitlines() if l.startswith("inventory.credits")][0]
        self.assertIn("int32", credits)
        self.assertIn("100..900", credits)
        actor_state = [l for l in out.splitlines() if l.startswith("actors.*.state")][0]
        obj_state = [l for l in out.splitlines() if l.startswith("objectives.*.state")][0]
        self.assertIn("7..7", actor_state)
        self.assertIn("2..2", obj_state)

        leaf_out = cli("census", "--by-leaf", self.tmp.name, check=True).stdout
        merged_state = [l for l in leaf_out.splitlines() if l.startswith("state ")][0]
        self.assertIn("2..7", merged_state)

    # ---- the original behaviour ------------------------------------------------------------------------------
    def test_layout_listing_still_works(self):
        db = Db()
        entity = db.struct_def("Entity", [("entityName", 13, 0), ("type", 13, 2), ("stringId", 13, 4), ("layout", 13, 6),
                                          ("image", 13, 8), ("width", 10, 12), ("height", 10, 16), ("flags", 5, 20),
                                          ("isVisible", 9, 24), ("vertices", 17, 28)])
        layout = db.struct_def("Layout", [("layoutId", 13, 0), ("entities", 17, 4)])
        arr = db.reserve()
        obj = db.add(0, struct.pack("<H", layout) + struct.pack("<Hxxi", db.s("MAIN_MENU"), 1))
        ent = struct.pack("<5H", db.s("btn"), db.s("button"), db.s("STR_PLAY"), db.s("MAIN_MENU"), db.s("play.png"))
        ent += b"\0\0" + struct.pack("<2fiBxxx", 100.0, 20.0, 7, 1) + struct.pack("<i", -1)
        db.put(arr, 2, db.array_body(16, 1, ent, entity))
        path = self.write("layouts.xml.sb", db.build())
        r = cli(path)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("MAIN_MENU", r.stdout)


if __name__ == "__main__":
    unittest.main()
