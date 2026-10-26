#include "skybox.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>
#include "noise.h"
#include "parallel.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Una galaxia espiral lejana: centro en el cielo, inclinacion y tamano.
struct Galaxy {
    Vec3 center;     // direccion (unitaria) donde se ve
    Vec3 axisU, axisV; // ejes del plano tangente (ya girados)
    float size;      // radio aparente (en unidades de la esfera unitaria)
    float flatten;   // 1 = de frente, menos = vista de canto
    float twist;     // cuanto se enrollan los brazos
    Vec3 armColor;
};

// Una nebulosa: nube de gas de un color alrededor de una direccion.
struct Nebula {
    Vec3 center;
    float spread;    // que tan grande es (mas chico = mas extendida)
    Vec3 color;
    Vec3 offset;     // desfase del ruido para que cada una tenga otra forma
};

static Vec3 randomDirection(std::mt19937& rng) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    float z = unit(rng) * 2.0f - 1.0f;
    float a = unit(rng) * 2.0f * PI;
    float r = std::sqrt(1.0f - z * z);
    return Vec3{r * std::cos(a), r * std::sin(a), z};
}

// Brillo de una galaxia en la direccion p: nucleo + dos brazos en espiral.
static Vec3 galaxyLight(const Galaxy& g, const Vec3& p) {
    Vec3 d = p - g.center;
    if (dot(d, d) > g.size * g.size * 9.0f) return Vec3{0, 0, 0}; // lejos: no aporta
    float u = dot(d, g.axisU) / g.size;
    float v = dot(d, g.axisV) / (g.size * g.flatten);
    float r = std::sqrt(u * u + v * v);
    float theta = std::atan2(v, u);
    float core = std::exp(-r * r * 30.0f);
    // Brazos: el angulo se corre con el logaritmo del radio (espiral logaritmica).
    float arms = 0.5f + 0.5f * std::cos(2.0f * theta - std::log(r + 0.05f) * g.twist);
    float disk = std::exp(-r * 2.6f) * (0.25f + 0.75f * arms * arms);
    return Vec3{1.0f, 0.85f, 0.6f} * (core * 1.4f) + g.armColor * (disk * 0.7f);
}

