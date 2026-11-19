#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace geometry {
constexpr std::array<float,16> identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
// The reader must bounds-check every request. No GL readbacks or raw payloads
// are retained. The guest/VBO readers and synthetic tests share this interface.
struct Reader {
    const void* context=nullptr;
    bool (*read)(const void*,uint64_t,void*,size_t)=nullptr;
    bool get(uint64_t offset,void* out,size_t size) const { return read && read(context,offset,out,size); }
};
struct Array {
    Reader storage;
    uint32_t pointer=0,buffer=0,type=0x1406;
    int32_t components=3,stride=0;
    bool enabled=false;
};
struct State {
    std::array<float,16> modelview=identity,projection=identity;
    std::array<float,4> color{1,1,1,1};
    std::array<int32_t,4> viewport{};
    uint32_t flags=0,blend_src=1,blend_dst=0,blend_alpha_src=1,blend_alpha_dst=0;
};
struct Submission {
    Array vertices,colors;
    Reader indices;
    State state;
    uint32_t mode=4,count=0,first=0,index_type=0x1403,index_pointer=0,element_buffer=0;
    uint32_t texture=0,return_address=0;
    bool native=false,indexed=false;
};
enum Flag : uint32_t { nonfinite=1, unreadable=2, long_edge=4, unsupported=8,
    zero_w=16, thin_triangle=32, partial=64 };
struct Budget {
    uint32_t vertices=8192,draws=2048;
    uint32_t per_draw_vertices=96,sample_phase=0;
};
struct Record {
    uint64_t serial=0,scene=0,frame=0;
    uint32_t source=0,vertex_buffer=0,texture=0,return_address=0,mode=0,count=0,first=0;
    uint32_t index_type=0,index_pointer=0,element_buffer=0,vertex_type=0,components=0,stride=0;
    uint32_t color_type=0,color_components=0,color_pointer=0,color_buffer=0;
    uint32_t flags=0,vertices_checked=0,indices_checked=0,primitives_checked=0,colors_checked=0;
    uint32_t lo=UINT32_MAX,hi=0,behind_eye=0;
    bool native=false,indexed=false,color_array=false,perspective=false;
    State state;
    std::array<float,3> local_lo{},local_hi{},eye_lo{},eye_hi{},ndc_lo{},ndc_hi{};
    std::array<float,4> color_mean{};
    float max_edge=0,max_local_edge=0,max_aspect=0,clip_w_lo=0,clip_w_hi=0;
};
// Samples complete point/line/triangle primitives across the draw, including
// late indices, without allocating buffers sized by untrusted draw counts.
Record analyze(const Submission& input,Budget& budget);
}  // namespace geometry
