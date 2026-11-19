#pragma once
#include "geometry_capture.h"
#include <cstring>
#include <limits>

namespace geometry {
// Checked span for CPU VBO shadows or the reserved guest address space. Guest
// spans additionally supply a mapped-page predicate; this never commits pages.
struct Source {
    const unsigned char* data=nullptr;
    uint64_t bytes=0,base=0;
    bool (*mapped)(uint64_t,size_t)=nullptr;
    static bool read(const void* context,uint64_t offset,void* out,size_t size) {
        const auto& s=*static_cast<const Source*>(context);
        if(!s.data || offset>s.bytes || size>s.bytes-offset || offset>std::numeric_limits<uint64_t>::max()-s.base)
            return false;
        if(s.mapped && !s.mapped(s.base+offset,size))return false;
        std::memcpy(out,s.data+size_t(offset),size);return true;
    }
    Reader reader() const { return {this,read}; }
};
}  // namespace geometry
