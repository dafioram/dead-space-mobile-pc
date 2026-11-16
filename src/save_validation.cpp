#include "save_validation.h"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <vector>

namespace save_validation {
namespace {
struct Reader {
    std::span<const uint8_t> bytes;
    bool has(size_t p, size_t n) const { return p <= bytes.size() && n <= bytes.size() - p; }
    uint16_t u16(size_t p) const { return uint16_t(bytes[p] | uint16_t(bytes[p + 1]) << 8); }
    uint32_t u32(size_t p) const { return uint32_t(u16(p)) | uint32_t(u16(p + 2)) << 16; }
};
unsigned width(uint16_t type) {
    switch (type) {
    case 1: case 2: case 9: return 1;
    case 3: case 4: case 12: case 13: case 20: return 2;
    case 5: case 6: case 10: case 15: case 17: case 18: case 19: return 4;
    case 7: case 8: case 11: return 8;
    default: return 0;
    }
}
}
const char* name(Status status) {
    return status == Status::valid ? "structurally-valid" : status == Status::invalid ? "INVALID" : "unverified-format";
}
Result inspect(std::span<const uint8_t> bytes) {
    Result result;
    result.digest = 14695981039346656037ull;
    for (uint8_t byte : bytes) result.digest = (result.digest ^ byte) * 1099511628211ull;
    auto fail = [&](const std::string& why) { result.status = Status::invalid; result.reason = why; return result; };
    auto unknown = [&](const std::string& why) { result.status = Status::unknown; result.reason = why; return result; };
    Reader file{bytes};
    if (!file.has(0, 8)) return fail("truncated SBIN header");
    if (!std::equal(bytes.begin(), bytes.begin() + 4, "SBIN")) return fail("not an SBIN file");
    result.version = file.u32(4);
    if (result.version != 2) return unknown("unsupported SBIN version; native compatibility not assumed");
    std::map<std::string, Reader> chunks;
    bool unfamiliar_chunk = false;
    const std::set<std::string> known{"STRU", "FIEL", "ENUM", "OHDR", "DATA", "STRS", "SYMB"};
    for (size_t p = 8; p < bytes.size();) {
        if (!file.has(p, 12)) return fail("truncated section header at " + std::to_string(p));
        std::string tag(reinterpret_cast<const char*>(bytes.data() + p), 4);
        std::string label = tag;
        for (char& c : label) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) c = '?';
        uint32_t size = file.u32(p + 4), hash = file.u32(p + 8);
        if (!file.has(p + 12, size)) return fail("truncated section " + label);
        if (chunks.contains(tag)) return fail("duplicate section " + label);
        auto body = bytes.subspan(p + 12, size);
        uint32_t actual = 0x811c9dc5;
        for (uint8_t b : body) actual = (actual * 0x01000193u) ^ b;
        if (hash != actual) return fail("checksum mismatch in " + label);
        chunks.emplace(tag, Reader{body});
        unfamiliar_chunk |= !known.contains(tag);
        size_t end = p + 12 + size, padding = (4 - end % 4) % 4;
        if (!file.has(end, padding)) return fail("truncated alignment after " + label);
        p = end + padding;
    }
    for (const char* required : {"STRU", "FIEL", "ENUM", "OHDR", "DATA", "STRS", "SYMB"})
        if (!chunks.contains(required)) return fail(std::string("missing section ") + required);
    Reader strings = chunks.at("STRS"), structures = chunks.at("STRU"), fields = chunks.at("FIEL");
    Reader headers = chunks.at("OHDR"), data = chunks.at("DATA");
    Reader symbols = chunks.at("SYMB");
    if (!symbols.has(0, 4) || symbols.u32(0) > (symbols.bytes.size() - 4) / 4) return fail("truncated symbol table");
    bool unfamiliar_metadata = !chunks.at("ENUM").bytes.empty() || symbols.u32(0) != 0 || symbols.bytes.size() != 4;
    if (!strings.has(0, 4)) return fail("missing string count");
    uint32_t string_count = strings.u32(0);
    if (string_count > (strings.bytes.size() - 4) / 4) return fail("string count exceeds section");
    size_t cursor = 4;
    for (uint32_t i = 0; i < string_count; i++) {
        if (!strings.has(cursor, 4)) return fail("truncated string length");
        uint32_t n = strings.u32(cursor);
        cursor += 4;
        if (!strings.has(cursor, n)) return fail("string length exceeds section");
        cursor += n;
    }
    if (cursor != strings.bytes.size()) return fail("trailing bytes in string section");
    if (structures.bytes.size() % 6 || fields.bytes.size() % 8 || headers.bytes.size() % 4) return fail("misaligned schema/object table");
    size_t ns = structures.bytes.size() / 6, nf = fields.bytes.size() / 8, no = headers.bytes.size() / 4;
    if (!no) return fail("no root object");
    result.objects = uint32_t(no);
    bool unfamiliar_type = false;
    std::vector<size_t> struct_sizes(ns);
    for (size_t i = 0; i < nf; i++) {
        uint16_t name = fields.u16(i * 8), type = fields.u16(i * 8 + 2);
        if (!name || name > string_count) return fail("field name references missing string");
        unfamiliar_type |= !width(type);
    }
    for (size_t i = 0; i < ns; i++) {
        uint16_t name = structures.u16(i * 6), first = structures.u16(i * 6 + 2), count = structures.u16(i * 6 + 4);
        if (!name || name > string_count || first > nf || count > nf - first) return fail("invalid structure definition");
        for (size_t j = first; j < size_t(first) + count; j++) {
            unsigned n = width(fields.u16(j * 8 + 2));
            if (n) struct_sizes[i] = std::max(struct_sizes[i], size_t(fields.u16(j * 8 + 4)) + n);
        }
    }
    std::vector<size_t> offsets;
    offsets.reserve(no);
    for (size_t i = 0; i < no; i++) {
        uint32_t h = headers.u32(i * 4);
        if ((h & 7) > 2) return unknown("unsupported object kind");
        size_t p = h >> 3;
        if (!data.has(p, std::array<size_t,3>{2,4,8}[h & 7])) return fail("object header outside DATA");
        offsets.push_back(p);
    }
    if ((headers.u32(0) & 7) != 1) return fail("first object is not a root");
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    size_t checks = 0, budget = bytes.size() > 1000000 ? 8000000 : bytes.size() * 8 + 1024;
    bool exhausted = false;
    auto value_ok = [&](uint16_t type, size_t p, size_t end) {
        if (++checks > budget) { exhausted = true; return false; }
        unsigned n = width(type);
        if (!n || p > end || n > end - p || !data.has(p, n)) return false;
        if (type == 13 && data.u16(p) > string_count) return false;
        if (type == 15 || type == 17) {
            uint32_t ref = data.u32(p);
            if (ref != 0xffffffffu && ref >= no) return false;
        }
        return true;
    };
    // Unknown types are not assigned a guessed size; keep envelope/schema checks
    // but defer semantic interpretation to the native loader rather than banning saves.
    if (unfamiliar_type) return unknown("unfamiliar field type; envelope/checksums validated");
    auto struct_ok = [&](size_t si, size_t p, size_t end) {
        if (si >= ns || p > end || struct_sizes[si] > end - p) return false;
        size_t first = structures.u16(si * 6 + 2), count = structures.u16(si * 6 + 4);
        for (size_t j = first; j < first + count; j++)
            if (!value_ok(fields.u16(j * 8 + 2), p + fields.u16(j * 8 + 4), end)) return false;
        return true;
    };
    std::map<size_t, uint32_t> visited;
    for (size_t i = 0; i < no; i++) {
        uint32_t h = headers.u32(i * 4);
        size_t p = h >> 3;
        auto [seen, inserted] = visited.emplace(p, h & 7);
        if (!inserted) {
            if (seen->second != (h & 7)) return fail("object aliases disagree on kind");
            continue;
        }
        auto next = std::upper_bound(offsets.begin(), offsets.end(), p);
        size_t end = next == offsets.end() ? data.bytes.size() : *next;
        switch (h & 7) {
        case 0:
            if (!struct_ok(data.u16(p), p + 2, end)) {
                if (exhausted) return unknown("bounded inspection work limit; no compatibility claim");
                return fail("invalid structured object/field reference at object " + std::to_string(i));
            }
            break;
        case 1: {
            uint16_t count = data.u16(p), size = data.u16(p + 2);
            if (size < 4 || size > end - p) return fail("root length exceeds object boundary");
            size_t q = 4;
            for (uint32_t j = 0; j < count; j++) {
                q = (q + 1) & ~size_t(1);
                if (q > size || size - q < 8) return fail("root field count exceeds object");
                uint16_t name = data.u16(p + q), type = data.u16(p + q + 2);
                uint32_t off = data.u32(p + q + 4);
                if (!name || name > string_count) return fail("root field name references missing string");
                if (!width(type)) return unknown("unfamiliar root field type; envelope/checksums validated");
                if (off < q + 8 || off > size || !value_ok(type, p + off, p + size)) {
                    if (exhausted) return unknown("bounded inspection work limit; no compatibility claim");
                    return fail("invalid root field/value reference at object " + std::to_string(i));
                }
                q = off + width(type);
            }
            if (q != size) return fail("root length does not match its fields");
            break;
        }
        case 2: {
            uint16_t type = data.u16(p), si = data.u16(p + 2);
            uint32_t count = data.u32(p + 4);
            if (end - p < 8) return fail("array header crosses object boundary");
            if (type == 16 && si >= ns) return fail("array references missing structure");
            size_t stride = type == 16 ? (struct_sizes[si] + 3) & ~size_t(3) : width(type);
            if (!stride && count) return unknown("unfamiliar/zero-width array element");
            if (stride && count > (end - p - 8) / stride) return fail("array count exceeds object boundary");
            for (uint32_t j = 0; j < count && stride; j++) {
                size_t q = p + 8 + size_t(j) * stride;
                if (!(type == 16 ? struct_ok(si, q, end) : value_ok(type, q, end))) {
                    if (exhausted) return unknown("bounded inspection work limit; no compatibility claim");
                    return fail("invalid array element/reference");
                }
            }
            break;
        }
        }
    }
    result.status = unfamiliar_chunk || unfamiliar_metadata ? Status::unknown : Status::valid;
    result.reason = unfamiliar_chunk || unfamiliar_metadata ? "unfamiliar section/enum/symbol metadata; known sections/checksums validated" : "bounds, section checksums, schema and object/string references checked; gameplay compatibility not established";
    return result;
}
}  // namespace save_validation
