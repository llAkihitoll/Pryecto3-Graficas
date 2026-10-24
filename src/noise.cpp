#include "noise.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

Noise::Noise(unsigned seed) {
    // Permutacion aleatoria de 0..255 a partir de la semilla.
    std::iota(perm, perm + 256, 0);
    std::mt19937 rng(seed);
    std::shuffle(perm, perm + 256, rng);
    for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
}

// Curva 6t^5 - 15t^4 + 10t^3: suaviza la interpolacion para que no se
// noten las lineas de la cuadricula.
static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static float mix(float a, float b, float t) { return a + (b - a) * t; }

// Producto punto entre la distancia a la esquina y uno de 12 gradientes
// (las aristas de un cubo), elegido con los bits del hash.
float Noise::grad(int hash, float x, float y, float z) const {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float Noise::perlin(const Vec3& p) const {
    float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    int X = (int)fx & 255, Y = (int)fy & 255, Z = (int)fz & 255;
    float x = p.x - fx, y = p.y - fy, z = p.z - fz;
    float u = fade(x), v = fade(y), w = fade(z);

    // Hash de las 8 esquinas del cubo que contiene al punto.
    int A = perm[X] + Y, AA = perm[A] + Z, AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y, BA = perm[B] + Z, BB = perm[B + 1] + Z;

    return mix(mix(mix(grad(perm[AA], x, y, z), grad(perm[BA], x - 1, y, z), u),
                   mix(grad(perm[AB], x, y - 1, z), grad(perm[BB], x - 1, y - 1, z), u), v),
               mix(mix(grad(perm[AA + 1], x, y, z - 1), grad(perm[BA + 1], x - 1, y, z - 1), u),
                   mix(grad(perm[AB + 1], x, y - 1, z - 1), grad(perm[BB + 1], x - 1, y - 1, z - 1), u), v),
               w);
}

float Noise::fbm(const Vec3& p, int octaves) const {
    float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
    Vec3 q = p;
    for (int i = 0; i < octaves; ++i) {
        sum += amplitude * perlin(q);
        total += amplitude;
        amplitude *= 0.5f;
        q = q * 2.03f + Vec3{17.1f, 9.3f, 4.7f}; // desfase para que las octavas no coincidan
    }
    return sum / total;
}

// Punto "aleatorio" (pero fijo) dentro de la celda (x, y, z), sacado de la
// permutacion para que tambien dependa de la semilla.
Vec3 Noise::cellPoint(int x, int y, int z) const {
    int h = perm[(perm[(perm[x & 255] + y) & 255] + z) & 255];
    int h2 = perm[(h + 71) & 255], h3 = perm[(h + 151) & 255];
    return Vec3{x + h / 255.0f, y + h2 / 255.0f, z + h3 / 255.0f};
}

float Noise::cellular(const Vec3& p) const {
    int cx = (int)std::floor(p.x), cy = (int)std::floor(p.y), cz = (int)std::floor(p.z);
    float best = 1e9f;
    // El punto mas cercano tiene que estar en esta celda o en una vecina.
    for (int dz = -1; dz <= 1; ++dz)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                Vec3 d = cellPoint(cx + dx, cy + dy, cz + dz) - p;
                best = std::min(best, dot(d, d));
            }
    return std::sqrt(best);
}
