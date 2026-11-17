#pragma once

#include <cstdint>

namespace render {
// GLES array description, also usable by the asset-free CPU preparation tests.
struct Array {
    bool enabled = false;
    int size = 4;
    unsigned int type = 0x1406;  // GL_FLOAT; no GL/context dependency here
    int stride = 0;
    const uint8_t* data = nullptr;  // host pointer (guest memory or buffer shadow)
};
}  // namespace render
