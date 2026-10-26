#include "boost_flame.h"
#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- penacho

// Perfil del penacho: radio segun la distancia z al escape (0..1). Sale del
// tamano del escape, se hincha cerca del inicio y termina en punta.
static float flameRadius(float z) { return std::pow(1.0f - z, 0.8f) * (0.6f + 0.4f * std::sin(z * PI * 0.9f + 0.6f)); }

Mesh makeFlame(Vec3 tint) {
    Mesh mesh;
    Material m{"Llama", tint};
    m.emissive = 1.0f;
    mesh.materials.push_back(m);

    // Superficie de revolucion: anillos a lo largo de z, cada uno con "sides"
    // vertices. La normal sale de la pendiente del perfil: (cos, sin, -dr/dz).
    const int rings = 12, sides = 16;
    for (int i = 0; i <= rings; ++i) {
        float z = (float)i / rings;
        float r = flameRadius(z);
        float dz = 0.01f;
        float slope = (flameRadius(std::min(1.0f, z + dz)) - flameRadius(std::max(0.0f, z - dz))) / (2.0f * dz);
        for (int j = 0; j < sides; ++j) {
            float a = 2.0f * PI * j / sides;
            float c = std::cos(a), s = std::sin(a);
            mesh.vertices.push_back(Vertex{{r * c, r * s, z}, normalize(Vec3{c, s, -slope})});
        }
    }
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sides; ++j) {
            int a = i * sides + j, b = i * sides + (j + 1) % sides;
            int c = a + sides, d = b + sides;
            // Orden para que las caras miren hacia afuera del eje:
            // (b - a) x (c - a) = tangente x eje Z = direccion radial.
            mesh.triangles.push_back(Triangle{a, b, c, 0});
            mesh.triangles.push_back(Triangle{b, d, c, 0});
        }
    }
    return mesh;
}
