#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace save_validation {
enum class Status { valid, invalid, unknown };
struct Result {
    Status status = Status::invalid;
    std::string reason;
    uint32_t version = 0, objects = 0;
    uint64_t digest = 0;  // FNV-1a identity, not a cryptographic authenticity check
};
Result inspect(std::span<const uint8_t> bytes);
const char* name(Status status);
}  // namespace save_validation
