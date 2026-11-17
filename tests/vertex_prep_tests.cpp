#include "render/vertex_prep.h"
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace prep = render::vertex_prep;
namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
bool identical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
struct Input {
    std::vector<uint8_t> bytes;
    render::Array array;
    Input(unsigned int type, int components, uint32_t count, int padding, int misalign) {
        const int step = std::max(components, 1) * prep::type_size(type) + padding;
        bytes.resize(size_t(count) * step + misalign + 32, 0x6b);
        array = {true, components, type, padding ? step : 0, bytes.data() + misalign};
        std::mt19937 random(type * 31 + components);
        for (uint32_t i = 0; i < count; ++i) for (int k = 0; k < components; ++k) {
            uint8_t* p = bytes.data() + misalign + size_t(i) * step + k * prep::type_size(type);
            switch (type) {
            case prep::byte_type: case prep::ubyte_type: *p = uint8_t(random()); break;
            case prep::short_type: case prep::ushort_type: {
                uint16_t v = uint16_t(random()); std::memcpy(p, &v, 2); break;
            }
            case prep::fixed_type: {
                int32_t v = int32_t(random()); std::memcpy(p, &v, 4); break;
            }
            default: {
                // Include signed zero, subnormals, infinities and NaN payloads.
                constexpr uint32_t edge[] = {0, 0x80000000, 1, 0x80000001, 0x7f800000, 0xff800000,
                                            0x7fc12345, 0xffc12345, 0x7f812345, 0x3f800000};
                uint32_t bits = i < std::size(edge) ? edge[i] : random();
                std::memcpy(p, &bits, 4); break;
            }
            }
        }
    }
};

void vertex_equivalence() {
    std::vector<float> reference(1200, -33), optimized(3600, 12);
    const unsigned int types[] = {prep::byte_type, prep::ubyte_type, prep::short_type, prep::ushort_type,
                                  prep::fixed_type, 0x1406, 0x123456};
    for (auto type : types) for (int components = 0; components <= 6; ++components) {
        for (int padding : {0, 7}) for (int misalign : {0, 1, 3}) {
            Input input(type, components, 149, padding, misalign);
            const auto before = input.bytes;
            for (int slot = 0; slot < 3; ++slot) for (uint32_t n : {0u, 1u, 17u, 128u, 3u}) {
                std::array<render::Array, 3> arrays{};
                arrays[slot] = input.array;
                prep::vertices_reference(reference, 11, n, arrays[0], arrays[1], arrays[2]);
                prep::vertices_optimized(optimized, 11, n, arrays[0], arrays[1], arrays[2]);
                if (!identical(reference, optimized)) {
                    size_t mismatch = 0;
                    while (mismatch < reference.size() && std::bit_cast<uint32_t>(reference[mismatch]) == std::bit_cast<uint32_t>(optimized[mismatch])) ++mismatch;
                    std::cerr << "conversion mismatch type=" << type << " components=" << components << " padding=" << padding
                              << " misalign=" << misalign << " slot=" << slot << " n=" << n << " element=" << mismatch
                              << " ref_bits=" << std::hex << std::bit_cast<uint32_t>(reference[mismatch])
                              << " opt_bits=" << std::bit_cast<uint32_t>(optimized[mismatch]) << std::dec << '\n';
                    throw std::runtime_error("bit-exact conversion/default/format/stride/offset/growth equivalence");
                }
            }
            require(input.bytes == before, "input arrays remain read-only");
        }
    }
    Input position(prep::short_type, 3, 100, 9, 1), color(prep::ubyte_type, 4, 100, 0, 3), tex(0x1406, 2, 100, 7, 1);
    for (int pass = 0; pass < 20; ++pass) {
        position.bytes[9 + pass] ^= uint8_t(pass + 1);  // same pointer, changing contents
        color.array.enabled = pass % 3 != 0;
        render::Array t = tex.array;
        if (pass % 4 == 0) t.data = nullptr;
        prep::vertices_reference(reference, 5, 79, position.array, color.array, t);
        prep::vertices_optimized(optimized, 5, 79, position.array, color.array, t);
        require(identical(reference, optimized), "mixed arrays, disabled/null inputs and changed data are not cached");
    }
    render::Array invalid_count = position.array;
    invalid_count.size = -1; invalid_count.stride = 8;
    prep::vertices_reference(reference, 0, 7, invalid_count, {}, {});
    prep::vertices_optimized(optimized, 0, 7, invalid_count, {}, {});
    require(identical(reference, optimized), "negative component count retains defaults without reads");
    std::array<int16_t, 3> p = {-2, 3, 4};
    std::array<uint8_t, 4> c = {0, 255, 128, 1};
    prep::vertices_optimized(optimized, 0, 1, {true, 3, prep::short_type, 0, reinterpret_cast<const uint8_t*>(p.data())},
                             {true, 4, prep::ubyte_type, 0, c.data()}, {});
    require(optimized[0] == -2 && optimized[1] == 3 && optimized[2] == 4 && optimized[3] == 1, "golden position/w defaults");
    require(optimized[4] == 0 && optimized[5] == 1 && optimized[6] == 128 * (1.0f / 255) && optimized[11] == 1, "golden ubyte color/texture defaults");
    require(optimized[12] == 0 && optimized[13] == 0 && optimized[14] == 0, "normals initialized every draw");
}

