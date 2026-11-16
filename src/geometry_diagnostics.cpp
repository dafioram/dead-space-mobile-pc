#include "geometry_diagnostics.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace geometry {
namespace {
size_t width(uint32_t type) {
    switch (type) {
    case byte: case unsigned_byte: return 1;
    case short_type: case unsigned_short: return 2;
    case unsigned_int: case float_type: case fixed: return 4;
    default: return 0;
    }
}
void hash(uint64_t& digest, const void* bytes, size_t size) {
    auto p = static_cast<const uint8_t*>(bytes);
    for (size_t i = 0; i < size; i++) digest = (digest ^ p[i]) * 1099511628211ull;
}
template<class T> void hash(uint64_t& digest, const T& value) { hash(digest, &value, sizeof value); }
bool finite(const Point& point) {
    return std::all_of(point.begin(), point.end(), [](double n) { return std::isfinite(n); });
}
Point transform(const std::array<float,16>& matrix, const Point& point) {
    Point out{};
    for (size_t row = 0; row < 4; row++)
        for (size_t k = 0; k < 4; k++) out[row] += double(matrix[k * 4 + row]) * point[k];
    return out;
}
double distance(const Point& a, const Point& b) {
    return std::hypot(a[0]-b[0], a[1]-b[1], a[2]-b[2]);
}
bool index(const Draw& draw, size_t slot, uint32_t& out) {
    if (!draw.indexed) {
        uint64_t n = uint64_t(draw.first) + slot;
        if (draw.first < 0 || n > UINT32_MAX) return false;
        out = uint32_t(n); return true;
    }
    size_t n = width(draw.index_type);
    uint8_t value[4]{};
    if (!n || !read(draw.indices, uint64_t(slot) * n, value, n)) return false;
    out = value[0];
    if (n >= 2) out |= uint32_t(value[1]) << 8;
    if (n == 4) out |= uint32_t(value[2]) << 16 | uint32_t(value[3]) << 24;
    return true;
}
bool vertex(const Array& array, uint32_t i, Point& out) {
    size_t n = width(array.type);
    uint64_t stride = array.stride ? uint64_t(array.stride) : n * array.components;
    uint8_t bytes[16]{};
    if (!read(array.source, uint64_t(i) * stride, bytes, n * array.components)) return false;
    out = {0,0,0,1};
    for (int k = 0; k < array.components; k++) {
        const uint8_t* p = bytes + k * n;
        switch (array.type) {
        case byte: { int8_t v; std::memcpy(&v,p,1); out[k] = v; break; }
        case unsigned_byte: out[k] = *p; break;
        case short_type: { int16_t v; std::memcpy(&v,p,2); out[k] = v; break; }
        case unsigned_short: { uint16_t v; std::memcpy(&v,p,2); out[k] = v; break; }
        case unsigned_int: { uint32_t v; std::memcpy(&v,p,4); out[k] = v; break; }
        case fixed: { int32_t v; std::memcpy(&v,p,4); out[k] = double(v)/65536.0; break; }
        default: { float v; std::memcpy(&v,p,4); out[k] = v; break; }
        }
    }
    return true;
}
bool layout(const Array& array, int low, int high) {
    return array.enabled && width(array.type) && array.components >= low && array.components <= high && array.stride >= 0;
}
void bounds(Summary& summary, const Point& local, const Point& eye) {
    for (int k = 0; k < 3; k++) {
        if (!summary.have_bounds) {
            summary.local_min[k] = summary.local_max[k] = local[k];
            summary.eye_min[k] = summary.eye_max[k] = eye[k];
        } else {
            summary.local_min[k] = std::min(summary.local_min[k],local[k]);
            summary.local_max[k] = std::max(summary.local_max[k],local[k]);
            summary.eye_min[k] = std::min(summary.eye_min[k],eye[k]);
            summary.eye_max[k] = std::max(summary.eye_max[k],eye[k]);
        }
    }
    summary.have_bounds = true;
}
}
bool read(const Source& source, uint64_t offset, void* output, size_t size) {
    if (!size) return true;
    if (!source.data || offset > source.bytes || size > source.bytes - offset || offset > SIZE_MAX - size) return false;
    if (source.guest) {
        if (offset > (uint64_t(1) << 32) - source.guest_address) return false;
        uint64_t begin = uint64_t(source.guest_address) + offset;
        if (!source.guest_address || begin >= (uint64_t(1)<<32) || size > (uint64_t(1)<<32) - begin || !source.mapped) return false;
        for (uint64_t page = begin & ~uint64_t(4095); page < begin + size; page += 4096)
            if (!source.mapped(uint32_t(page))) return false;
    }
    std::memcpy(output, source.data + size_t(offset), size);
    return true;
}
Summary inspect(const Draw& draw, size_t budget) {
    Summary out;
    out.caller=draw.caller; out.mode=draw.mode; out.index_type=draw.index_type;
    out.index_pointer=draw.index_pointer; out.index_buffer=draw.index_buffer; out.index_generation=draw.index_generation;
    out.indexed=draw.indexed; out.count=draw.count; out.first=draw.first;
    out.position=draw.position; out.normal=draw.normal; out.color=draw.color; out.uv=draw.uv; out.state=draw.state;
    // A submission signature is NOT an object identity. Guest scratch storage can be reused.
    out.signature=14695981039346656037ull;
    for (uint32_t n : {draw.caller,draw.mode,draw.index_type,draw.index_pointer,draw.index_buffer,
                      draw.position.pointer,draw.position.buffer,draw.position.type,uint32_t(draw.position.stride),
                      uint32_t(draw.position.components),uint32_t(draw.count),uint32_t(draw.first),draw.state.texture[0]}) hash(out.signature,n);
    hash(out.signature,draw.position.generation); hash(out.signature,draw.index_generation);
    hash(out.signature,draw.state.framebuffer);
    if (draw.count <= 0) return out;
    if (!layout(draw.position,2,4) || (!draw.indexed && draw.first < 0)) { out.bad_layout=true; out.rank=1e9; return out; }
    if (draw.indexed && draw.index_type != unsigned_byte && draw.index_type != unsigned_short && draw.index_type != unsigned_int) {
        out.unsupported=true; return out;
    }
    if (draw.indexed) {
        uint64_t bytes = uint64_t(draw.count) * width(draw.index_type);
        out.bad_index_span = bytes > draw.indices.bytes;
    }
    if (draw.mode != triangles && draw.mode != triangle_strip && draw.mode != triangle_fan) { out.unsupported=true; return out; }
    size_t count = std::min({size_t(draw.count),budget,max_draw_indices});
    out.limited = count < size_t(draw.count);
    std::array<Point,3> local{}, eye{}, clip{};
    std::array<uint32_t,3> ids{};
    size_t filled=0;
    for (size_t slot=0;slot<count;slot++) {
        uint32_t id;
        ++out.inspected;
        if (!index(draw,slot,id)) { out.unreadable=true; break; }
        out.min_index=std::min(out.min_index,id); out.max_index=std::max(out.max_index,id);
        Point p;
        if (!vertex(draw.position,id,p)) { out.unreadable=true; break; }
        unsigned bit=1;
        for(const Array* attribute:{&draw.normal,&draw.color,&draw.uv}) {
            if(attribute->enabled) {
                if(!layout(*attribute,1,4))out.bad_attribute_layouts|=bit;
                else {
                    size_t n=width(attribute->type)*attribute->components;
                    uint64_t stride=attribute->stride?uint64_t(attribute->stride):n;
                    uint8_t sample[16];
                    if(!read(attribute->source,uint64_t(id)*stride,sample,n))out.unreadable_attributes|=bit;
                }
            }
            bit<<=1;
        }
        hash(out.digest,id); hash(out.digest,p);
        Point e=transform(draw.state.modelview,p), c=transform(draw.state.projection,e);
        if (!finite(p) || !finite(e) || !finite(c)) {
            out.nonfinite=true;
            // A fan cannot acquire a different anchor after a malformed vertex.
            if(draw.mode==triangle_fan && slot==0)break;
            filled=draw.mode==triangle_fan?1:0;
            continue;
        }
        bounds(out,p,e);
        if (draw.mode == triangles) {
            size_t k=slot%3;local[k]=p;eye[k]=e;clip[k]=c;ids[k]=id;
            if (k!=2 || filled<2) { ++filled;continue; }
            filled=0;
        } else {
            if (filled<2) {local[filled]=p;eye[filled]=e;clip[filled]=c;ids[filled]=id;++filled;continue;}
            local[2]=p;eye[2]=e;clip[2]=c;ids[2]=id;
        }
        Triangle tri;
        tri.valid=true;tri.number=uint32_t(draw.mode==triangles?slot/3:slot-2);
        tri.local=local;tri.eye=eye;tri.clip=clip;tri.indices=ids;
        if(draw.mode==triangle_strip && (tri.number&1)) {
            std::swap(tri.local[0],tri.local[1]);std::swap(tri.eye[0],tri.eye[1]);
            std::swap(tri.clip[0],tri.clip[1]);std::swap(tri.indices[0],tri.indices[1]);
        }
        ++out.triangle_count;
        if (ids[0]==ids[1] || ids[0]==ids[2] || ids[1]==ids[2]) { ++out.degenerate;tri.valid=false; }
        if (tri.valid) {
            // Non-unit vertex w is retained in the sample. Do not pretend these
            // affine-space edge metrics are meaningful for projective vertices.
            if (e[3]!=1 || eye[0][3]!=1 || eye[1][3]!=1) out.unsupported=true;
            else {
                tri.edge=std::max({distance(eye[0],eye[1]),distance(eye[1],eye[2]),distance(eye[2],eye[0])});
                double ax=eye[1][0]-eye[0][0],ay=eye[1][1]-eye[0][1],az=eye[1][2]-eye[0][2];
                double bx=eye[2][0]-eye[0][0],by=eye[2][1]-eye[0][1],bz=eye[2][2]-eye[0][2];
                double area2=std::hypot(ay*bz-az*by,az*bx-ax*bz,ax*by-ay*bx);
                if (!area2) ++out.degenerate;
                tri.aspect=std::min(1e12,tri.edge*tri.edge/std::max(area2,1e-30));
                if (!out.longest.valid || tri.edge>out.longest.edge) out.longest=tri;
                if (area2>0 && (!out.thinnest.valid || tri.aspect>out.thinnest.aspect)) out.thinnest=tri;
            }
        }
        if (draw.mode!=triangles) {
            size_t keep=draw.mode==triangle_fan?0:1;
            if (keep) {local[0]=local[1];eye[0]=eye[1];clip[0]=clip[1];ids[0]=ids[1];}
            local[1]=local[2];eye[1]=eye[2];clip[1]=clip[2];ids[1]=ids[2];
        }
    }
    double edge=out.longest.edge,aspect=out.thinnest.aspect;
    out.rank=std::log1p(edge)*std::log1p(std::min(aspect,1e6));
    if (out.unreadable || out.nonfinite || out.bad_index_span || out.unreadable_attributes || out.bad_attribute_layouts) out.rank+=1e9;
    return out;
}
std::string flags(const Summary& out) {
    std::string text;
    for (auto [value,name] : {std::pair{out.bad_layout,"layout"}, {out.bad_index_span,"index-span"},
            {out.unreadable,"unreadable"},{out.nonfinite,"nonfinite"},{out.limited,"partial-scan"},{out.unsupported,"unverified-semantics"},
            {out.unreadable_attributes!=0,"attribute-read"},{out.bad_attribute_layouts!=0,"attribute-layout"}})
        if (value) {if (!text.empty()) text+=',';text+=name;}
    return text.empty()?"no-checked-error":text;
}
bool screen_edge(Point a, Point b, const State& state, int width, int height, float out[4]) {
    if (!finite(a)||!finite(b)||width<=0||height<=0||state.viewport[2]<=0||state.viewport[3]<=0) return false;
    double low=0,high=1;
    for (int plane=0;plane<7;plane++) {
        auto test=[&](const Point& p) { return plane==6?p[3]-1e-8:p[3]+(plane%2?-1:1)*p[plane/2]; };
        double x=test(a),y=test(b);
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        if (x<0&&y<0)return false;
        if (x<0)low=std::max(low,x/(x-y));
        if (y<0)high=std::min(high,x/(x-y));
        if(low>high)return false;
    }
    Point start=a;
    for(int k=0;k<4;k++){a[k]=start[k]+low*(b[k]-start[k]);}
    for(int k=0;k<4;k++){b[k]=start[k]+high*(b[k]-start[k]);}
    for(int i=0;i<2;i++){
        const auto& p=i?b:a;
        double x=state.viewport[0]+(p[0]/p[3]*0.5+0.5)*state.viewport[2];
        double y=state.viewport[1]+(p[1]/p[3]*0.5+0.5)*state.viewport[3];
        if(!std::isfinite(x)||!std::isfinite(y))return false;
        out[i*2]=float(std::clamp(x,0.0,double(width)));
        out[i*2+1]=float(std::clamp(double(height)-y,0.0,double(height)));
    }
    return true;
}
}  // namespace geometry
