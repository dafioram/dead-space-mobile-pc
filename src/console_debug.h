// Asset-free parsing/policy for the opt-in console's resource and inspection tools.
#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace console_debug {

enum class Kind { none, credits, nodes, rich, restore, enemies };
struct Request {
    Kind kind = Kind::none;
    uint32_t credits = 0, nodes = 0;
    float radius = 30;
    std::string error;
    bool mutates() const { return kind != Kind::none && kind != Kind::enemies; }
};

// Debug bound, not a claim about the retail wallet cap. Keep values representable
// by both unsigned inventory fields and signed native UI/count calculations.
constexpr uint32_t kMaxResource = uint32_t(std::numeric_limits<int32_t>::max());
constexpr size_t kEnemyRows = 10;

inline bool valid_radius(float radius) { return std::isfinite(radius) && radius >= 0.1f && radius <= 500; }

inline bool resource_value(std::string_view text, uint32_t& value) {
    if (text.empty()) return false;
    for (char c : text)
        if (c < '0' || c > '9') return false;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value <= kMaxResource;
}

// Input has already passed the console's case/space-to-underscore normalization.
inline Request parse(std::string_view line) {
    std::vector<std::string_view> words;
    for (size_t start = 0;;) {
        size_t end = line.find('_', start);
        words.push_back(line.substr(start, end == line.npos ? end : end - start));
        if (end == line.npos) break;
        start = end + 1;
    }
    Request request;
    const auto name = words.front();
    if (name == "/credits") request.kind = Kind::credits;
    else if (name == "/nodes") request.kind = Kind::nodes;
    else if (name == "/rich") request.kind = Kind::rich;
    else if (name == "/restore") request.kind = Kind::restore;
    else if (name == "/enemies") request.kind = Kind::enemies;
    else return request;

    switch (request.kind) {
    case Kind::credits:
    case Kind::nodes: {
        uint32_t value = 0;
        if (words.size() != 2 || !resource_value(words[1], value)) {
            request.error = "usage: " + std::string(name) + " AMOUNT (integer 0..2147483647; sets, never adds)";
        } else if (request.kind == Kind::credits) request.credits = value;
        else request.nodes = value;
        break;
    }
    case Kind::rich:
        if (words.size() == 1) {
            request.credits = 500000;
            request.nodes = 200;
        } else if (words.size() != 3 || !resource_value(words[1], request.credits) ||
                   !resource_value(words[2], request.nodes)) {
            request.error = "usage: /rich [CREDITS NODES] (integers 0..2147483647; sets both balances)";
        }
        break;
    case Kind::restore:
        if (words.size() != 1) request.error = "usage: /restore (no arguments)";
        break;
    case Kind::enemies:
        if (words.size() == 2 && !words[1].empty()) {
            auto text = words[1];
            auto result = std::from_chars(text.data(), text.data() + text.size(), request.radius);
            if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) request.radius = 0;
        } else if (words.size() != 1) request.radius = 0;
        if (!valid_radius(request.radius))
            request.error = "usage: /enemies [RADIUS] (0.1..500 metres; default 30; read-only)";
        break;
    default: break;
    }
    return request;
}

struct Wallet { uint32_t credits, nodes; };
inline Wallet set_resources(const Request& request, Wallet before) {
    if (!request.error.empty()) return before;
    if (request.kind == Kind::credits || request.kind == Kind::rich) before.credits = request.credits;
    if (request.kind == Kind::nodes || request.kind == Kind::rich) before.nodes = request.nodes;
    return before;
}

inline bool same_context(uint32_t player, uint32_t world, uint32_t epoch,
                         uint32_t current_player, uint32_t current_world, uint32_t current_epoch) {
    return player && world && player == current_player && world == current_world && epoch == current_epoch;
}

enum class Execution { ready, stale, busy };
inline Execution execution(const Request& request, bool same, bool mutation_allowed) {
    if (!same) return Execution::stale;
    return request.mutates() && !mutation_allowed ? Execution::busy : Execution::ready;
}

inline bool valid_restore_limits(float health, float stasis) {
    return std::isfinite(health) && health > 0 && std::isfinite(stasis) && stasis >= 0;
}

inline bool enemy_distance(float px, float py, float pz, float x, float y, float z, float radius, double& distance) {
    if (!std::isfinite(radius) || radius <= 0 || !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(pz) ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
    distance = std::hypot(double(x) - px, double(y) - py, double(z) - pz);
    return distance <= radius;
}

using Output = void (*)(const std::string&);
// Queues onto the live player update. Rechecks identity, epoch and ownership
// before native access; never stores a guest object pointer for a later session.
bool submit(const Request& request, Output output, std::string& error);

}  // namespace console_debug
