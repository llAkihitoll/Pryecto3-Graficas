#pragma once
#include <string>
#include "framebuffer.h"
#include "renderer.h"

// Interfaz 2D dibujada directo en el framebuffer, despues de la escena 3D.

// Rectangulo relleno, recortado a la pantalla.
void fillRect(Framebuffer& fb, int x, int y, int w, int h, uint32_t argb);

// Oscurece un rectangulo (fondo semitransparente para los paneles):
// amount = 0 lo deja igual, 1 lo vuelve negro.
void darkenRect(Framebuffer& fb, int x, int y, int w, int h, float amount);

// Barra horizontal con marco: fraction (0..1) indica cuanto esta llena.
void drawBar(Framebuffer& fb, int x, int y, int w, int h, float fraction, uint32_t argb);

// Todo lo que muestra el HUD en un cuadro (lo llena main.cpp).
struct HudInfo {
    float fps = 0;
    float frameMs = 0; // cuanto tardo en renderizarse el cuadro
    float speed = 0, maxSpeed = 1;
    float boostFraction = 0;
    bool boosting = false;
    std::string nearName;
    float altitude = 0;
    bool surfaceMode = false, surfaceEnabled = true;
    bool gravity = true, relief = true, normalMapping = true;
    int asteroids = 0, asteroidsFlying = 0, explosions = 0;
    Optimizations opt;
    RenderStats stats;
    unsigned seed = 0;
    bool showHelp = true;
};

void drawHud(Framebuffer& fb, const HudInfo& info);
