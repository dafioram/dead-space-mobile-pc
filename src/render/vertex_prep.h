#pragma once

#include "vertex_array.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace render::vertex_prep {
constexpr int stride = 15;  // pos4, color4, tex4, normal3
constexpr unsigned int byte_type = 0x1400, ubyte_type = 0x1401;
constexpr unsigned int short_type = 0x1402, ushort_type = 0x1403;
constexpr unsigned int fixed_type = 0x140C;

inline float read_component(const uint8_t* e, unsigned int type, int k) {
    switch (type) {
    case byte_type: return float(static_cast<const signed char*>(static_cast<const void*>(e))[k]);
    case ubyte_type: return float(e[k]);
    case short_type: { int16_t v; std::memcpy(&v, e + 2 * k, 2); return v; }
    case ushort_type: { uint16_t v; std::memcpy(&v, e + 2 * k, 2); return v; }
    case fixed_type: { int32_t v; std::memcpy(&v, e + 4 * k, 4); return v / 65536.0f; }
    default: { float v; std::memcpy(&v, e + 4 * k, 4); return v; }
    }
}
inline int type_size(unsigned int type) {
    switch (type) {
    case byte_type: case ubyte_type: return 1;
    case short_type: case ushort_type: return 2;
    default: return 4;
    }
}

// Original calculations retained for the same-EXE A/B control and equivalence
// tests. Inputs are the current draw's arrays, never pointer-keyed cached data.
inline void vertices_reference(std::vector<float>& verts, uint32_t lo, uint32_t n,
                               const Array& position, const Array& color, const Array& tex) {
    verts.assign(size_t(n) * stride, 0.0f);
    for (uint32_t i = 0; i < n; i++) {
        float* v = &verts[size_t(i) * stride];
        v[3] = 1;
        v[4] = v[5] = v[6] = v[7] = 1;
        v[8] = v[9] = 0, v[10] = 0, v[11] = 1;
    }
    struct Slot { const Array* array; int off, max; bool color; };
    const Slot slots[] = {{&position, 0, 4, false}, {&color, 4, 4, true}, {&tex, 8, 4, false}};
    for (auto& s : slots) {
        const Array& a = *s.array;
        if (!a.enabled || !a.data) continue;
        int step = a.stride ? a.stride : a.size * type_size(a.type);
        int cnt = std::min(a.size, s.max);
        bool norm_color = s.color && a.type == ubyte_type;
        for (uint32_t i = 0; i < n; i++) {
            const uint8_t* e = a.data + size_t(lo + i) * step;
            float* v = &verts[size_t(i) * stride + s.off];
            for (int k = 0; k < cnt; k++) v[k] = read_component(e, a.type, k) * (norm_color ? 1.0f / 255.0f : 1.0f);
        }
    }
}

template<class T, int Components, bool Fixed = false>
inline void copy_attribute(float* output, const Array& array, uint32_t lo, uint32_t n, int step, float scale) {
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t* input = array.data + size_t(lo + i) * step;
        for (int k = 0; k < Components; ++k) {
            T value;
            std::memcpy(&value, input + k * sizeof(T), sizeof(T));  // unaligned arrays are legal
            float f = float(value);
            if constexpr (Fixed) f = f / 65536.0f;
            output[k] = f * scale;
        }
        output += stride;
    }
}

template<class T, bool Normalize = false, bool Fixed = false>
inline void copy_components(float* output, const Array& array, uint32_t lo, uint32_t n, int step,
                            float scale = Normalize ? 1.0f / 255.0f : 1.0f) {
    switch (std::min(array.size, 4)) {
    case 1: copy_attribute<T, 1, Fixed>(output, array, lo, n, step, scale); break;
    case 2: copy_attribute<T, 2, Fixed>(output, array, lo, n, step, scale); break;
    case 3: copy_attribute<T, 3, Fixed>(output, array, lo, n, step, scale); break;
    case 4: copy_attribute<T, 4, Fixed>(output, array, lo, n, step, scale); break;
    default: break;  // zero/negative component counts did not read anything in the original loop
    }
}

inline void attribute(float* output, const Array& array, uint32_t lo, uint32_t n, bool color) {
    if (!array.enabled || !array.data) return;
    const int step = array.stride ? array.stride : array.size * type_size(array.type);
    // Dispatch once per ARRAY, not for every component of every vertex. Keep the
    // original float fallback for formats not explicitly handled by the old code.
    switch (array.type) {
    case byte_type: copy_components<int8_t>(output, array, lo, n, step); break;
    case ubyte_type:
        if (color) copy_components<uint8_t, true>(output, array, lo, n, step);
        else copy_components<uint8_t>(output, array, lo, n, step);
        break;
    case short_type: copy_components<int16_t>(output, array, lo, n, step); break;
    case ushort_type: copy_components<uint16_t>(output, array, lo, n, step); break;
    case fixed_type: copy_components<int32_t, false, true>(output, array, lo, n, step); break;
    default: {
        // Keep the original arithmetic, including signalling-NaN propagation,
        // rather than silently turning float conversion into a byte copy. One
        // volatile read per float array prevents folding away its multiply.
        const volatile float identity = 1.0f;
        copy_components<float>(output, array, lo, n, step, identity);
        break;
    }
    }
}