void Skybox::bake(unsigned seed, int size) {
    Noise noise(seed);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    // Plano de la Via Lactea: una franja alrededor de un gran circulo del cielo.
    const Vec3 bandNormal = normalize(Vec3{0.3f, 0.85f, -0.4f});

    std::vector<Galaxy> galaxies;
    for (int i = 0; i < 6; ++i) {
        Galaxy g;
        g.center = randomDirection(rng);
        Vec3 helper = std::fabs(g.center.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 t1 = normalize(cross(g.center, helper));
        Vec3 t2 = cross(g.center, t1);
        float rot = unit(rng) * 2.0f * PI; // orientacion al azar en el cielo
        g.axisU = t1 * std::cos(rot) + t2 * std::sin(rot);
        g.axisV = cross(g.center, g.axisU);
        g.size = 0.02f + 0.035f * unit(rng);
        g.flatten = 0.3f + 0.7f * unit(rng);
        g.twist = 3.0f + 3.0f * unit(rng);
        g.armColor = lerp(Vec3{0.55f, 0.65f, 1.0f}, Vec3{0.9f, 0.7f, 1.0f}, unit(rng));
        galaxies.push_back(g);
    }

    const Nebula nebulae[] = {
        {normalize(Vec3{0.8f, 0.2f, 0.5f}), 2.2f, {0.9f, 0.25f, 0.6f}, {0.0f, 0.0f, 0.0f}},  // magenta
        {normalize(Vec3{-0.6f, 0.4f, -0.7f}), 2.6f, {0.15f, 0.75f, 0.7f}, {7.0f, 3.0f, 1.0f}}, // turquesa
        {normalize(Vec3{-0.2f, -0.7f, 0.6f}), 3.0f, {0.4f, 0.35f, 1.0f}, {2.0f, 9.0f, 4.0f}},  // azul violeta
    };

    sky.bake(size, [&](const Vec3& p) {
        Vec3 color{0.012f, 0.014f, 0.035f}; // fondo casi negro, un poco azulado

        // Via Lactea: brillo segun que tan cerca del plano, con grumos de
        // estrellas (fbm) y canales de polvo oscuro (donde el ruido cruza cero).
        float h = dot(p, bandNormal);
        float band = std::exp(-h * h * 28.0f);
        float clumps = 0.5f + 0.5f * noise.fbm(p * 6.0f, 4);
        float dust = smoothstep(0.0f, 0.12f, std::fabs(noise.fbm(p * 3.5f + Vec3{4.0f, 1.0f, 2.0f}, 4)));
        color += Vec3{0.32f, 0.3f, 0.36f} * (band * clumps * (0.25f + 0.75f * dust) * 0.5f);

        // Nebulosas: ruido fractal deformado con otro ruido (remolinos),
        // limitado a una zona alrededor de su centro.
        for (const Nebula& n : nebulae) {
            float near = std::exp(-length(p - n.center) * n.spread);
            if (near < 0.02f) continue;
            Vec3 q = p * 2.5f + n.offset;
            Vec3 warp{noise.fbm(q, 3), noise.fbm(q + Vec3{5.0f, 2.0f, 8.0f}, 3), 0.0f};
            float gas = smoothstep(-0.1f, 0.5f, noise.fbm(q * 1.6f + warp * 1.8f, 5));
            color += n.color * (gas * near * 0.45f);
        }

        // Galaxias lejanas.
        for (const Galaxy& g : galaxies) color += galaxyLight(g, p);

        // Polvo de estrellas: puntitos tenues, mas en la Via Lactea.
        float speck = noise.cellular(p * 70.0f);
        if (speck < 0.12f) color += Vec3{0.5f, 0.5f, 0.55f} * ((1.0f - speck / 0.12f) * (0.12f + 0.35f * band));
        return color;
    });
}

void Skybox::draw(Framebuffer& fb, const Uniforms& u) const {
    const int W = fb.width(), H = fb.height();
    // Ejes de la camara en el mundo: son las filas de la parte de rotacion de
    // la matriz de vista (derecha, arriba y "hacia atras").
    Vec3 right{u.view.m[0][0], u.view.m[0][1], u.view.m[0][2]};
    Vec3 up{u.view.m[1][0], u.view.m[1][1], u.view.m[1][2]};
    Vec3 back{u.view.m[2][0], u.view.m[2][1], u.view.m[2][2]};
    // Deshacer la proyeccion: un pixel en NDC (x, y) mira en la direccion
    // (x / P00, y / P11, -1) en espacio de camara.
    float sx = 1.0f / u.projection.m[0][0], sy = 1.0f / u.projection.m[1][1];

    const int bandHeight = 16;
    const int bands = (H + bandHeight - 1) / bandHeight;
    threadPool().parallelFor(bands, [&](int band) {
        int y0 = band * bandHeight, y1 = std::min(H, y0 + bandHeight);
        for (int y = y0; y < y1; ++y) {
            float ny = (1.0f - (y + 0.5f) / H * 2.0f) * sy;
            for (int x = 0; x < W; ++x) {
                float nx = ((x + 0.5f) / W * 2.0f - 1.0f) * sx;
                Vec3 dir = right * nx + up * ny - back;
                Vec3 c = sky.sample(normalize(dir));
                uint32_t r = (uint32_t)(clampf(c.x, 0.0f, 1.0f) * 255.0f);
                uint32_t g = (uint32_t)(clampf(c.y, 0.0f, 1.0f) * 255.0f);
                uint32_t b = (uint32_t)(clampf(c.z, 0.0f, 1.0f) * 255.0f);
                fb.setPixel(x, y, 0xFF000000u | (r << 16) | (g << 8) | b);
            }
        }
    });
}
