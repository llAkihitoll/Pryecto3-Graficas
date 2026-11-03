#include "rlball.h"
#include <algorithm>
#include <cmath>

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Distancia (en unidades de celda) al borde del hexagono mas cercano de una
// rejilla de hexagonos de ancho 1: 0 sobre el borde, ~0.5 en el centro. Los
// centros forman dos rejillas rectangulares desfasadas; se toma la mas cercana.
static float hexEdge(float x, float y) {
    const float h = 1.7320508f; // sqrt(3)
    auto wrap = [](float v, float m) { return v - m * std::floor(v / m); };
    float ax = wrap(x, 1.0f) - 0.5f, ay = wrap(y, h) - h * 0.5f;
    float bx = wrap(x - 0.5f, 1.0f) - 0.5f, by = wrap(y - h * 0.5f, h) - h * 0.5f;
    bool useA = ax * ax + ay * ay < bx * bx + by * by;
    float gx = std::fabs(useA ? ax : bx), gy = std::fabs(useA ? ay : by);
    float d = std::max(gx, gx * 0.5f + gy * 0.8660254f);
    return 0.5f - d;
}

struct BallTexel {
    Vec3 color;
    float shine;
};

static BallTexel rocketBall(const Vec3& d) {
    const Vec3 white{0.92f, 0.93f, 0.95f}, grey{0.52f, 0.54f, 0.58f}, rubber{0.05f, 0.05f, 0.06f};
    const Vec3 plate{0.80f, 0.82f, 0.86f}, plateDark{0.50f, 0.52f, 0.56f};
    float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);

    // Coordenadas dentro de la cara del cubo (-1..1) y cual eje domina.
    int axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
    float big = axis == 0 ? ax : (axis == 1 ? ay : az);
    float s = (axis == 0 ? d.y : d.x) / big, t = (axis == 2 ? d.y : d.z) / big;

    // Costura: cerca de una arista del cubo (la segunda coordenada se acerca
    // a la mayor). seam = 0 sobre la arista.
    float sorted[3] = {ax, ay, az};
    std::sort(sorted, sorted + 3);
    float seam = 1.0f - sorted[1] / sorted[2];

    // Placa mecanica en la esquina mas cercana del cubo.
    Vec3 corner{d.x < 0 ? -1.0f : 1.0f, d.y < 0 ? -1.0f : 1.0f, d.z < 0 ? -1.0f : 1.0f};
    corner = corner * 0.57735f;
    float angle = std::acos(clampf(dot(d, corner), -1.0f, 1.0f)); // radianes a la esquina

    if (angle < 0.075f) { // lente: negro con un aro claro y un brillo
        if (angle > 0.058f) return {Vec3{0.85f, 0.87f, 0.9f}, 0.9f};
        if (angle < 0.018f) return {Vec3{0.25f, 0.27f, 0.3f}, 1.0f};
        return {Vec3{0.03f, 0.03f, 0.04f}, 1.0f};
    }
    bool onArm = seam < 0.11f && angle < 0.42f; // brazos de la placa sobre las costuras
    if (angle < 0.2f || onArm) {
        float edge = angle < 0.2f ? 0.2f - angle : (0.11f - seam) * 1.8f;
        if (edge < 0.012f) return {rubber, 0.2f}; // contorno oscuro de la placa
        // Estrias: anillos alrededor del lente y escalones sobre los brazos.
        float ribs = std::sin(angle * 140.0f);
        Vec3 c = ribs > 0.55f ? plateDark : plate;
        if (angle > 0.1f && angle < 0.115f) c = plateDark; // ranura alrededor del centro
        return {c, 0.8f};
    }
    if (seam < 0.075f) return {rubber, 0.15f}; // costura negra

    if (axis != 2) {
        // Panel blanco: relieve suave de hexagonos grandes (sombra en los bordes).
        float e = hexEdge(s * 2.6f, t * 2.6f);
        float shade = 0.86f + 0.14f * smoothstep(0.0f, 0.18f, e);
        return {white * shade, 0.45f};
    }
    // Panel gris: malla fina de hexagonos, con la linea mas oscura.
    float e = hexEdge(s * 9.0f, t * 9.0f);
    Vec3 c = e < 0.07f ? grey * 0.62f : grey * (0.95f + 0.1f * smoothstep(0.07f, 0.3f, e));
    return {c, 0.25f};
}

void bakeRocketBall(PlanetTextures& tex, int size) {
    tex.color.bake(size, [](const Vec3& d) { return rocketBall(d).color; });
    tex.mask.bake(size, [](const Vec3& d) { return rocketBall(d).shine; });
}

Vec3 shadeRocketBall(const PlanetTextures& tex, const Vec3& localDir, const Vec3& n, const Vec3& toLight,
                     const Vec3& toCamera, float ambient) {
    Vec3 albedo = tex.color.sample(localDir);
    float shine = tex.mask.sample(localDir);
    float diffuse = std::max(0.0f, dot(n, toLight));
    Vec3 half = normalize(toLight + toCamera);
    // Lo metalico (placas, lente) tiene un brillo mas concentrado que el hule.
    float spec = shine * std::pow(std::max(0.0f, dot(n, half)), 12.0f + 60.0f * shine) * 0.6f;
    float edge = 1.0f - std::max(0.0f, dot(n, toCamera));
    Vec3 rim = Vec3{0.45f, 0.6f, 1.0f} * (edge * edge * edge * 0.3f);
    return albedo * (ambient + (1.0f - ambient) * diffuse) + Vec3{spec, spec, spec} + rim;
}