void compare_normals(const std::vector<float>& input, const std::vector<uint16_t>& indices, uint32_t n) {
    auto a = input, b = input;
    std::vector<uint32_t> slots_a, refs, slots_b, offsets;
    for (int pass = 0; pass < 2; ++pass) {
        prep::normals_reference(a, indices, n, slots_a, refs);
        prep::normals_optimized(b, indices, n, slots_b, offsets);
        require(identical(a, b), "bit-exact normals, complete vertex records and repeated accumulation");
        require(slots_a == slots_b, "identical raw-position hash slots/probing");
        require(refs.size() == offsets.size(), "representative scratch count");
        for (size_t i = 0; i < refs.size(); ++i) require(refs[i] * prep::stride + 12 == offsets[i], "identical weld representative, stored as normal offset");
    }
}

void normal_equivalence() {
    compare_normals({}, {}, 0);
    compare_normals({}, {0, 0, 0, 1}, 0);
    std::mt19937 random(0x123abc);
    for (uint32_t n : {1u, 2u, 3u, 7u, 17u, 128u, 1024u, 65535u}) {
        std::vector<float> input(size_t(n) * prep::stride, 0);
        for (uint32_t i = 0; i < n; ++i) {
            float* v = input.data() + size_t(i) * prep::stride;
            for (int k = 0; k < 12; ++k) v[k] = float(int(random() % 53) - 26) / 8;
            if (i % 3 == 1) std::memcpy(v, v - prep::stride, 12);  // texture seam, same raw position
        }
        std::vector<uint16_t> idx;
        for (uint32_t t = 0; t < n * 3 + 11; ++t) idx.push_back(uint16_t(random() % (n + 4)));
        compare_normals(input, idx, n);  // invalid indices and incomplete tails included
        if (n < 1024) {
            // Preserve exact-bit welding for +/- zero and equal/unequal NaN payloads,
            // and the original arithmetic's behavior for non-finite positions.
            constexpr uint32_t bits[] = {0, 0x80000000, 0x7fc12345, 0x7fc12346, 0x7f800000, 0xff800000, 0x7f812345};
            for (uint32_t i = 0; i < n; ++i) {
                for (int k = 0; k < 3; ++k) input[size_t(i) * prep::stride + k] = std::bit_cast<float>(bits[(i / 2 + k) % std::size(bits)]);
            }
            compare_normals(input, idx, n);
        }
    }
    // Two triangles share a seam position, but use different other vertices.
    std::vector<float> seam(6 * prep::stride, 0);
    const float positions[6][3] = {{0,0,0},{1,0,0},{0,1,0},{0,0,0},{0,0,1},{1,0,0}};
    for (int i = 0; i < 6; ++i) std::memcpy(seam.data() + i * prep::stride, positions[i], 12);
    std::vector<uint32_t> slots, offsets;
    prep::normals_optimized(seam, {0,1,2,3,4,5}, 6, slots, offsets);
    require(seam[12] == 0 && seam[13] == 1 && seam[14] == 1, "golden area-weighted seam sum");
    require(std::memcmp(&seam[12], &seam[3 * prep::stride + 12], 12) == 0, "golden seam normals copied exactly");
    compare_normals(seam, {2,1,0,3,5,4,0,0,0}, 6);  // reverse winding and degenerate triangle
    {
        auto extreme = seam;
        for (size_t i = 0; i < 6; ++i) {
            extreme[i * prep::stride] = i % 2 ? std::numeric_limits<float>::max() : -std::numeric_limits<float>::max();
            extreme[i * prep::stride + 1] = i % 3 ? std::numeric_limits<float>::min() : std::numeric_limits<float>::max();
        }
        compare_normals(extreme, {0,1,2,3,4,5}, 6);  // overflowing original arithmetic is not clamped/hidden
    }
    {
        // Exercise both hash-table growth/shrink and changed positions/topology
        // through the same scratch allocations, as with moving/skinned draws.
        std::vector<uint32_t> sa, ra, sb, rb;
        for (uint32_t pass = 0; pass < 40; ++pass) {
            const uint32_t n = pass % 3 ? 157 : 9;
            std::vector<float> a(size_t(n) * prep::stride, 0);
            for (uint32_t i = 0; i < n; ++i) for (uint32_t k = 0; k < 3; ++k)
                a[size_t(i) * prep::stride + k] = float((i / (pass % 4 + 1) + k * pass) % 17);
            auto b = a;
            std::vector<uint16_t> topology;
            for (uint32_t t = 0; t < n * 6; ++t) topology.push_back(uint16_t((t * 7 + pass) % n));
            prep::normals_reference(a, topology, n, sa, ra);
            prep::normals_optimized(b, topology, n, sb, rb);
            require(identical(a, b) && sa == sb, "scratch reuse cannot retain stale positions/topology/welds");
        }
    }
}

