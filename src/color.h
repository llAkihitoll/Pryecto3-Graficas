#pragma once
#include <cstdint>

struct Color {
    uint8_t r, g, b;

    // Formato ARGB8888, el mismo que usa la textura de SDL.
    uint32_t toARGB() const {
        return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
    }
};
