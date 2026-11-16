#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace sha256 {
// Standard SHA-256 content identity, not a vendor-signature/authenticity check.
std::string digest(std::span<const uint8_t> bytes);
}