#if defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline, noclone))
#else
#define BENCH_NOINLINE
#endif
volatile float sink = 0;
using VertexFn = decltype(&prep::vertices_reference);
BENCH_NOINLINE double vertex_time(VertexFn fn, const render::Array& p, const render::Array& c, const render::Array& t, uint32_t n) {
    std::vector<float> out;
    fn(out, 0, n, p, c, t);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 4000; ++i) fn(out, 0, n, p, c, t);
    sink = out[0];
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
using NormalFn = decltype(&prep::normals_reference);
BENCH_NOINLINE double normal_time(NormalFn fn, const std::vector<float>& input, const std::vector<uint16_t>& indices, uint32_t n) {
    auto out = input;
    std::vector<uint32_t> slots, refs;
    fn(out, indices, n, slots, refs);
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 4000; ++i) {
        // Exactly the same per-draw reset for both functions, not a cache reuse test.
        out = input;
        fn(out, indices, n, slots, refs);
    }
    sink = out[12];
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void benchmark() {
    for (unsigned int type : {prep::short_type, 0x1406u}) {
        Input p(type, 3, 1024, 0, 1), c(prep::ubyte_type, 4, 1024, 0, 1), t(type, 2, 1024, 0, 1);
        const double ref = vertex_time(prep::vertices_reference, p.array, c.array, t.array, 1024);
        const double opt = vertex_time(prep::vertices_optimized, p.array, c.array, t.array, 1024);
        std::cout << "synthetic vertices type=" << type << " reference_ms=" << ref << " optimized_ms=" << opt << " ratio=" << opt/ref << '\n';
    }
    constexpr uint32_t n = 1024;
    std::vector<float> input(n * prep::stride, 0);
    for (uint32_t i = 0; i < n; ++i) {
        input[size_t(i) * prep::stride] = float((i / 2) % 32);
        input[size_t(i) * prep::stride + 1] = float((i / 2) / 32);
        input[size_t(i) * prep::stride + 2] = float(i % 7) * 0.125f;
    }
    std::vector<uint16_t> indices;
    for (uint32_t i = 0; i < n - 34; ++i) {
        for (uint32_t v : {i, i+1, i+32, i+1, i+33, i+32}) indices.push_back(uint16_t(v));
    }
    const double ref = normal_time(prep::normals_reference, input, indices, n);
    const double opt = normal_time(prep::normals_optimized, input, indices, n);
    std::cout << "synthetic normals reference_ms=" << ref << " optimized_ms=" << opt << " ratio=" << opt/ref << '\n';
    std::cout << "Synthetic workload only; no gameplay CPU/FPS claim.\n";
}
}

int main(int argc, char** argv) {
    try {
        vertex_equivalence(); normal_equivalence();
        std::cout << "Native CPU preparation equivalence passed; no game executed\n";
        if (argc == 2 && std::string(argv[1]) == "--benchmark") benchmark();
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