inline void vertices_optimized(std::vector<float>& verts, uint32_t lo, uint32_t n,
                               const Array& position, const Array& color, const Array& tex) {
    verts.resize(size_t(n) * stride);
    constexpr float defaults[stride] = {0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0};
    for (uint32_t i = 0; i < n; ++i) std::memcpy(verts.data() + size_t(i) * stride, defaults, sizeof(defaults));
    if (!n) return;  // do not form offset pointers into an empty vector
    attribute(verts.data(), position, lo, n, false);
    attribute(verts.data() + 4, color, lo, n, true);
    attribute(verts.data() + 8, tex, lo, n, false);
}

inline void normals_reference(std::vector<float>& verts, const std::vector<uint16_t>& idx, uint32_t n,
                              std::vector<uint32_t>& weld_slot, std::vector<uint32_t>& weld_rep) {
    uint32_t cap = 16;
    while (cap < n * 2) cap <<= 1;
    weld_slot.assign(cap, 0xFFFFFFFFu);
    weld_rep.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        const float* p = &verts[size_t(i) * stride];
        uint32_t b[3];
        std::memcpy(b, p, 12);
        uint32_t h = (b[0] * 0x9E3779B1u) ^ (b[1] * 0x85EBCA77u) ^ (b[2] * 0xC2B2AE3Du);
        h ^= h >> 15;
        for (uint32_t s = h & (cap - 1);; s = (s + 1) & (cap - 1)) {
            uint32_t j = weld_slot[s];
            if (j == 0xFFFFFFFFu) {
                weld_slot[s] = i;
                weld_rep[i] = i;
                break;
            }
            if (std::memcmp(&verts[size_t(j) * stride], p, 12) == 0) {
                weld_rep[i] = j;
                break;
            }
        }
    }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        if (i0 >= n || i1 >= n || i2 >= n) continue;
        const float *a = &verts[size_t(i0) * stride], *b = &verts[size_t(i1) * stride], *c = &verts[size_t(i2) * stride];
        float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (uint32_t v : {i0, i1, i2}) {
            float* d = &verts[size_t(weld_rep[v]) * stride + 12];
            d[0] += fn[0], d[1] += fn[1], d[2] += fn[2];
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        uint32_t r = weld_rep[i];
        if (r != i) std::memcpy(&verts[size_t(i) * stride + 12], &verts[size_t(r) * stride + 12], 12);
    }
}

// The caller's indexed draw range check bounds n to 65535, so a float offset
// fits uint32_t. Precompute that offset once per vertex, not per triangle corner.
inline void normals_optimized(std::vector<float>& verts, const std::vector<uint16_t>& idx, uint32_t n,
                              std::vector<uint32_t>& weld_slot, std::vector<uint32_t>& normal_offsets) {
    uint32_t cap = 16;
    while (cap < n * 2) cap <<= 1;
    weld_slot.assign(cap, 0xFFFFFFFFu);
    normal_offsets.resize(n);
    float* const base = verts.data();
    uint32_t* const slots = weld_slot.data();
    uint32_t* const offsets = normal_offsets.data();
    for (uint32_t i = 0; i < n; ++i) {
        const float* p = base + size_t(i) * stride;
        uint32_t bits[3];
        uint64_t prefix;
        std::memcpy(bits, p, 12);
        std::memcpy(&prefix, p, 8);
        uint32_t h = (bits[0] * 0x9E3779B1u) ^ (bits[1] * 0x85EBCA77u) ^ (bits[2] * 0xC2B2AE3Du);
        h ^= h >> 15;
        for (uint32_t s = h & (cap - 1);; s = (s + 1) & (cap - 1)) {
            const uint32_t j = slots[s];
            if (j == 0xFFFFFFFFu) {
                slots[s] = i;
                offsets[i] = i * stride + 12;
                break;
            }
            const float* candidate = base + size_t(j) * stride;
            uint64_t candidate_prefix;
            std::memcpy(&candidate_prefix, candidate, 8);
            if (candidate_prefix == prefix) {
                uint32_t candidate_tail;
                std::memcpy(&candidate_tail, candidate + 2, 4);
                if (candidate_tail == bits[2]) {
                    offsets[i] = j * stride + 12;
                    break;
                }
            }
        }
    }
    for (size_t t = 0; t + 2 < idx.size(); t += 3) {
        const uint32_t i0 = idx[t], i1 = idx[t + 1], i2 = idx[t + 2];
        if (i0 >= n || i1 >= n || i2 >= n) continue;
        const float *a = base + size_t(i0) * stride, *b = base + size_t(i1) * stride, *c = base + size_t(i2) * stride;
        const float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        // Preserve vertex order, including repeated/welded representatives. No
        // reassociation, degenerate-triangle skipping, or parallel scatter adds.
        for (uint32_t v : {i0, i1, i2}) {
            float* d = base + offsets[v];
            d[0] += fn[0], d[1] += fn[1], d[2] += fn[2];
        }
    }
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t offset = offsets[i];
        if (offset != i * stride + 12) std::memcpy(base + size_t(i) * stride + 12, base + offset, 12);
    }
}
}  // namespace render::vertex_prep
