#!/usr/bin/env python3
"""Disassemble a function (or address range) of the guest lib by symbol name.

usage: disas.py [--lib PATH] [-s] <symbol|substring|0xADDR> [count]   (or: disas.py LIB.so SYMBOL ...)
       disas.py [--lib PATH] --grep REGEX

  <symbol|substring>  an exact symbol name, else the shortest symbol containing it (candidates are listed)
  0xADDR              disassemble from an address
  count               maximum number of instructions (default: the whole function for a symbol, 80 for 0xADDR)
  -s, --symbolize     name the target of every bl/blx (needs c++filt for readable names)
  --grep REGEX        list function symbols whose (mangled) name matches REGEX and exit
  --lib PATH          the guest library (default: $DS_LIB, else apk/assets/guest_libs/libDeadSpace.so)

The library may also be given as the first argument. Needs: pip install capstone (the ELF file is read
directly; c++filt from binutils is optional and only makes -s output readable)."""
import bisect
import os
import re
import struct
import subprocess
import sys

from capstone import CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB, Cs

LIB = os.environ.get("DS_LIB", "apk/assets/guest_libs/libDeadSpace.so")


def parse_args(argv):
    opts = {"symbolize": False, "grep": None}
    rest = []
    it = iter(argv)
    for a in it:
        if a == "--lib":
            global LIB
            LIB = next(it, None) or sys.exit(__doc__)
        elif a == "--grep":
            opts["grep"] = next(it, None) or sys.exit(__doc__)
        elif a in ("-s", "--symbolize"):
            opts["symbolize"] = True
        elif a in ("-h", "--help"):
            sys.exit(__doc__)
        else:
            rest.append(a)
    return opts, rest


class Elf:
    """Just enough ELF (32 or 64 bit) to list FUNC symbols and map virtual addresses to file offsets."""

    def __init__(self, path):
        try:
            self.data = d = open(path, "rb").read()
        except OSError as e:
            sys.exit(f"cannot read the library {path!r}: {e.strerror} (use --lib PATH or set DS_LIB)")
        if d[:4] != b"\x7fELF":
            sys.exit(f"{path!r} is not an ELF file")
        self.is64 = d[4] == 2
        e = self.e = "<" if d[5] == 1 else ">"
        if self.is64:
            phoff, shoff = struct.unpack_from(e + "QQ", d, 0x20)
            phentsize, phnum, shentsize, shnum = struct.unpack_from(e + "HHHH", d, 0x36)
        else:
            phoff, shoff = struct.unpack_from(e + "II", d, 0x1C)
            phentsize, phnum, shentsize, shnum = struct.unpack_from(e + "HHHH", d, 0x2A)
        self.segments = []  # PT_LOAD: (vaddr, offset, filesz)
        for i in range(phnum):
            o = phoff + i * phentsize
            if self.is64:
                t, _fl, off, va, _pa, filesz, _ms, _al = struct.unpack_from(e + "IIQQQQQQ", d, o)
            else:
                t, off, va, _pa, filesz, _ms, _fl, _al = struct.unpack_from(e + "IIIIIIII", d, o)
            if t == 1:
                self.segments.append((va, off, filesz))
        self.sections = []  # (type, offset, size, link, entsize)
        for i in range(shnum):
            o = shoff + i * shentsize
            if self.is64:
                _n, t, _fl, _addr, off, size, link, _inf, _al, ent = struct.unpack_from(e + "IIQQQQIIQQ", d, o)
            else:
                _n, t, _fl, _addr, off, size, link, _inf, _al, ent = struct.unpack_from(e + "IIIIIIIIII", d, o)
            self.sections.append((t, off, size, link, ent))

    def functions(self):
        """(address, size, name) of every defined FUNC symbol, from .symtab and .dynsym (deduplicated)."""
        d, e, out = self.data, self.e, set()
        for t, off, size, link, ent in self.sections:
            if t not in (2, 11) or not ent or link >= len(self.sections):  # SHT_SYMTAB, SHT_DYNSYM
                continue
            str_off = self.sections[link][1]
            for o in range(off, off + size - ent + 1, ent):
                if self.is64:
                    name, info, _oth, shndx, value, sz = struct.unpack_from(e + "IBBHQQ", d, o)
                else:
                    name, value, sz, info, _oth, shndx = struct.unpack_from(e + "IIIBBH", d, o)
                if info & 0xF == 2 and shndx != 0:
                    end = d.index(b"\0", str_off + name)
                    out.add((value, sz, d[str_off + name:end].decode("ascii", "replace")))
        return sorted(out)

    def read(self, va, size):
        for seg_va, off, filesz in self.segments:
            if seg_va <= va < seg_va + filesz:
                start = off + va - seg_va
                return self.data[start:start + min(size, seg_va + filesz - va)]
        return self.data[va:va + size]  # no PT_LOAD found: assume virtual address == file offset


def demangle(names):
    try:
        out = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True).stdout
        out = out.split("\n")
        return out if len(out) >= len(names) else names
    except OSError:
        return names


def main(argv):
    opts, rest = parse_args(argv)
    if rest and os.path.isfile(rest[0]) and not rest[0].startswith("0x"):
        global LIB
        LIB = rest.pop(0)
    elf = Elf(LIB)
    syms = elf.functions()
    if opts["grep"]:
        pattern = re.compile(opts["grep"])
        for name in sorted({s[2] for s in syms if pattern.search(s[2])}):
            print(name)
        return
    if not rest:
        sys.exit(__doc__)
    q = rest[0]
    explicit = int(rest[1]) if len(rest) > 1 else None

    if q.startswith("0x"):
        va = int(q, 16)
        thumb = va & 1
        va &= ~1
        name = q
        size = (explicit or 80) * 4
    else:
        exact = [s for s in syms if s[2] == q]
        cands = exact or sorted((s for s in syms if q in s[2]), key=lambda s: len(s[2]))
        if not cands:
            sys.exit(f"no symbol containing {q!r} (try --grep)")
        if not exact:
            for c in cands[:8]:
                print("cand:", hex(c[0]), c[1], c[2])
        va, size, name = cands[0]
        thumb = va & 1
        va &= ~1
        if explicit:
            size = min(size or explicit * 4, explicit * 4)
        elif not size:
            size = 80 * 4

    names = None
    addrs = None
    if opts["symbolize"]:
        ordered = sorted((s[0] & ~1, s[2]) for s in syms)
        addrs = [a for a, _ in ordered]
        names = demangle([n for _, n in ordered])
        starts = ordered

    def target_name(a):
        i = bisect.bisect_right(addrs, a) - 1
        if i < 0:
            return hex(a)
        return names[i] + ("" if a == starts[i][0] else "+%#x" % (a - starts[i][0]))

    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    print(f"== {name} @ {va:#x} size {size} {'thumb' if thumb else 'arm'}")
    for i in md.disasm(elf.read(va, size), va):
        extra = ""
        if names and i.mnemonic in ("bl", "blx") and i.op_str.startswith("#"):
            extra = "  ; " + target_name(int(i.op_str[1:], 16))[:110]
        print(f"{i.address:08x}: {i.mnemonic:8s} {i.op_str}{extra}")


if __name__ == "__main__":
    main(sys.argv[1:])
