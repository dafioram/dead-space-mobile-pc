#include "hle_dispatch.h"
#include <chrono>
#include <future>
#include <iostream>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void require(bool okay, const char* why) { if (!okay) throw std::runtime_error(why); }
struct Context { uint64_t value = 0; };
using Handler = void (*)(Context&);
void even(Context& c) { c.value += 7; }
void odd(Context& c) { c.value += 19; }

template<size_t Capacity>
struct Registry {
    struct Entry { std::string name; Handler fn; };
    cpu::hle_dispatch::Table<Handler, Capacity> published;
    std::mutex lock;
    std::vector<Entry> metadata;
    std::array<uint64_t, Capacity> stubs{};
    std::atomic<uint64_t> slow_calls{0};

    uint32_t append(Handler fn) {
        std::lock_guard guard(lock);
        uint32_t id = uint32_t(metadata.size());
        require(id < Capacity, "synthetic registry capacity");
        metadata.push_back({"synthetic-" + std::to_string(id), fn});
        stubs[id] = uint64_t(id) << 32 | (id ^ 0x51cafeu);
        require(published.publish_next(id, fn), "monotonic publication under writer lock");
        return id;
    }
    Handler lookup(uint32_t id, bool optimized) {
        return cpu::hle_dispatch::resolve(published, id, optimized, [&](uint32_t index) -> Handler {
            slow_calls.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard guard(lock);
            return index < metadata.size() ? metadata[index].fn : nullptr;
        });
    }
    std::string name(uint32_t id) {
        std::lock_guard guard(lock);
        return id < metadata.size() ? metadata[id].name : "";
    }
};

void bounds_and_equivalence() {
    Registry<8> registry;
    require(registry.published.find(0) == nullptr && registry.published.find(UINT32_MAX) == nullptr, "empty and extreme IDs are safe misses");
    require(!registry.published.publish_next(1, even), "cannot publish out of sequence");
    require(registry.append(nullptr) == 0, "reserved/null slot keeps its ID");
    require(!registry.published.publish_next(0, odd), "registered null slot cannot be replaced");
    for (uint32_t i = 1; i < 8; ++i) require(registry.append(i % 2 ? odd : even) == i, "unchanged contiguous SVC IDs");
    require(!registry.published.publish_next(8, even), "capacity overflow refused");
    require(!registry.published.publish_next(1, even), "published handler cannot be replaced");
    for (uint32_t id : {0u,1u,2u,3u,4u,5u,6u,7u,8u,0xffffffu,UINT32_MAX}) {
        Handler fast = registry.lookup(id, true), reference = registry.lookup(id, false);
        require(fast == reference, "published/missing/null/extreme lookup result equals reference");
        if (fast) {
            Context a, b;
            fast(a); reference(b);
            require(a.value == b.value, "same handler selected and invoked");
        }
    }
    const auto before = registry.slow_calls.load();
    require(registry.lookup(1, true) == odd && registry.slow_calls.load() == before, "published hit avoids original registry mutex");
    require(registry.lookup(0, true) == nullptr && registry.slow_calls.load() == before + 1, "registered null retains slow validation/failure result");
    require(registry.lookup(999, true) == nullptr && registry.slow_calls.load() == before + 2, "out of range retains slow validation/failure result");
}

void full_stub_capacity() {
    // Production's full 1 MiB / 8-byte stub capacity, heap-owned in this test so
    // the Windows test stack does not have to contain a 1 MiB table.
    auto table = std::make_unique<cpu::hle_dispatch::Table<Handler, 0x100000 / 8>>();
    static_assert(decltype(table)::element_type::storage_bytes == 1024 * 1024);
    for (uint32_t i = 0; i < table->capacity; ++i) require(table->publish_next(i, i ? odd : nullptr), "every original stub slot fits");
    require(table->find(uint32_t(table->capacity - 1)) == odd, "last supported slot is reachable");
    require(!table->publish_next(uint32_t(table->capacity), even), "does not extend guest stub limit");
}

void release_acquire_visibility() {
    constexpr size_t count = 2048;
    cpu::hle_dispatch::Table<Handler, count> table;
    std::array<uint64_t, count> stub_words{};
    std::atomic<bool> failed{false};
    std::latch ready(4);
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) readers.emplace_back([&] {
        ready.count_down();
        for (uint32_t i = 0; i < count; ++i) {
            Handler handler;
            while (!(handler = table.find(i))) std::this_thread::yield();
            if (handler != odd || stub_words[i] != (uint64_t(i) << 32 | (i ^ 0x51cafeu))) failed.store(true);
        }
    });
    ready.wait();
    for (uint32_t i = 0; i < count; ++i) {
        stub_words[i] = uint64_t(i) << 32 | (i ^ 0x51cafeu);
        require(table.publish_next(i, odd), "single writer publishes metadata/stub before function");
    }
    for (auto& reader : readers) reader.join();
    require(!failed.load(), "acquiring a handler also observes completed stub/metadata writes");
}

