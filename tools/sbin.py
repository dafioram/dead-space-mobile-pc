#!/usr/bin/env python3
"""Reader, checker and small editor for EA IronMonkey "SBIN" binary databases: layouts.xml.sb, stringdata and the
game's own save files (settings.sb, AchievementsSave.sb, NN_CheckpointSave.sb, NN_LevelSave.sb).

usage:
  sbin.py FILE [LAYOUT_ID]                list the layouts of layouts.xml.sb (the original behaviour)
  sbin.py dump FILE [PATH] [--types] [--ids]   print the decoded tree (or one subtree)
  sbin.py diff A [B] [--types]            semantic diff of files or dirs (default: .bak against current)
  sbin.py verify FILE... [--coverage] [--strict]   check hashes, lengths, references, types and unexplained bytes
  sbin.py census FILE_OR_DIR... [--by-leaf]    every schema path (or leaf name) with its types and value ranges
  sbin.py set FILE PATH=VALUE... (-o OUT | --in-place | --dry-run)   change scalar values in place

PATH looks like  player.weapons.ripper.clip  or  inventory.items[2].count ; a key with odd characters is written
["like.this"]. `set` only changes values that keep their size (ints, floats, bools and strings that already exist in
the file), then fixes the DATA chunk hash. It never adds or removes anything.

Format (from im::serialization::Database in the game; the type names are noted as sure or inferred below):
  "SBIN" u32 version (2), then chunks: tag(4) size(4) hash(4) data, 4-byte aligned. The hash is FNV-1 (32 bit:
  multiply by 0x01000193, then xor the byte) over the `size` data bytes only. There is no whole-file checksum.
  STRU  6-byte struct defs  (u16 name+1, u16 first field, u16 field count)
  FIEL  8-byte field defs   (u16 name+1, u16 type, u16 offset, u16 extra)
  OHDR  u32 per object: low 3 bits kind (0 struct, 1 root/unstructured, 2 array), >>3 = DATA offset
  DATA  object bodies, after 4 bytes (00 00 04 00) that are the same in every file seen so far.
        kind 0: u16 struct index, data.  kind 2: u16 elem type, u16 struct, u32 count, data.
        kind 1 (the save data): u16 entry count, u16 byte length of the whole object, then per entry a field definition
        stored inline - u16 key+1, u16 type, u16 offset of its value from the object start, u16 extra (the same four
        fields as FIEL) - followed by the value, padded to an even size.
  STRS  u32 count, then (u32 len, bytes) strings; string fields store index+1 (0 = none)
  ENUM, SYMB  present but empty in everything seen so far
Value types. Sure: 5 int32, 6 uint32 (the game has TypeConversion::write<unsigned> and write/read<int>), 9 bool or int8
(its `signed char`), 10 float, 13 string (u16 index+1), 15 object reference (u32 object index), 16 struct elements in
arrays, 17 array reference. Inferred only from TYPE_SIZE (1..4 and 7/8 look like signed/unsigned pairs of 1, 2 and 8
bytes; 11 double; 12 u16 enum): shown as raw hex, never decoded.
"""
import argparse
import math
import os
import re
import struct
import sys

TYPE_SIZE = {1: 1, 2: 1, 3: 2, 4: 2, 5: 4, 6: 4, 7: 8, 8: 8, 9: 1, 10: 4, 11: 8, 12: 2, 13: 2, 15: 4, 17: 4,
             18: 4, 19: 4, 20: 2}
HDR_SIZE = {0: 2, 1: 4, 2: 8}

# Value types this tool decodes (see the module docstring). Anything else is shown as raw hex with a "?" mark.
TYPE_NAME = {5: "int32", 6: "uint32", 9: "bool", 10: "float", 13: "string", 15: "object", 16: "struct", 17: "array"}
ARRAY_ELEM_SIZE = {5: 4, 6: 4, 9: 1, 10: 4, 13: 2, 15: 4, 17: 4}
KIND_NAME = {0: "struct", 1: "unstructured", 2: "array"}
REQUIRED_CHUNKS = ("STRU", "FIEL", "OHDR", "DATA", "STRS")
KNOWN_CHUNKS = REQUIRED_CHUNKS + ("ENUM", "SYMB")
FNV_BASIS, FNV_PRIME = 0x811C9DC5, 0x01000193


def fnv1(data):
    """The 32-bit FNV-1 hash the game stores for every chunk (multiply, then xor)."""
    h = FNV_BASIS
    for b in data:
        h = ((h * FNV_PRIME) & 0xFFFFFFFF) ^ b
    return h


