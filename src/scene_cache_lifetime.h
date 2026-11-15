// Host observations are not ownership. Only native construction starts a
// lifetime; looking up a recycled address must never revive a cached handle.
#pragma once
#include <cstdint>
#include <unordered_map>

namespace scene_cache {

struct Token {
    uint32_t address = 0;
    uint64_t generation = 0;
    explicit operator bool() const { return address && generation; }
    bool operator==(const Token&) const = default;
};

class Lifetimes {
    struct Lifetime { uint64_t generation; bool ready; };
    std::unordered_map<uint32_t, Lifetime> live_;
    uint64_t generation_ = 0;

public:
    Token begin(uint32_t address, bool ready = true) {
        if (!address) return {};
        live_[address] = {++generation_, ready};
        return {address, generation_};
    }
    void publish(uint32_t address) {
        auto it = live_.find(address);
        if (it != live_.end()) it->second.ready = true;
    }
    Token current(uint32_t address) const {
        auto it = live_.find(address);
        return it == live_.end() || !it->second.ready ? Token{} : Token{address, it->second.generation};
    }
    bool contains(Token token) const { return token && current(token.address) == token; }
    void retire(uint32_t address) { live_.erase(address); }
};

struct NodeRef {
    Token world, node;
    explicit operator bool() const { return world && node; }
    bool operator==(const NodeRef&) const = default;
};

struct LifetimesByOwner {
    Lifetimes worlds, nodes;
    NodeRef reference(Token world, uint32_t node) const {
        Token token = nodes.current(node);
        return worlds.contains(world) ? NodeRef{world, token ? token : Token{node, 0}} : NodeRef{};
    }
    bool contains(NodeRef ref) const { return worlds.contains(ref.world) && nodes.contains(ref.node); }
};

// Header checks are not lifetime evidence. Callers must first validate tokens,
// then check the actual mapped span and supported native type before access.
inline bool vector_layout(uint32_t begin, uint32_t end, uint32_t capacity, uint32_t element_size) {
    return element_size && begin <= end && end <= capacity &&
           (begin || (!end && !capacity)) && begin % element_size == 0 &&
           (end - begin) % element_size == 0 && (capacity - begin) % element_size == 0;
}

}  // namespace scene_cache
