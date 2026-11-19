#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace attribution {
constexpr uint32_t hle_base = 16, hle_capacity = 4096, bucket_count = hle_base + hle_capacity + 1;
enum Bucket : uint32_t { other, guest_run, fixture_scan, texture_lookup, texture_readback,
    texture_mask, mesh_sampling, fixture_merge, jit_setup, glow_analysis, fan_analysis,
    beacon_analysis, light_setup, open_direction, texture_copy };
struct Counter { uint64_t ns=0, calls=0, max_ns=0, max_frame=0; };
// Nesting-aware partition. No allocations or locks on the instrumented path.
class Clock {
public:
    void begin(uint64_t now) { last_=now;current_=other;depth_=0; }
    uint32_t enter(uint32_t id,uint64_t now);
    void leave(uint32_t previous,uint64_t now);
    void end(uint64_t now);
    std::array<Counter,bucket_count> counters{};
    uint64_t errors=0;
private:
    void charge(uint64_t now);
    uint32_t current_=other,depth_=0;
    uint64_t last_=0;
};
extern thread_local bool collecting, hle_selected;
bool configure(const std::string& mode,bool geometry); // startup only; off/sampled/full
void register_hle(uint32_t id,const std::string& name);
void frame_begin();
void frame_end();
void report(bool marker);
void fixture_work(uint64_t key,uint32_t vertices,uint32_t triangles);
class Scope {
public:
    explicit Scope(uint32_t bucket);
    ~Scope();
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
private:
    uint32_t previous_=0,bucket_=0;
    uint64_t start_=0;
    bool live_=false;
    uint64_t tag_=0;
};
// Metadata only: no source vertex/index/texture payload is retained or dumped.
struct Draw {
    uint64_t serial=0,scene=0;
    uint32_t source=0,mode=0,count=0,lo=0,hi=0,flags=0,vertices_checked=0,triangles_checked=0;
    std::array<float,16> transform{};
    std::array<float,3> local_lo{},local_hi{},world_lo{},world_hi{};
    float max_edge=0;
};
bool geometry_enabled();
void geometry_draw(Draw draw,const float* vertices,uint32_t vertex_count,unsigned stride,
                   const uint16_t* indices,size_t count);
}  // namespace attribution
