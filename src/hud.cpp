#include "hud.h"
#include <algorithm>
#include <cstdio>
#include "font.h"

void fillRect(Framebuffer& fb, int x, int y, int w, int h, uint32_t argb) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(fb.width(), x + w), y1 = std::min(fb.height(), y + h);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) fb.setPixel(px, py, argb);
}

void darkenRect(Framebuffer& fb, int x, int y, int w, int h, float amount) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(fb.width(), x + w), y1 = std::min(fb.height(), y + h);
    uint32_t keep = (uint32_t)((1.0f - amount) * 256.0f);
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px) {
            uint32_t p = fb.getPixel(px, py);
            uint32_t r = ((p >> 16) & 0xFF) * keep >> 8, g = ((p >> 8) & 0xFF) * keep >> 8, b = (p & 0xFF) * keep >> 8;
            fb.setPixel(px, py, 0xFF000000u | (r << 16) | (g << 8) | b);
        }
}

void drawBar(Framebuffer& fb, int x, int y, int w, int h, float fraction, uint32_t argb) {
    fraction = std::min(1.0f, std::max(0.0f, fraction));
    fillRect(fb, x - 1, y - 1, w + 2, h + 2, 0xFF5A6478); // marco
    fillRect(fb, x, y, w, h, 0xFF0C0F1A);                 // fondo
    fillRect(fb, x, y, (int)(w * fraction), h, argb);
}

const uint32_t WHITE = 0xFFE8ECF4, GREY = 0xFF98A2B8, GREEN = 0xFF60E880, CYAN = 0xFF60D0FF, ORANGE = 0xFFFFA040;

static const char* yesNo(bool b) { return b ? "SI" : "NO"; }

void drawHud(Framebuffer& fb, const HudInfo& info) {
    const int W = fb.width(), H = fb.height();
    char line[128];

    // --- Panel de navegacion (arriba a la izquierda) ---
    darkenRect(fb, 8, 8, 232, 58, 0.55f);
    std::snprintf(line, sizeof(line), "%s  %.1f", info.nearName.c_str(), info.altitude);
    drawText(fb, 16, 14, line, WHITE, 2);
    if (info.surfaceMode) drawText(fb, 16, 34, "MODO SUPERFICIE", GREEN);
    else drawText(fb, 16, 34, info.surfaceEnabled ? "VUELO LIBRE" : "VUELO LIBRE (SUPERFICIE APAGADA)", GREY);
    std::snprintf(line, sizeof(line), "VELOCIDAD %.1f   BOOST %.0f%%", info.speed, info.boostFraction * 100.0f);
    drawText(fb, 16, 48, line, WHITE);

    // --- Panel del renderizador (arriba a la derecha) ---
    const int panelW = 250, px = W - panelW - 8;
    darkenRect(fb, px, 8, panelW, 124, 0.55f);
    std::snprintf(line, sizeof(line), "FPS %.0f   CUADRO %.1f MS", info.fps, info.frameMs);
    drawText(fb, px + 8, 14, line, info.fps >= 30 ? GREEN : ORANGE);
    std::snprintf(line, sizeof(line), "TRIANGULOS %d", info.stats.trianglesIn);
    drawText(fb, px + 8, 26, line, WHITE);
    std::snprintf(line, sizeof(line), "RASTERIZADOS %d", info.stats.trianglesDrawn);
    drawText(fb, px + 8, 36, line, WHITE);
    std::snprintf(line, sizeof(line), "OBJETOS %d  DESCARTADOS %d", info.stats.objectsDrawn, info.stats.objectsCulled);
    drawText(fb, px + 8, 46, line, WHITE);
    std::snprintf(line, sizeof(line), "SUPERFICIE %d  SEMILLA %u", info.stats.propsDrawn, info.seed);
    drawText(fb, px + 8, 56, line, WHITE);
    std::snprintf(line, sizeof(line), "DESCARTE %s  LOD %s", yesNo(info.opt.cullObjects), yesNo(info.opt.autoLod));
    drawText(fb, px + 8, 70, line, CYAN);
    std::snprintf(line, sizeof(line), "RELIEVE %s  GRAVEDAD %s", yesNo(info.relief), yesNo(info.gravity));
    drawText(fb, px + 8, 80, line, CYAN);
    std::snprintf(line, sizeof(line), "MAPA DE NORMALES %s", yesNo(info.normalMapping));
    drawText(fb, px + 8, 90, line, CYAN);
    std::snprintf(line, sizeof(line), "ASTEROIDES %d  GOLPEADOS %d", info.asteroids, info.asteroidsFlying);
    drawText(fb, px + 8, 104, line, ORANGE);
    std::snprintf(line, sizeof(line), "EXPLOSIONES %d", info.explosions);
    drawText(fb, px + 8, 114, line, ORANGE);

    // --- Barras de velocidad y boost (abajo a la izquierda) ---
    drawText(fb, 16, H - 36, "VEL", GREY);
    drawText(fb, 16, H - 22, "BOOST", GREY);
    drawBar(fb, 56, H - 35, 160, 7, info.speed / info.maxSpeed, 0xFF3CC8FF);
    drawBar(fb, 56, H - 21, 160, 7, info.boostFraction, info.boosting ? 0xFFFFE070 : 0xFFFF8A20);
    uint32_t modeColor = info.surfaceMode ? 0xFF50E070 : (info.surfaceEnabled ? 0xFF303848 : 0xFF803030);
    fillRect(fb, 226, H - 36, 22, 22, modeColor);

    // --- Ayuda de controles ---
    if (info.showHelp) {
        static const char* help[] = {
            "CONTROLES               (H OCULTA)",
            "W / S      ACELERAR / FRENAR",
            "A / D      GIRAR",
            "^ / ~      NARIZ (EN SUPERFICIE: SUBIR/BAJAR)",
            "Q / E      RODAR",
            "SHIFT      BOOST",
            "ESPACIO    FRENO FUERTE",
            "RATON DER. MIRAR   RUEDA  ZOOM",
            "F  MODO SUPERFICIE   V  GRAVEDAD",
            "T  ORBITAS   Y  TRAYECTORIA",
            "G  RELIEVE   N  NUEVA SEMILLA   M  NORMALES",
            "O  DESCARTE  L  LOD  B  CARAS TRASERAS",
            "R  REINICIAR  P  CAPTURA  ESC  SALIR",
        };
        const int lines = (int)(sizeof(help) / sizeof(help[0]));
        const int hx = 8, hy = 76, hw = 292, hh = lines * 11 + 10;
        darkenRect(fb, hx, hy, hw, hh, 0.6f);
        for (int i = 0; i < lines; ++i) drawText(fb, hx + 8, hy + 6 + i * 11, help[i], i == 0 ? ORANGE : WHITE);
    } else {
        drawText(fb, W - textWidth("H: AYUDA") - 12, H - 18, "H: AYUDA", GREY);
    }
}
