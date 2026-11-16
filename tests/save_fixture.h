// A small synthetic SBIN schema: scalar/string/array root, a structured object,
// and an object-reference array. No extracted game bytes or real save data.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace save_fixture {
using Bytes = std::vector<uint8_t>;
inline void u16(Bytes& b, uint16_t n) { b.push_back(uint8_t(n)); b.push_back(uint8_t(n >> 8)); }
inline void u32(Bytes& b, uint32_t n) { u16(b, uint16_t(n)); u16(b, uint16_t(n >> 16)); }
inline void put16(Bytes& b, size_t p, uint16_t n) { b.at(p) = uint8_t(n); b.at(p + 1) = uint8_t(n >> 8); }
inline void put32(Bytes& b, size_t p, uint32_t n) { put16(b,p,uint16_t(n)); put16(b,p+2,uint16_t(n>>16)); }
inline void chunk(Bytes& b, const char* tag, const Bytes& body) {
    b.insert(b.end(), tag, tag + 4);
    u32(b, uint32_t(body.size()));
    uint32_t hash = 0x811c9dc5;
    for (uint8_t c : body) hash = (hash * 0x01000193u) ^ c;
    u32(b, hash);
    b.insert(b.end(), body.begin(), body.end());
    while (b.size() % 4) b.push_back(0);
}
inline Bytes valid(uint32_t marker = 123) {
    Bytes strings;
    u32(strings, 7);
    for (std::string text : {"root", "number", "name", "items", "synthetic", "Tiny", "value"}) {
        u32(strings,uint32_t(text.size())); strings.insert(strings.end(),text.begin(),text.end());
    }
    Bytes stru; u16(stru,6); u16(stru,0); u16(stru,1);
    Bytes fields; u16(fields,7); u16(fields,5); u16(fields,0); u16(fields,0);
    Bytes headers; u32(headers,1); u32(headers,38u<<3); u32(headers,(44u<<3)|2);
    Bytes data;
    u16(data,3); u16(data,38);
    u16(data,2); u16(data,5); u32(data,12); u32(data,marker);
    u16(data,3); u16(data,13); u32(data,24); u16(data,5);
    u16(data,4); u16(data,17); u32(data,34); u32(data,2);
    u16(data,0); u32(data,marker + 1);
    u16(data,15); u16(data,0); u32(data,1); u32(data,1);
    Bytes result{'S','B','I','N'}; u32(result,2);
    chunk(result,"STRU",stru); chunk(result,"FIEL",fields); chunk(result,"ENUM",{});
    chunk(result,"OHDR",headers); chunk(result,"DATA",data); chunk(result,"STRS",strings);
    Bytes symbols; u32(symbols,0); chunk(result,"SYMB",symbols);
    return result;
}
inline size_t section(const Bytes& bytes, const char* tag) {
    auto get = [&](size_t p) { return uint32_t(bytes.at(p)) | uint32_t(bytes.at(p+1))<<8 | uint32_t(bytes.at(p+2))<<16 | uint32_t(bytes.at(p+3))<<24; };
    for (size_t p = 8; p + 12 <= bytes.size(); p = (p + 12 + get(p+4) + 3) & ~size_t(3))
        if (std::string(reinterpret_cast<const char*>(bytes.data()+p),4) == std::string(tag,4)) return p;
    return bytes.size();
}
inline void rehash(Bytes& bytes, const char* tag) {
    size_t p=section(bytes,tag), n=uint32_t(bytes.at(p+4)) | uint32_t(bytes.at(p+5))<<8 | uint32_t(bytes.at(p+6))<<16 | uint32_t(bytes.at(p+7))<<24;
    uint32_t hash=0x811c9dc5;
    for(size_t i=p+12;i<p+12+n;i++) hash=(hash*0x01000193u)^bytes.at(i);
    put32(bytes,p+8,hash);
}
}
