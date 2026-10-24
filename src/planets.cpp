#include "planets.h"
#include <algorithm>
#include <cmath>
#include "noise.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// ---------------------------------------------------------------- Duna

// Perfil de un crater segun la distancia d al centro de su celda: hundido
// adentro del radio y con un borde levantado justo afuera.
static float crater(float d, float radius) {
    if (d < radius) {
        float x = d / radius;
        return (x * x - 1.0f) + 0.35f * std::exp(-std::pow((x - 1.0f) / 0.25f, 2.0f));
    }
    return 0.35f * std::exp(-std::pow((d - radius) / (radius * 0.25f), 2.0f));
}

// Todo lo que describe un punto de la superficie de Duna.
struct DuneSample {
    float height;
    float base;   // relieve grande: negativo = tierras bajas
    float craterBig;
    float dune;
};

static DuneSample duneAt(const Noise& noise, const Vec3& p) {
    DuneSample s;
    s.base = noise.fbm(p * 1.8f, 5);
    s.craterBig = crater(noise.cellular(p * 2.6f + Vec3{3.1f, 7.7f, 1.3f}), 0.33f);
    float craterSmall = crater(noise.cellular(p * 7.0f + Vec3{11.0f, 2.0f, 5.0f}), 0.25f);

    // Dunas: franjas paralelas deformadas con ruido, solo en las tierras bajas.
    float lowland = smoothstep(0.05f, -0.2f, s.base);
    float wave = 0.5f + 0.5f * std::sin(dot(p, Vec3{0.6f, 0.25f, 0.76f}) * 90.0f + noise.fbm(p * 5.0f, 3) * 8.0f);
    s.dune = wave * wave * wave * lowland;

    s.height = s.base * 0.6f + s.craterBig * 0.5f + craterSmall * 0.2f + s.dune * 0.08f;
    return s;
}

void bakeDune(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 rust{0.55f, 0.24f, 0.12f}, sand{0.86f, 0.54f, 0.30f}, pale{0.94f, 0.79f, 0.56f};

    tex.color.bake(size, [&](const Vec3& p) {
        DuneSample s = duneAt(noise, p);
        // Mezcla por altura: rojizo abajo, arena en medio, claro en las mesetas.
        Vec3 c = s.base < 0.0f ? lerp(sand, rust, smoothstep(0.0f, -0.45f, s.base))
                               : lerp(sand, pale, smoothstep(0.1f, 0.5f, s.base));
        if (s.craterBig < 0.0f) c = c * (1.0f + 0.25f * s.craterBig); // fondo del crater mas oscuro
        c = c * (1.0f + 0.12f * s.dune);                                // crestas de las dunas
        c = c * (1.0f + 0.08f * noise.fbm(p * 40.0f, 2));               // grano fino
        return c;
    });

    // Normal por diferencias finitas: cuanto cambia la altura al moverse un
    // poco en dos direcciones tangentes a la esfera.
    const float e = 0.0015f;
    const float bump = 0.06f; // que tan marcado se ve el relieve
    tex.normal.bake(size, [&](const Vec3& p) {
        Vec3 helper = std::fabs(p.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 t1 = normalize(cross(p, helper));
        Vec3 t2 = cross(p, t1);
        float h0 = duneAt(noise, p).height;
        float h1 = duneAt(noise, normalize(p + t1 * e)).height;
        float h2 = duneAt(noise, normalize(p + t2 * e)).height;
        return normalize(p - t1 * ((h1 - h0) / e * bump) - t2 * ((h2 - h0) / e * bump));
    });
}

// ---------------------------------------------------------------- Sol

void bakeSun(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    tex.color.bake(size, [&](const Vec3& p) {
        // Granulos: brillantes al centro de cada celda y oscuros en los bordes.
        float cell = noise.cellular(p * 18.0f);
        float granule = clampf(1.0f - cell * 1.1f + 0.15f * noise.fbm(p * 40.0f, 2), 0.0f, 1.0f);
        float phase = 0.5f + 0.5f * noise.fbm(p * 6.0f, 3); // cada zona late a su propio ritmo
        float spots = noise.fbm(p * 3.0f + Vec3{5.0f, 1.0f, 9.0f}, 4);
        return Vec3{granule, phase, spots};
    });
    // El sol no usa normales con relieve, asi que tex.normal queda vacio.
}
