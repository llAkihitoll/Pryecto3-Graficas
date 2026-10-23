#include "hud.h"
#include <algorithm>

void fillRect(Framebuffer& fb, int x, int y, int w, int h, uint32_t argb) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(fb.width(), x + w), y1 = std::min(fb.height(), y + h);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) fb.setPixel(px, py, argb);
}

void drawBar(Framebuffer& fb, int x, int y, int w, int h, float fraction, uint32_t argb) {
    fraction = std::min(1.0f, std::max(0.0f, fraction));
    fillRect(fb, x - 1, y - 1, w + 2, h + 2, 0xFF5A6478); // marco
    fillRect(fb, x, y, w, h, 0xFF0C0F1A);                 // fondo
    fillRect(fb, x, y, (int)(w * fraction), h, argb);
}