void in_flight_registration() {
    Registry<8> registry;
    registry.append(nullptr);
    std::unique_lock writer(registry.lock);
    std::latch slow_entered(1);
    auto reader = std::async(std::launch::async, [&] {
        return cpu::hle_dispatch::resolve(registry.published, 1, true, [&](uint32_t index) -> Handler {
            slow_entered.count_down();
            std::lock_guard guard(registry.lock);
            return index < registry.metadata.size() ? registry.metadata[index].fn : nullptr;
        });
    });
    slow_entered.wait();  // reader missed unpublished slot and is entering the ORIGINAL lock
    registry.metadata.push_back({"synthetic-1", odd});
    registry.stubs[1] = 0xabcdef;
    require(registry.published.publish_next(1, odd), "publish in-flight registration");
    writer.unlock();
    require(reader.get() == odd, "in-flight registration is not a false bad-SVC/null failure");
}

void published_hit_during_append() {
    Registry<8> registry;
    registry.append(nullptr); registry.append(odd);
    std::unique_lock writer(registry.lock);
    auto reader = std::async(std::launch::async, [&] { return registry.lookup(1, true); });
    const bool ready = reader.wait_for(std::chrono::seconds(3)) == std::future_status::ready;
    writer.unlock();  // always release before any failure, so this test cannot strand the reader
    require(reader.get() == odd && ready, "published lookup completes while unrelated registration owns mutex");
}

void callback_reentry() {
    Registry<8> registry;
    registry.append(nullptr); registry.append(odd);
    for (bool optimized : {false, true}) {
        Handler fn = registry.lookup(1, optimized);
        // Dispatch returns an address, not a lock-owning guard. Simulate a host
        // callback registering and resolving another function before it returns.
        uint32_t id = registry.append(even);
        Context context;
        fn(context);
        registry.lookup(id, optimized)(context);
        require(context.value == 26, "callback registration/nested lookup runs with no registry lock held");
    }
}

void concurrent_append_and_names(bool optimized) {
    constexpr size_t capacity = 4096;
    auto registry = std::make_unique<Registry<capacity>>();
    registry->append(nullptr);
    for (int i = 0; i < 32; ++i) registry->append(odd);
    std::atomic<bool> failed{false};
    std::latch start(7);
    std::vector<std::thread> threads;
    for (int w = 0; w < 2; ++w) threads.emplace_back([&] {
        start.arrive_and_wait();
        for (int i = 0; i < 1500; ++i) registry->append(odd);
    });
    for (int r = 0; r < 4; ++r) threads.emplace_back([&, r] {
        start.arrive_and_wait();
        uint32_t random = uint32_t(r + 1);
        for (int i = 0; i < 30000; ++i) {
            random = random * 1664525u + 1013904223u;
            const uint32_t id = random % capacity;
            Handler fn = registry->lookup(id, optimized);
            if (!fn) continue;
            Context context; fn(context);
            if (context.value != 19 || registry->stubs[id] != (uint64_t(id) << 32 | (id ^ 0x51cafeu)) ||
                registry->name(id) != "synthetic-" + std::to_string(id)) failed.store(true);
        }
    });
    start.arrive_and_wait();
    for (auto& thread : threads) thread.join();
    require(!failed.load(), "concurrent serialized writers, readers and name copies survive vector reallocations");
}

#if defined(__GNUC__)
#define BENCH_NOINLINE __attribute__((noinline, noclone))
#else
#define BENCH_NOINLINE
#endif
BENCH_NOINLINE void accumulate(Context& context) { context.value += 1; }
BENCH_NOINLINE double timing(bool optimized, uint64_t& checksum) {
    Registry<128> registry;
    registry.append(nullptr);
    for (int i = 0; i < 64; ++i) registry.append(accumulate);
    Context context;
    const auto start = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < 2000000; ++i) {
        // Do not include the test registry's slow-call accounting atomic in the
        // reference timing. This is the production resolver + original lock.
        Handler fn = cpu::hle_dispatch::resolve(registry.published, i % 64 + 1, optimized, [&](uint32_t index) -> Handler {
            std::lock_guard guard(registry.lock);
            return index < registry.metadata.size() ? registry.metadata[index].fn : nullptr;
        });
        fn(context);
    }
    checksum = context.value;
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}

int main(int argc, char** argv) {
    try {
        bounds_and_equivalence(); full_stub_capacity(); release_acquire_visibility();
        in_flight_registration(); published_hit_during_append(); callback_reentry();
        concurrent_append_and_names(false); concurrent_append_and_names(true);
        std::cout << "HLE dispatch publication/equivalence/concurrency passed; no guest/JIT/game execution\n";
        if (argc == 2 && std::string(argv[1]) == "--benchmark") {
            uint64_t a = 0, b = 0;
            const double reference = timing(false, a), optimized = timing(true, b);
            require(a == 2000000 && b == a, "benchmark invokes the same handler count/results");
            std::cout << "synthetic dispatch reference_ms=" << reference << " optimized_ms=" << optimized
                      << " ratio=" << optimized / reference << " (not a gameplay improvement claim)\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
