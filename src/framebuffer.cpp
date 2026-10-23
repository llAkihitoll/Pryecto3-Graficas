#include "framebuffer.h"
#include <algorithm>
#include <fstream>

Framebuffer::Framebuffer(int width, int height)
    : w(width), h(height), pixels((size_t)width * (size_t)height),
      depth((size_t)width * (size_t)height),
      clearColor(0xFF000000u), currentColor(0xFFFFFFFFu) {
    clear();
}

void Framebuffer::setClearColor(Color color) {
    clearColor = color.toARGB();
}

void Framebuffer::setColor(Color color) {
    currentColor = color.toARGB();
}

// Limpia color y profundidad; se llama al inicio de cada cuadro.
void Framebuffer::clear() {
    std::fill(pixels.begin(), pixels.end(), clearColor);
    std::fill(depth.begin(), depth.end(), 0.0f);
}

void Framebuffer::point(int x, int y) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    pixels[(size_t)y * w + x] = currentColor;
}

void Framebuffer::renderBuffer(SDL_Renderer* renderer, SDL_Texture* texture) {
    SDL_UpdateTexture(texture, nullptr, pixels.data(), w * (int)sizeof(uint32_t));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

// Guarda el contenido actual del framebuffer como imagen BMP.
bool Framebuffer::saveBMP(const char* path) const {
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        (void*)pixels.data(), w, h, 32, w * (int)sizeof(uint32_t), SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return false;
    bool ok = SDL_SaveBMP(surface, path) == 0;
    SDL_FreeSurface(surface);
    return ok;
}

static void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// Escribe un chunk PNG: longitud, tipo, datos y CRC (de tipo + datos).
static void writeChunk(std::ofstream& file, const char* type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> chunk;
    putU32(chunk, (uint32_t)data.size());
    chunk.insert(chunk.end(), type, type + 4);
    chunk.insert(chunk.end(), data.begin(), data.end());
    putU32(chunk, crc32(chunk.data() + 4, chunk.size() - 4));
    file.write((const char*)chunk.data(), (std::streamsize)chunk.size());
}

// Guarda el framebuffer como PNG RGB de 8 bits. Los datos van en bloques
// deflate "stored" (sin compresion), asi no se necesita ninguna biblioteca.
bool Framebuffer::savePNG(const char* path) const {
    // Cada fila empieza con el byte de filtro 0 (ninguno) seguido de RGB.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (size_t)(w * 3 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            uint32_t p = pixels[(size_t)y * w + x];
            raw.push_back((uint8_t)(p >> 16));
            raw.push_back((uint8_t)(p >> 8));
            raw.push_back((uint8_t)p);
        }
    }

    // Flujo zlib: cabecera, bloques de hasta 65535 bytes y checksum Adler-32.
    std::vector<uint8_t> zlib = {0x78, 0x01};
    size_t pos = 0;
    do {
        uint16_t n = (uint16_t)std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + n == raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back((uint8_t)n);
        zlib.push_back((uint8_t)(n >> 8));
        zlib.push_back((uint8_t)~n);
        zlib.push_back((uint8_t)(~n >> 8));
        zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    } while (pos < raw.size());

    uint32_t a = 1, b = 0;
    for (uint8_t d : raw) {
        a = (a + d) % 65521;
        b = (b + a) % 65521;
    }
    putU32(zlib, (b << 16) | a);

    // IHDR: ancho, alto, 8 bits por canal, tipo de color 2 (RGB).
    std::vector<uint8_t> ihdr;
    putU32(ihdr, (uint32_t)w);
    putU32(ihdr, (uint32_t)h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    const uint8_t signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    file.write((const char*)signature, 8);
    writeChunk(file, "IHDR", ihdr);
    writeChunk(file, "IDAT", zlib);
    writeChunk(file, "IEND", std::vector<uint8_t>());
    return file.good();
}
