#pragma once

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace hle::presentation {

// GL values, without a window/GL dependency: tests exercise this exact setup and
// transaction with a synthetic driver. The production adapter calls host GL.
constexpr uint32_t complete = 0x8CD5, incomplete_multisample = 0x8D56;
constexpr uint32_t rgba8 = 0x8058, rgb10_a2 = 0x8059;
constexpr uint32_t depth32f_stencil8 = 0x8CAD, depth24_stencil8 = 0x88F0;
struct Bindings { uint32_t draw = 0, read = 0, renderbuffer = 0; };
struct Target {
    uint32_t fbo = 0, color = 0, depth = 0, normal = 0, veil = 0;
    uint32_t depth_format = depth32f_stencil8;
    int samples = 0;
};
enum class Stage { color, depth, core, normal, veil, final };
struct Result {
    bool accepted = false;
    Stage stage = Stage::core;
    uint32_t status = 0;  // 0 means allocation/dimension failure, not a GL status
};
inline const char* stage_name(Stage stage) {
    switch (stage) {
    case Stage::color: return "color allocation";
    case Stage::depth: return "depth allocation";
    case Stage::core: return "color/depth";
    case Stage::normal: return "normals";
    case Stage::veil: return "fog coverage";
    default: return "complete target";
    }
}
template<class Gl> void release(Gl& gl, Target& target) {
    if (target.fbo) gl.delete_framebuffer(target.fbo);
    for (uint32_t id : {target.color, target.depth, target.normal, target.veil})
        if (id) gl.delete_renderbuffer(id);
    target = {};
}

// Fresh objects are essential: an old auxiliary attachment must not invalidate
// a core/normal completeness check while switching the requested sample count.
template<class Gl>
Result build(Gl& gl, Target& target, uint32_t texture, int width, int height,
             int requested, uint32_t depth_format, bool require_normal, bool require_veil) {
    target.depth_format = depth_format;
    target.fbo = gl.framebuffer();
    if (!target.fbo) return {false, Stage::core, 0};
    gl.bind_framebuffer(target.fbo);
    if (requested) {
        target.color = gl.renderbuffer(requested, rgba8, width, height);
        if (!target.color) return {false, Stage::color, 0};
        target.samples = gl.samples(target.color);
        if (target.samples < requested) return {false, Stage::color, incomplete_multisample};
        gl.attach_color(0, target.color);
    } else gl.attach_texture(texture);
    target.depth = gl.renderbuffer(target.samples, depth_format, width, height);
    if (!target.depth) return {false, Stage::depth, 0};
    if (gl.samples(target.depth) != target.samples) return {false, Stage::depth, incomplete_multisample};
    gl.attach_depth(target.depth);
    uint32_t status = gl.status();
    if (status != complete) return {false, Stage::core, status};

    auto auxiliary = [&](uint32_t& id, int attachment, uint32_t format, bool required, Stage stage) -> Result {
        id = gl.renderbuffer(target.samples, format, width, height);
        uint32_t st = 0;
        if (id && gl.samples(id) == target.samples) {
            gl.attach_color(attachment, id);
            st = gl.status();
            if (st == complete) return {true, stage, st};
        } else if (id) st = incomplete_multisample;
        gl.attach_color(attachment, 0);
        if (id) gl.delete_renderbuffer(id);
        id = 0;
        return {!required, stage, st};
    };
    Result normal = auxiliary(target.normal, 1, rgb10_a2, require_normal, Stage::normal);
    if (!normal.accepted) return normal;
    if (target.normal) {
        Result veil = auxiliary(target.veil, 2, rgba8, require_veil, Stage::veil);
        if (!veil.accepted) return veil;
    } else if (require_veil) return {false, Stage::veil, 0};
    status = gl.status();
    return {status == complete, Stage::final, status};
}

// A failed candidate never mutates/reallocates the live target. Preserve existing
// Native attachments on runtime changes instead of silently dropping effects.
// On initial setup only, optional unsupported targets retain the old fallback.
template<class Gl>
Result change(Gl& gl, Target& current, uint32_t texture, int width, int height,
              int requested, uint32_t depth_format) {
    Bindings saved = gl.bindings();
    Target candidate;
    Result result = build(gl, candidate, texture, width, height, requested, depth_format,
                          current.normal != 0, current.veil != 0);
    if (result.accepted) {
        std::swap(candidate, current);  // candidate now owns only the OLD objects
        if (candidate.fbo) {
            if (saved.draw == candidate.fbo) saved.draw = current.fbo;
            if (saved.read == candidate.fbo) saved.read = current.fbo;
            const uint32_t before[] = {candidate.color, candidate.depth, candidate.normal, candidate.veil};
            const uint32_t after[] = {current.color, current.depth, current.normal, current.veil};
            for (int i = 0; i < 4; ++i)
                if (before[i] && saved.renderbuffer == before[i]) saved.renderbuffer = after[i];
        }
    }
    release(gl, candidate);  // failed proposal, or the retired successful target
    gl.restore(saved);       // read/draw bindings can differ; do not merge them
    return result;
}

}  // namespace hle::presentation