class SBin:
    def __init__(self, data):
        self.d = data
        self.c = {}
        o = 8
        while o + 12 <= len(data):
            tag = data[o:o + 4].decode()
            size, h = struct.unpack_from("<II", data, o + 4)
            self.c[tag] = (o + 12, size, h)
            o = (o + 12 + size + 3) & ~3
        off, size, _ = self.c["STRS"]
        n = struct.unpack_from("<I", data, off)[0]
        p = off + 4
        self.strs = []
        for _ in range(n):
            ln = struct.unpack_from("<I", data, p)[0]
            self.strs.append(data[p + 4:p + 4 + ln].decode("utf-8", "replace"))
            p += 4 + ln
        off, size, _ = self.c["STRU"]
        self.structs = [struct.unpack_from("<3H", data, off + 6 * i) for i in range(size // 6)]
        off, size, _ = self.c["FIEL"]
        self.fields = [struct.unpack_from("<4H", data, off + 8 * i) for i in range(size // 8)]
        off, size, _ = self.c["OHDR"]
        self.ohdr = list(struct.unpack_from("<%dI" % (size // 4), data, off))
        self.doff = self.c["DATA"][0]

    def s(self, idx1):
        return self.strs[idx1 - 1] if 0 < idx1 <= len(self.strs) else None

    def struct_name(self, i):
        return self.s(self.structs[i][0])

    def struct_fields(self, i):
        _, first, count = self.structs[i]
        return [(self.s(self.fields[f][0]), self.fields[f][1], self.fields[f][2]) for f in range(first, first + count)]

    def obj(self, idx):
        h = self.ohdr[idx]
        return h & 7, self.doff + (h >> 3)

    def read_struct(self, si, p):
        out = {}
        for name, t, off in self.struct_fields(si):
            q = p + off
            if t == 13:
                out[name] = self.s(struct.unpack_from("<H", self.d, q)[0])
            elif t == 10:
                out[name] = struct.unpack_from("<f", self.d, q)[0]
            elif t == 5:
                out[name] = struct.unpack_from("<i", self.d, q)[0]
            elif t == 9:
                out[name] = self.d[q]
            elif t == 17:
                out[name] = ("array", struct.unpack_from("<i", self.d, q)[0])
            else:
                out[name] = ("t%d" % t, self.d[q:q + TYPE_SIZE.get(t, 4)].hex())
        return out

    def struct_size(self, si):
        return max((off + TYPE_SIZE.get(t, 4) for _, t, off in self.struct_fields(si)), default=0)

    def read_array(self, idx):
        kind, p = self.obj(idx)
        assert kind == 2, kind
        et, si, n = struct.unpack_from("<HHI", self.d, p)
        p += 8
        if et == 16:
            sz = (self.struct_size(si) + 3) & ~3
            return [self.read_struct(si, p + i * sz) for i in range(n)]
        if et == 13:
            return [self.s(v) for v in struct.unpack_from("<%dH" % n, self.d, p)]
        return ("elem type %d" % et, n)

    def read_object(self, idx):
        kind, p = self.obj(idx)
        if kind == 0:
            si = struct.unpack_from("<H", self.d, p)[0]
            return self.struct_name(si), self.read_struct(si, p + 2)
        if kind == 2:
            return "array", self.read_array(idx)
        return "kind%d" % kind, None


def layouts_main(argv):
    """sbin.py FILE [LAYOUT_ID]: list the layouts of layouts.xml.sb (the original behaviour)."""
    with open(argv[0], "rb") as f:
        db = SBin(f.read())
    want = argv[1] if len(argv) > 1 else None
    for i in range(len(db.ohdr)):
        kind, _ = db.obj(i)
        if kind != 0:
            continue
        name, o = db.read_object(i)
        if name != "Layout" or (want and o["layoutId"] != want):
            continue
        print("Layout #%d %s" % (i, o["layoutId"]))
        if want:
            for e in db.read_array(o["entities"][1]):
                verts = db.read_array(e["vertices"][1]) if e["vertices"][1] >= 0 else []
                vs = " ".join("(%g,%g)" % (v["x"], v["y"]) for v in verts)
                print("  %-28s type=%-14s str=%-22s layout=%-22s img=%s w=%g h=%g flags=%d vis=%d %s" % (
                    e["entityName"], e["type"], e["stringId"], e["layout"], e["image"], e["width"], e["height"],
                    e["flags"], e["isVisible"], vs))


# ---------------------------------------------------------------------------------------------------------------
# Decoding of the unstructured (kind 1) objects and of whole files into a tree
# ---------------------------------------------------------------------------------------------------------------

class Problem(Exception):
    pass


class Node:
    """One decoded value. kind: 'dict' (items = [(key, Node)]), 'array' (items = [(index, Node)]) or 'leaf'."""
    __slots__ = ("kind", "items", "typ", "value", "pos", "size", "obj", "elem_type", "known")

    def __init__(self, kind, **kw):
        self.kind = kind
        self.items = []
        self.typ = None
        self.value = None
        self.pos = None
        self.size = 0
        self.obj = None
        self.elem_type = None
        self.known = True
        for k, v in kw.items():
            setattr(self, k, v)


def read_scalar(db, typ, pos):
    """(value, known) of one scalar at an absolute position."""
    d = db.d
    if typ == 5:
        return struct.unpack_from("<i", d, pos)[0], True
    if typ == 6:
        return struct.unpack_from("<I", d, pos)[0], True
    if typ == 9:
        return d[pos], True
    if typ == 10:
        return struct.unpack_from("<f", d, pos)[0], True
    if typ == 13:
        return db.s(struct.unpack_from("<H", d, pos)[0]), True
    return d[pos:pos + TYPE_SIZE.get(typ, 4)].hex(), False


def unstructured_at(db, p, label="object"):
    """Decode a kind-1 object at absolute position `p`: (length_field, [entry dicts], [problems]). Never raises."""
    d, end = db.d, db.c["DATA"][0] + db.c["DATA"][1]
    problems, entries = [], []
    if p + 4 > end:
        return 0, entries, ["%s: header outside DATA" % label]
    count, length = struct.unpack_from("<HH", d, p)
    q = p + 4
    for i in range(count):
        if q + 8 > end:
            problems.append("%s entry %d: header outside DATA" % (label, i))
            break
        key, typ, off, extra = struct.unpack_from("<4H", d, q)
        vp = p + off
        size = TYPE_SIZE.get(typ)
        e = {"key_idx": key, "key": db.s(key), "type": typ, "offset": off, "extra": extra, "pos": vp, "size": size,
             "entry_pos": q}
        entries.append(e)
        if size is None:
            problems.append("%s entry %d (%s): unknown type %d, so its size is unknown" % (label, i, e["key"], typ))
            break
        if vp + size > end:
            problems.append("%s entry %d (%s): value outside DATA" % (label, i, e["key"]))
            break
        q = vp + ((size + 1) & ~1)
    return length, entries, problems


def unstructured(db, idx):
    """Decode kind-1 object `idx`: (length_field, [entry dicts], [problems])."""
    return unstructured_at(db, db.obj(idx)[1], "object %d" % idx)


def array_info(db, idx):
    """(elem_type, struct_index, count, data_pos, stride) of array object `idx`."""
    kind, p = db.obj(idx)
    et, si, n = struct.unpack_from("<HHI", db.d, p)
    if et == 16:
        stride = (db.struct_size(si) + 3) & ~3 if si < len(db.structs) else None
    else:
        stride = ARRAY_ELEM_SIZE.get(et, TYPE_SIZE.get(et))
    return et, si, n, p + 8, stride


def build_tree(db, idx=0, _stack=None):
    """Decode object `idx` (and everything it references) into a Node tree."""
    stack = _stack or []
    if idx in stack:
        return Node("leaf", typ=15, value="<cycle to object %d>" % idx, obj=idx, known=False)
    if not 0 <= idx < len(db.ohdr):
        return Node("leaf", typ=15, value="<bad object %d>" % idx, obj=idx, known=False)
    stack = stack + [idx]
    kind, p = db.obj(idx)
    if kind == 1:
        node = Node("dict", obj=idx)
        _, entries, _ = unstructured(db, idx)
        for e in entries:
            if e["size"] is None:
                break
            node.items.append((e["key"], leaf_or_ref(db, e["type"], e["pos"], e["size"], stack)))
        return node
    if kind == 0:
        si = struct.unpack_from("<H", db.d, p)[0]
        node = Node("dict", obj=idx)
        for name, t, off in db.struct_fields(si):
            node.items.append((name, leaf_or_ref(db, t, p + 2 + off, TYPE_SIZE.get(t, 4), stack)))
        return node
    if kind == 2:
        et, si, n, dp, stride = array_info(db, idx)
        node = Node("array", obj=idx, elem_type=et)
        if stride is None:
            node.known = False
            node.items.append((0, Node("leaf", typ=et, value="<%d elements of unknown type %d>" % (n, et), known=False)))
            return node
        for i in range(n):
            ep = dp + i * stride
            if et == 16:
                el = Node("dict")
                for name, t, off in db.struct_fields(si):
                    el.items.append((name, leaf_or_ref(db, t, ep + off, TYPE_SIZE.get(t, 4), stack)))
                node.items.append((i, el))
            else:
                node.items.append((i, leaf_or_ref(db, et, ep, stride, stack)))
        return node
    return Node("leaf", typ=None, value="<kind %d>" % kind, known=False)


def leaf_or_ref(db, typ, pos, size, stack):
    if typ == 15 or typ == 17:  # a reference to another object
        ref = struct.unpack_from("<i", db.d, pos)[0] if typ == 17 else struct.unpack_from("<I", db.d, pos)[0]
        if typ == 17 and ref < 0:
            return Node("leaf", typ=typ, value=None, pos=pos, size=size)
        child = build_tree(db, ref, stack)
        child.pos, child.size = pos, size
        return child
    value, known = read_scalar(db, typ, pos)
    return Node("leaf", typ=typ, value=value, pos=pos, size=size, known=known)


# ---- paths ----------------------------------------------------------------------------------------------------

_BARE_KEY = re.compile(r'[^.\[\]"]+')
_INDEX_TOK = re.compile(r'\[(\d+)\]')
_QUOTED_TOK = re.compile(r'\["([^"]*)"\]')


def parse_path(text):
    if not text:
        raise Problem("empty path")
    out, i = [], 0
    while i < len(text):
        if i > 0:
            if text[i] == ".":
                i += 1
                if i >= len(text) or text[i] in ".[":
                    raise Problem("cannot parse path %r at %r" % (text, text[i - 1:]))
            elif text[i] != "[":
                raise Problem("cannot parse path %r at %r" % (text, text[i:]))
        elif text[i] == ".":
            raise Problem("cannot parse path %r at %r" % (text, text[i:]))
        if text[i] == "[":
            m = _INDEX_TOK.match(text, i)
            if m:
                out.append(int(m.group(1)))
                i = m.end()
                continue
            m = _QUOTED_TOK.match(text, i)
            if m:
                out.append(m.group(1))
                i = m.end()
                continue
            raise Problem("cannot parse path %r at %r" % (text, text[i:]))
        m = _BARE_KEY.match(text, i)
        if not m:
            raise Problem("cannot parse path %r at %r" % (text, text[i:]))
        out.append(m.group(0))
        i = m.end()
    return out


def format_path(parts):
    s = ""
    for p in parts:
        if isinstance(p, int):
            s += "[%d]" % p
        elif re.fullmatch(r"[\w\-:@/+ ]+", p):
            s += ("." if s else "") + p
        else:
            s += '["%s"]' % p
    return s


def find(node, parts):
    for want in parts:
        if node.kind == "array":
            if not isinstance(want, int) or not 0 <= want < len(node.items):
                raise Problem("no element %r" % (want,))
            node = node.items[want][1]
        elif node.kind == "dict":
            for k, v in node.items:
                if k == want:
                    node = v
                    break
            else:
                raise Problem("no key %r (have: %s)" % (want, ", ".join(str(k) for k, _ in node.items[:12])))
        else:
            raise Problem("%r is a value, not a container" % format_path(parts))
    return node


def leaves(node, parts=()):
    """Yield (path tuple, Node) for every scalar leaf, in file order."""
    if node.kind == "leaf":
        yield parts, node
    else:
        for k, child in node.items:
            yield from leaves(child, parts + (k,))


def show_value(node, types=False):
    v = node.value
    if node.kind != "leaf":
        return ""
    if node.typ == 13:
        s = "None" if v is None else repr(v)
    elif node.typ == 10:
        s = "%g" % v
    elif v is None:
        s = "None"
    elif not node.known:
        s = "raw " + str(v) + " ?t%s" % node.typ
    else:
        s = str(v)
    if types and node.known and node.typ in TYPE_NAME and node.typ not in (13, 15, 17):
        s += "  [%s]" % TYPE_NAME[node.typ]
    return s


# ---- dump -----------------------------------------------------------------------------------------------------

def print_tree(node, out, indent=0, name=None, types=False, ids=False):
    pad = "  " * indent
    tag = (" #%d" % node.obj) if ids and node.obj is not None else ""
    if node.kind == "leaf":
        out.append("%s%s = %s" % (pad, name, show_value(node, types)) if name is not None else pad + show_value(node, types))
        return
    inline = (node.kind == "array" and name is not None and len(node.items) <= 16 and
              all(c.kind == "leaf" for _, c in node.items))
    if inline:
        body = " ".join(show_value(c, False) for _, c in node.items) if node.items else ""
        suffix = "  [%s]" % TYPE_NAME.get(node.elem_type, "t%s" % node.elem_type) if types else ""
        out.append("%s%s[%d]%s = %s%s" % (pad, name, len(node.items), tag, body, suffix))
        return
    if name is not None:
        extra = "[%d]" % len(node.items) if node.kind == "array" else ""
        out.append("%s%s%s%s:" % (pad, name, extra, tag))
        indent += 1
    for k, child in node.items:
        if node.kind == "array":
            print_tree(child, out, indent, "[%d]" % k, types, ids)
        else:
            print_tree(child, out, indent, k, types, ids)


def cmd_dump(args):
    db = load(args.file)
    root = build_tree(db, 0)
    if args.path:
        root = find(root, parse_path(args.path))
    out = []
    print_tree(root, out, 0, None if root.kind != "leaf" else args.path, args.types, args.ids)
    print("\n".join(out))
    return 0


# ---- diff -----------------------------------------------------------------------------------------------------

def flat(db):
    d = {}
    for parts, node in leaves(build_tree(db, 0)):
        d[format_path(parts)] = node
    return d


def leaves_and_empties(node, parts=()):
    """Yield (path tuple, Node) for scalar leaves and empty containers, in file order."""
    if node.kind == "leaf" or not node.items:
        yield parts, node
    else:
        for k, child in node.items:
            yield from leaves_and_empties(child, parts + (k,))


def flat_for_diff(db):
    d = {}
    for parts, node in leaves_and_empties(build_tree(db, 0)):
        d[format_path(parts)] = node
    return d


def show_diff_node(node, types=False, float_fmt="%g"):
    if node.kind == "dict":
        return "{}"
    if node.kind == "array":
        suffix = "  [%s]" % TYPE_NAME.get(node.elem_type, "t%s" % node.elem_type) if types and node.elem_type is not None else ""
        return "[]" + suffix
    if node.typ == 10 and node.known:
        s = float_fmt % node.value
        if types:
            s += "  [float]"
        return s
    return show_value(node, types)


def nodes_equal(na, nb):
    if na.kind != nb.kind or na.typ != nb.typ or na.known != nb.known:
        return False
    if na.kind != "leaf":
        return na.elem_type == nb.elem_type
    if na.typ == 10 and na.known:
        return struct.pack("<f", na.value) == struct.pack("<f", nb.value)
    return na.value == nb.value


def diff_pair(a_path, b_path, types=False, quiet_same=False):
    fa, fb = flat_for_diff(load(a_path)), flat_for_diff(load(b_path))
    lines = []
    for path, nb in fb.items():
        na = fa.get(path)
        if na is None:
            lines.append("+ %s = %s" % (path, show_diff_node(nb, types)))
        elif not nodes_equal(na, nb):
            float_fmt = "%g"
            if na.kind == "leaf" and nb.kind == "leaf" and na.typ == 10 and nb.typ == 10 and na.known and nb.known:
                if ("%g" % na.value) == ("%g" % nb.value):
                    float_fmt = "%.9g"
            suffix = "" if (na.kind == nb.kind and na.typ == nb.typ) else "   (type %s -> %s)" % (
                na.typ if na.kind == "leaf" else na.kind, nb.typ if nb.kind == "leaf" else nb.kind)
            lines.append("~ %s: %s -> %s%s" % (
                path, show_diff_node(na, types, float_fmt), show_diff_node(nb, types, float_fmt), suffix))
    for path, na in fa.items():
        if path not in fb:
            lines.append("- %s = %s" % (path, show_diff_node(na, types)))
    if lines or not quiet_same:
        print("--- %s\n+++ %s" % (a_path, b_path))
        for line in lines:
            print(line)
        print("%d difference%s" % (len(lines), "" if len(lines) == 1 else "s"))
    return len(lines)


def list_sb_files(directory):
    found = {}
    for root, _, names in os.walk(directory):
        for n in sorted(names):
            if n.endswith(".sb"):
                full = os.path.join(root, n)
                rel = os.path.relpath(full, directory).replace("\\", "/")
                found[rel] = full
    return found


def cmd_diff(args):
    a_is_dir = os.path.isdir(args.a)
    if args.b is None:
        if a_is_dir:
            files = list_sb_files(args.a)
            pairs = [(full + ".bak", full) for _, full in sorted(files.items()) if os.path.exists(full + ".bak")]
            if not pairs:
                raise Problem("no *.sb files with a .bak sibling in %s" % args.a)
            total, changed = 0, 0
            for a_path, b_path in pairs:
                n = diff_pair(a_path, b_path, args.types, quiet_same=True)
                if n:
                    changed += 1
                    total += n
            print("%d file(s) checked, %d changed, %d total difference%s" % (
                len(pairs), changed, total, "" if total == 1 else "s"))
            return 1 if total else 0
        a_path = args.a + ".bak"
        if not os.path.exists(a_path):
            print("%s does not exist; give two files to compare" % a_path, file=sys.stderr)
            return 2
        return 1 if diff_pair(a_path, args.a, args.types) else 0

    b_is_dir = os.path.isdir(args.b)
    if a_is_dir != b_is_dir:
        raise Problem("cannot diff a directory against a file")
    if a_is_dir:
        fa_map, fb_map = list_sb_files(args.a), list_sb_files(args.b)
        all_rels = sorted(set(fa_map) | set(fb_map))
        total, changed = 0, 0
        for rel in all_rels:
            if rel not in fb_map:
                print("- only in %s: %s" % (args.a, rel))
                changed += 1
                total += 1
            elif rel not in fa_map:
                print("+ only in %s: %s" % (args.b, rel))
                changed += 1
                total += 1
            else:
                n = diff_pair(fa_map[rel], fb_map[rel], args.types, quiet_same=True)
                if n:
                    changed += 1
                    total += n
        print("%d file(s) compared, %d changed, %d total difference%s" % (
            len(all_rels), changed, total, "" if total == 1 else "s"))
        return 1 if total else 0

    return 1 if diff_pair(args.a, args.b, args.types) else 0


# ---------------------------------------------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------------------------------------------

def walk_chunks(data, report):
    """Chunk table of a raw file: {tag: (body_offset, size, stored_hash)}; problems go to report."""
    chunks = {}
    if len(data) < 8 or data[:4] != b"SBIN":
        report.error("not an SBIN file (magic %r)" % data[:4])
        return chunks
    version = struct.unpack_from("<I", data, 4)[0]
    if version != 2:
        report.warn("SBIN version %d (only 2 has been checked)" % version)
    o = 8
    while o < len(data):
        if o + 12 > len(data):
            report.warn("%d stray byte(s) after the last chunk" % (len(data) - o))
            break
        tag = data[o:o + 4]
        size, stored = struct.unpack_from("<II", data, o + 4)
        name = tag.decode("latin-1")
        if o + 12 + size > len(data):
            report.error("chunk %s: size %d runs past the end of the file" % (name, size))
            break
        body = data[o + 12:o + 12 + size]
        if name in chunks:
            report.error("chunk %s appears twice" % name)
        chunks[name] = (o + 12, size, stored)
        if fnv1(body) != stored:
            report.error("chunk %s: stored hash %08x but the data hashes to %08x" % (name, stored, fnv1(body)))
        if name not in KNOWN_CHUNKS:
            report.warn("unknown chunk %s (%d bytes)" % (name, size))
        nxt = (o + 12 + size + 3) & ~3
        if any(data[o + 12 + size:nxt]):
            report.warn("chunk %s: non-zero padding after the data" % name)
        o = nxt
    for tag in REQUIRED_CHUNKS:
        if tag not in chunks:
            report.error("missing chunk %s" % tag)
    return chunks


class Report:
    def __init__(self):
        self.errors, self.warnings, self.notes = [], [], []

    def error(self, m):
        self.errors.append(m)

    def warn(self, m):
        self.warnings.append(m)

    def note(self, m):
        self.notes.append(m)


def object_extent(db, idx, report):
    """(start, end) absolute byte range of object `idx`, or None when it cannot be worked out."""
    kind, p = db.obj(idx)
    d_end = db.c["DATA"][0] + db.c["DATA"][1]
    if kind == 0:
        si = struct.unpack_from("<H", db.d, p)[0] if p + 2 <= d_end else None
        if si is None or si >= len(db.structs):
            report.error("object %d: bad struct index" % idx)
            return None
        return p, p + 2 + db.struct_size(si)
    if kind == 1:
        length, entries, problems = unstructured(db, idx)
        for m in problems:
            report.error(m)
        if problems:
            return None
        end = max([4] + [e["pos"] - p + e["size"] for e in entries])
        if length != end:
            report.error("object %d: length field %d but its entries end at %d" % (idx, length, end))
        return p, p + max(length, end)
    if kind == 2:
        if p + 8 > d_end:
            report.error("object %d: array header outside DATA" % idx)
            return None
        et, si, n, dp, stride = array_info(db, idx)
        if stride is None:
            report.error("object %d: array of unknown element type %d" % (idx, et))
            return None
        if et == 16 and si >= len(db.structs):
            report.error("object %d: array of bad struct index %d" % (idx, si))
            return None
        return p, dp + n * stride
    report.error("object %d: unsupported kind %d" % (idx, kind))
    return None


def verify_data(data, coverage=False):
    report = Report()
    chunks = walk_chunks(data, report)
    if report.errors and any(t not in chunks for t in REQUIRED_CHUNKS):
        return report
    try:
        db = SBin(data)
    except Exception as e:  # a damaged file may fail in many ways; say which
        report.error("cannot parse the chunks: %r" % (e,))
        return report
    d_off, d_size, _ = db.c["DATA"]
    d_end = d_off + d_size
    for tag, unit in (("STRU", 6), ("FIEL", 8), ("OHDR", 4)):
        if db.c[tag][1] % unit:
            report.error("chunk %s: size %d is not a multiple of %d" % (tag, db.c[tag][1], unit))
    s_off, s_size, _ = db.c["STRS"]
    p = s_off + 4
    for i in range(len(db.strs)):
        p += 4 + struct.unpack_from("<I", data, p)[0]
    if p != s_off + s_size:
        report.warn("STRS: %d unexplained byte(s) after the %d strings" % (s_off + s_size - p, len(db.strs)))
    for fi, (name, typ, off, extra) in enumerate(db.fields):
        if typ not in TYPE_SIZE and typ not in (16,):
            report.warn("field definition %d: unknown type %d" % (fi, typ))
    extents, referenced, extras, types_seen = {}, set(), 0, {}

    def check_ref(typ, pos, label):
        if typ == 17:
            ref = struct.unpack_from("<i", data, pos)[0]
            if ref < 0:
                return
        else:
            ref = struct.unpack_from("<I", data, pos)[0]
        if ref >= len(db.ohdr):
            report.error("%s: reference to missing object %d" % (label, ref))
        else:
            referenced.add(ref)

    def check_str_idx(pos, label):
        sidx = struct.unpack_from("<H", data, pos)[0]
        if sidx > len(db.strs):
            report.error("%s: string index %d out of range" % (label, sidx))

    def check_struct_fields(si, base_pos, label):
        for fname, ftype, foff in db.struct_fields(si):
            fpos = base_pos + foff
            fsize = TYPE_SIZE.get(ftype)
            if fsize is None:
                continue
            if fpos + fsize > d_end:
                report.error("%s.%s: value outside DATA" % (label, fname))
                continue
            if ftype in (15, 17):
                check_ref(ftype, fpos, "%s.%s" % (label, fname))
            elif ftype == 13:
                check_str_idx(fpos, "%s.%s" % (label, fname))

    for idx in range(len(db.ohdr)):
        kind, p = db.obj(idx)
        if not d_off <= p < d_end:
            report.error("object %d: offset outside DATA" % idx)
            continue
        ext = object_extent(db, idx, report)
        if ext is None:
            continue
        if ext[1] > d_end:
            report.error("object %d: runs %d byte(s) past DATA" % (idx, ext[1] - d_end))
            continue
        extents[idx] = ext
        if kind == 0:
            si = struct.unpack_from("<H", data, p)[0]
            check_struct_fields(si, p + 2, "object %d" % idx)
        elif kind == 1:
            _, entries, _ = unstructured(db, idx)
            for e in entries:
                types_seen[e["type"]] = types_seen.get(e["type"], 0) + 1
                if e["key"] is None:
                    report.error("object %d: entry with bad key index %d" % (idx, e["key_idx"]))
                if e["type"] not in TYPE_NAME:
                    report.note("object %d key %s: type %d is not decoded (shown raw)" % (idx, e["key"], e["type"]))
                if e["extra"]:
                    extras += 1
                    report.note("object %d key %s: extra field is %d (zero everywhere else)" % (idx, e["key"], e["extra"]))
                if e["type"] in (15, 17):
                    check_ref(e["type"], e["pos"], "object %d key %s" % (idx, e["key"]))
                elif e["type"] == 13:
                    check_str_idx(e["pos"], "object %d key %s" % (idx, e["key"]))
        elif kind == 2:
            et, si, n, dp, stride = array_info(db, idx)
            if et in (15, 17):
                for i in range(n):
                    check_ref(et, dp + i * stride, "object %d element %d" % (idx, i))
            elif et == 13:
                for i in range(n):
                    check_str_idx(dp + 2 * i, "object %d element %d" % (idx, i))
            elif et == 16:
                for i in range(n):
                    check_struct_fields(si, dp + i * stride, "object %d element %d" % (idx, i))
    if len(db.ohdr) and 0 not in extents:
        report.error("the root object (0) could not be decoded")
    unreached = [i for i in extents if i != 0 and i not in referenced]
    if unreached and not any(db.obj(i)[0] == 0 for i in range(len(db.ohdr))):
        report.note("%d object(s) are not referenced by any key or array (the game leaves some behind, for example 3 per "
                    "level entry in AchievementsSave): %s" % (len(unreached), unreached[:10]))
    # DATA coverage: which bytes does the layout explain? Gaps between the objects are classified, not just listed.
    spans = sorted(extents.values())
    gaps, cur = [], d_off
    for s0, e0 in spans:
        if s0 > cur:
            gaps.append((cur, s0))
        cur = max(cur, e0)
    if cur < d_end:
        gaps.append((cur, d_end))
    pads, dead, dead_bytes, placeholder = 0, 0, 0, False
    for s0, e0 in gaps:
        p = s0
        while p < e0:
            # an object first (so the DATA-start placeholder 00 00 04 00 is not mistaken for padding), then padding
            length, entries, problems = unstructured_at(db, p, "gap object")
            end = max([4] + [e["pos"] - p + e["size"] for e in entries if e["size"]])
            if not problems and length == end and p + length <= e0:
                if p == d_off and not entries:
                    placeholder = True
                else:
                    dead += 1
                    dead_bytes += length
                    report.note("DATA byte %d: abandoned copy of an object (%d entries, newest key %s); the game left it "
                                "behind when the object grew" % (p - d_off, len(entries), entries[-1]["key"] if entries else None))
                p += length
                continue
            if data[p] in (0x00, 0xCD):
                q = p
                while q < e0 and q - p < 8 and data[q] in (0x00, 0xCD):
                    q += 1
                pads += 1
                p = q
                continue
            blob = data[p:e0]
            report.warn("DATA bytes %d..%d (%d byte(s)) are not explained by any object: %s" % (
                p - d_off, e0 - d_off, e0 - p, blob[:16].hex()))
            break
    if placeholder:
        report.note("DATA starts with an empty unstructured object (count 0, length 4): a placeholder for object offset 0")
    if pads:
        report.note("%d run(s) of alignment padding (0xCD or zero bytes)" % pads)
    if dead:
        report.note("%d abandoned object copie(s), %d bytes in all (the file was not compacted)" % (dead, dead_bytes))
    overlaps = sum(1 for (s1, e1), (s2, e2) in zip(spans, spans[1:]) if s2 < e1)
    if overlaps:
        report.note("%d object(s) overlap another (shared or aliased data)" % overlaps)
    report.note("%d object(s), %d string(s), value types seen in kind-1 objects: %s" % (
        len(db.ohdr), len(db.strs), ", ".join("%d x%d" % (t, n) for t, n in sorted(types_seen.items())) or "none"))
    return report


def cmd_verify(args):
    status = 0
    for path in args.files:
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as e:
            print("%s: cannot read (%s)" % (path, e.strerror))
            status = 1
            continue
        rep = verify_data(data, args.coverage)
        for m in rep.errors:
            print("%s: ERROR %s" % (path, m))
        for m in rep.warnings:
            print("%s: warning %s" % (path, m))
        if args.notes or args.coverage:
            for m in rep.notes:
                print("%s: note %s" % (path, m))
        bad = rep.errors or (args.strict and rep.warnings)
        print("%s: %s (%d error(s), %d warning(s))" % (path, "FAILED" if bad else "ok", len(rep.errors), len(rep.warnings)))
        status = status or (1 if bad else 0)
    return status


# ---------------------------------------------------------------------------------------------------------------
# census
# ---------------------------------------------------------------------------------------------------------------

def iter_files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                for n in sorted(names):
                    if n.endswith(".sb"):
                        yield os.path.join(root, n)
        else:
            yield p


_DYNAMIC_MAP_PARENTS = {"actors", "objectives"}


def schema_path(parts):
    """Normalize a concrete leaf path into a schema path: indices -> [], dynamic map keys -> *."""
    out = ""
    prev = None
    for p in parts:
        if isinstance(p, int):
            out += "[]"
            prev = None
        else:
            seg = "*" if prev in _DYNAMIC_MAP_PARENTS else p
            out += ("." if out else "") + seg
            prev = p
    return out or "(root)"


def cmd_census(args):
    stats, files, bad = {}, 0, 0
    for path in iter_files(args.paths):
        try:
            db = load(path)
        except Exception as e:
            print("%s: cannot read (%s)" % (path, e), file=sys.stderr)
            bad += 1
            continue
        files += 1
        for parts, node in leaves(build_tree(db, 0)):
            if args.by_leaf:
                names = [p for p in parts if isinstance(p, str)]
                key = names[-1] if names else "(root)"
            else:
                key = schema_path(parts)
            st = stats.setdefault(key, {"types": set(), "n": 0, "values": set(), "lo": None, "hi": None})
            st["n"] += 1
            st["types"].add(TYPE_NAME.get(node.typ, "t%s" % node.typ))
            v = node.value
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                st["lo"] = v if st["lo"] is None else min(st["lo"], v)
                st["hi"] = v if st["hi"] is None else max(st["hi"], v)
            if len(st["values"]) < 8:
                st["values"].add(show_value(node))
    print("%d file(s)%s" % (files, ", %d unreadable" % bad if bad else ""))
    width = max((len(k) for k in stats), default=34)
    width = max(34, min(width, 60))
    for key in sorted(stats):
        st = stats[key]
        rng = "" if st["lo"] is None else "  range %g..%g" % (st["lo"], st["hi"])
        vals = "" if st["lo"] is not None else "  e.g. " + ", ".join(sorted(st["values"])[:4])
        print("%-*s %6d  %-18s%s%s" % (width, key, st["n"], "/".join(sorted(st["types"])), rng, vals))
    return 0 if not bad else 1


# ---------------------------------------------------------------------------------------------------------------
# set
# ---------------------------------------------------------------------------------------------------------------

def encode_value(db, node, text):
    """(bytes, decoded_value) to write for `text` at scalar leaf `node`; the size never changes."""
    t = node.typ
    try:
        if t == 5:
            v = int(text, 0)
            if not -2 ** 31 <= v < 2 ** 31:
                raise Problem("%s does not fit int32" % text)
            return struct.pack("<i", v), v
        if t == 6:
            v = int(text, 0)
            if not 0 <= v < 2 ** 32:
                raise Problem("%s does not fit uint32" % text)
            return struct.pack("<I", v), v
        if t == 9:
            low = text.lower()
            if low == "true":
                v = 1
            elif low == "false":
                v = 0
            else:
                v = int(text, 0)
            if v not in (0, 1):
                raise Problem("%r is not a valid bool (expected true, false, 0 or 1)" % text)
            return bytes([v]), v
        if t == 10:
            v = float(text)
            if not math.isfinite(v):
                raise Problem("%r is not a finite float" % text)
            raw = struct.pack("<f", v)
            return raw, struct.unpack("<f", raw)[0]
        if t == 13:
            if text not in db.strs:
                raise Problem("%r is not in the file's string table (adding strings is not supported)" % text)
            return struct.pack("<H", db.strs.index(text) + 1), text
    except ValueError:
        raise Problem("%r is not a valid value for a %s" % (text, TYPE_NAME.get(t)))
    raise Problem("type %s cannot be changed by this tool" % t)


def cmd_set(args):
    try:
        with open(args.file, "rb") as f:
            original = f.read()
    except OSError as e:
        raise Problem("cannot read %s: %s" % (args.file, e.strerror or e))
    data = bytearray(original)
    try:
        db = SBin(original)
    except Exception as e:
        raise Problem("cannot parse %s as SBIN (%s)" % (args.file, e))
    root = build_tree(db, 0)
    changes = []
    for item in args.assignments:
        if "=" not in item:
            raise Problem("expected PATH=VALUE, got %r" % item)
        path, text = item.split("=", 1)
        parts = parse_path(path)
        node = find(root, parts)
        if node.kind != "leaf" or node.pos is None:
            raise Problem("%s is not a scalar value" % path)
        new_bytes, decoded = encode_value(db, node, text)
        data[node.pos:node.pos + len(new_bytes)] = new_bytes
        changes.append((format_path(parts), show_value(node), text, TYPE_NAME.get(node.typ), node.pos))
        node.value = decoded
    d_off, d_size, _ = db.c["DATA"]
    struct.pack_into("<I", data, d_off - 4, fnv1(bytes(data[d_off:d_off + d_size])))
    report = verify_data(bytes(data))
    for path, old, new, tname, pos in changes:
        print("%s: %s -> %s  (%s at file offset %d)" % (path, old, new, tname, pos))
    if report.errors:
        for m in report.errors:
            print("ERROR after the change: " + m)
        print("refusing to write")
        return 1
    if args.dry_run:
        print("dry run: nothing written")
        return 0
    if args.output:
        target = args.output
    elif args.in_place:
        target = args.file
        backup, n = args.file + ".preedit", 0
        while os.path.exists(backup):
            n += 1
            backup = "%s.preedit%d" % (args.file, n)
        with open(backup, "wb") as f:
            f.write(original)
        print("original kept as " + backup)
    else:
        print("give -o OUT or --in-place (or --dry-run to only look)")
        return 2
    with open(target, "wb") as f:
        f.write(bytes(data))
    print("wrote " + target)
    return 0


def load(path):
    try:
        with open(path, "rb") as f:
            raw = f.read()
    except OSError as e:
        raise Problem("cannot read %s: %s" % (path, e.strerror or e))
    if len(raw) < 8 or raw[:4] != b"SBIN":
        raise Problem("%s is not an SBIN file" % path)
    try:
        return SBin(raw)
    except Exception as e:
        raise Problem("cannot parse %s as SBIN (%s)" % (path, e))


COMMANDS = {"dump", "diff", "verify", "census", "set"}


def build_parser():
    ap = argparse.ArgumentParser(prog="sbin.py", description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("dump", help="print the decoded tree")
    p.add_argument("file")
    p.add_argument("path", nargs="?")
    p.add_argument("--types", action="store_true", help="show value types")
    p.add_argument("--ids", action="store_true", help="show object indexes")
    p = sub.add_parser("diff", help="semantic diff of two files or directories (default: .bak against current)")
    p.add_argument("a")
    p.add_argument("b", nargs="?")
    p.add_argument("--types", action="store_true")
    p = sub.add_parser("verify", help="check hashes, lengths, references and unexplained bytes")
    p.add_argument("files", nargs="+")
    p.add_argument("--coverage", action="store_true", help="also print how every DATA gap was explained")
    p.add_argument("--strict", action="store_true", help="warnings fail too")
    p.add_argument("--notes", action="store_true", help="also print informational notes")
    p = sub.add_parser("census", help="schema paths (or leaf names), types and value ranges over many files")
    p.add_argument("paths", nargs="+")
    p.add_argument("--by-leaf", action="store_true", help="group by leaf key name instead of normalized schema path")
    p = sub.add_parser("set", help="change scalar values in place")
    p.add_argument("file")
    p.add_argument("assignments", nargs="+", metavar="PATH=VALUE")
    dest = p.add_mutually_exclusive_group()
    dest.add_argument("-o", "--output")
    dest.add_argument("--in-place", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    return ap


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] in ("-h", "--help") and len(argv) == 1:
        print(__doc__)
        return 0
    if argv[0] not in COMMANDS:
        layouts_main(argv)
        return 0
    args = build_parser().parse_args(argv)
    try:
        return {"dump": cmd_dump, "diff": cmd_diff, "verify": cmd_verify, "census": cmd_census, "set": cmd_set}[args.cmd](args)
    except Problem as e:
        print("error: %s" % e, file=sys.stderr)
        return 2
    except BrokenPipeError:
        return 0


if __name__ == "__main__":
    try:
        code = main()
        sys.stdout.flush()
    except BrokenPipeError:  # `sbin.py ... | head`
        os.dup2(os.open(os.devnull, os.O_WRONLY), sys.stdout.fileno())
        code = 0
    sys.exit(code)
