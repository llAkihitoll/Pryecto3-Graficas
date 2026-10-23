#pragma once
#include <SDL.h>
#include <cstdint>
#include <vector>
#include "color.h"

// Arreglo de pixeles ARGB8888 donde se dibuja todo. SDL solo se usa
// en renderBuffer() para copiar el arreglo a una textura y mostrarlo.
// Junto a los colores guarda un buffer de profundidad del mismo tamano.
class Framebuffer {
public:
    Framebuffer(int width, int height);

    void setClearColor(Color color);
    void setColor(Color color);
    void clear();
    void point(int x, int y);

    // Escritura directa usada por el rasterizador (sin revisar limites:
    // el rasterizador ya recorta a la pantalla).
    void setPixel(int x, int y, uint32_t argb) { pixels[(size_t)y * w + x] = argb; }

    // Profundidad guardada como 1/w (w = distancia a la camara). Un valor
    // mas grande significa mas cerca; 0 es "infinitamente lejos".
    float& depthAt(int x, int y) { return depth[(size_t)y * w + x]; }

    void renderBuffer(SDL_Renderer* renderer, SDL_Texture* texture);
    bool saveBMP(const char* path) const;
    bool savePNG(const char* path) const;

    int width() const { return w; }
    int height() const { return h; }

private:
    int w, h;
    std::vector<uint32_t> pixels;
    std::vector<float> depth;
    uint32_t clearColor;
    uint32_t currentColor;
};
