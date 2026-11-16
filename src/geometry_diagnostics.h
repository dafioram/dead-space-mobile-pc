// Read-only inspection of submitted GLES data. No GL, SDL or retail-code dependency.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace geometry {
constexpr uint32_t triangles = 0x0004, triangle_strip = 0x0005, triangle_fan = 0x0006;
constexpr uint32_t byte = 0x1400, unsigned_byte = 0x1401, short_type = 0x1402;
constexpr uint32_t unsigned_short = 0x1403, unsigned_int = 0x1405, float_type = 0x1406, fixed = 0x140c;
constexpr size_t max_draw_indices = 32768, max_frame_indices = 196608;

struct Source {
    const uint8_t* data = nullptr;
    uint64_t bytes = 0;
    uint32_t guest_address = 0;
    bool guest = false;
    bool (*mapped)(uint32_t address) = nullptr;
};
struct ArrayInfo {
    bool enabled = false;
    int components = 0, stride = 0;
    uint32_t type = 0, pointer = 0, buffer = 0;
    uint64_t generation = 0;
};
struct Array : ArrayInfo { Source source; };
struct State {
    std::array<float, 16> modelview{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::array<float, 16> projection = modelview;
    std::array<int, 4> viewport{};
    uint32_t texture[2]{}, blend_src = 1, blend_dst = 0, depth_func = 0x201;
    uint32_t framebuffer = 0;  // guest target; only 0 has a direct game-screen overlay
    bool texture_enabled[2]{};
    uint32_t texture_env[2]{};
    std::array<float,4> color{1,1,1,1};
    std::array<int,4> scissor{};
    uint32_t cull_mode = 0x405, front_face = 0x901, alpha_func = 0x207;
    bool native = false, blend = false, depth_test = false, depth_write = true;
    bool cull = false, alpha_test = false, fog = false;
    bool scissor_on = false, polygon_offset = false, lighting = false;
    float alpha_ref = 0;
    float polygon_factor = 0, polygon_units = 0;
};
struct Draw {
    uint32_t caller = 0, mode = triangles, index_type = 0, index_pointer = 0, index_buffer = 0;
    uint64_t index_generation = 0;
    int count = 0, first = 0;
    bool indexed = false;
    Source indices;
    Array position, normal, color, uv;
    State state;
};
using Point = std::array<double, 4>;
struct Triangle {
    bool valid = false;
    uint32_t number = 0;
    std::array<uint32_t, 3> indices{};
    std::array<Point, 3> local{}, eye{}, clip{};
    double edge = 0, aspect = 0;
};
struct Summary {
    uint64_t frame = 0, scene = 0, ordinal = 0;
    uint64_t signature = 0, digest = 14695981039346656037ull;
    uint32_t caller = 0, mode = 0, index_type = 0, index_pointer = 0, index_buffer = 0;
    uint64_t index_generation = 0;
    ArrayInfo position, normal, color, uv;
    State state;
    int count = 0, first = 0;
    bool indexed = false, limited = false, unsupported = false, bad_layout = false;
    bool bad_index_span = false, unreadable = false, nonfinite = false, have_bounds = false;
    uint32_t min_index = UINT32_MAX, max_index = 0, inspected = 0, triangle_count = 0, degenerate = 0;
    uint32_t unreadable_attributes = 0, bad_attribute_layouts = 0;
    std::array<double, 3> local_min{}, local_max{}, eye_min{}, eye_max{};
    Triangle longest, thinnest;
    double rank = 0;
};

bool read(const Source& source, uint64_t offset, void* output, size_t size);
Summary inspect(const Draw& draw, size_t budget = max_draw_indices);
std::string flags(const Summary& summary);
// Clips homogeneous edges against all six clip planes before dividing by w.
bool screen_edge(Point a, Point b, const State& state, int width, int height, float out[4]);
}  // namespace geometry
