#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>
#include "blackhole.h"
#include "rlball.h"
#include "noise.h"
#include "scene.h"

// Universo alterno: al que se llega cruzando el agujero negro. Tiene su
// propia estrella (blanca), su propio cielo y dos planetas con minijuego.

const unsigned ALT_STAR_SEED = 808;
const unsigned ALT_SKY_SEED = 77;
const unsigned ALBOR_SEED = 11;
const unsigned COBALT_SEED = 23;
const int ALT_STAR_COUNT = 5000;

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// --- Albor, la estrella blanca ---

// Paleta de una estrella mas caliente que el sol: de azul profundo a blanco.
static Vec3 whitePlasma(float heat) {
    const Vec3 deep{0.25f, 0.35f, 0.85f}, blue{0.6f, 0.75f, 1.0f};
    const Vec3 pale{0.88f, 0.93f, 1.0f}, white{1.0f, 1.0f, 1.0f};
    heat = clampf(heat, 0.0f, 1.0f);
    if (heat < 0.4f) return lerp(deep, blue, heat / 0.4f);
    if (heat < 0.75f) return lerp(blue, pale, (heat - 0.4f) / 0.35f);
    return lerp(pale, white, (heat - 0.75f) / 0.25f);
}

// Igual que el shader del sol (granulos que laten y limbo), pero mas
// brillante y blanco, con un halo azulado en el borde.
static Vec3 whiteStarShader(const Fragment& f, const Uniforms& u) {
    Vec3 s = u.textures->color.sample(normalize(f.localPos));
    float boil = 0.5f + 0.5f * std::sin(u.time * 2.6f + s.y * 4.0f * PI);
    float heat = 0.62f + 0.5f * s.x * (0.45f + 0.55f * boil);
    heat -= std::max(0.0f, s.z - 0.4f) * 1.0f;

    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float facing = std::max(0.0f, dot(f.normal, toCamera));
    heat *= 0.7f + 0.3f * std::sqrt(facing);
    float edge = 1.0f - facing;
    return whitePlasma(heat) + Vec3{0.45f, 0.6f, 1.0f} * (edge * edge * edge * 0.6f);
}

// --- Arena: un balon de Rocket League gigante ---

// Misma superficie que el balon del estadio (rlball.cpp), con poca luz
// ambiente para que se vea redondo con la luz de Albor.
static Vec3 arenaShader(const Fragment& f, const Uniforms& u) {
    return shadeRocketBall(*u.textures, normalize(f.localPos), f.normal, normalize(u.lightPos - f.worldPos),
                           normalize(u.cameraPos - f.worldPos), 0.16f);
}

// --- Cobalto: el planeta de la carrera de anillos ---

// Roca azul marmoleada con vetas que brillan (bordes de las celdas de Worley).
static void bakeCobalt(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    tex.color.bake(size, [&](const Vec3& p) {
        float m = 0.5f + 0.5f * noise.fbm(p * 3.0f + Vec3{noise.fbm(p * 2.0f, 3), 0.0f, 0.0f} * 1.5f, 5);
        Vec3 c = lerp(Vec3{0.05f, 0.07f, 0.22f}, Vec3{0.2f, 0.38f, 0.65f}, m);
        return lerp(c, Vec3{0.75f, 0.8f, 0.95f}, smoothstep(0.75f, 0.9f, m) * 0.6f);
    });
    tex.mask.bake(size, [&](const Vec3& p) {
        float f1, f2;
        noise.cellular2(p * 5.0f, f1, f2);
        return 1.0f - smoothstep(0.0f, 0.07f, f2 - f1);
    });
}

static Vec3 cobaltShader(const Fragment& f, const Uniforms& u) {
    Vec3 dir = normalize(f.localPos);
    Vec3 albedo = u.textures->color.sample(dir);
    float vein = u.textures->mask.sample(dir);
    Vec3 geo = f.normal;
    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float day = smoothstep(-0.12f, 0.22f, dot(geo, toLight));
    float diffuse = std::max(0.0f, dot(geo, toLight)) * day;
    Vec3 half = normalize(toLight + toCamera);
    float spec = std::pow(std::max(0.0f, dot(geo, half)), 30.0f) * 0.35f * day;
    // Las vetas brillan cian, y pulsan despacio.
    float pulse = 0.75f + 0.25f * std::sin(u.time * 1.5f + dir.y * 6.0f);
    Vec3 glow = Vec3{0.2f, 0.85f, 1.0f} * (vein * pulse * (0.45f + 0.55f * (1.0f - day)));
    float edge = 1.0f - std::max(0.0f, dot(geo, toCamera));
    Vec3 atmosphere = Vec3{0.4f, 0.55f, 1.0f} * (edge * edge * edge * 0.55f);
    return albedo * (0.16f + 0.9f * diffuse) + Vec3{spec, spec, spec} + glow + atmosphere;
}

void Scene::buildAlternate() {
    alternate = true;

    CelestialBody star;
    star.name = "Albor";
    star.buildLods(Material{"Albor", Vec3{0.9f, 0.95f, 1.0f}});
    star.shader = whiteStarShader;
    star.radius = 48.0f; // mucho mas grande que el sol del universo normal
    star.spinPeriod = 55.0f;
    star.surfaceGravity = 4.0f;
    bodies.push_back(std::move(star));

    // Los planetas de los minijuegos no giran sobre su eje (spinPeriod = 0):
    // asi la cancha y los anillos se quedan quietos respecto al planeta.
    CelestialBody arena;
    arena.name = "Arena";
    arena.buildLods(Material{"Arena", Vec3{0.2f, 0.6f, 0.25f}});
    arena.shader = arenaShader;
    arena.radius = 22.0f;
    arena.orbitRadius = 185.0f;
    arena.orbitPeriod = keplerPeriod(arena.orbitRadius);
    arena.orbitPhase = -0.25f;
    arena.surfaceGravity = 10.0f;
    arena.orbitColor = {0.4f, 0.95f, 0.5f};
    arena.cruiseAltitude = 1.7f; // baja solo hasta el suelo: al tocarlo se aterriza en el mundo abierto
    bodies.push_back(std::move(arena));

    CelestialBody cobalt;
    cobalt.name = "Cobalto";
    cobalt.buildLods(Material{"Cobalto", Vec3{0.2f, 0.35f, 0.7f}});
    cobalt.shader = cobaltShader;
    cobalt.radius = 16.0f;
    cobalt.orbitRadius = 300.0f;
    cobalt.orbitPeriod = keplerPeriod(cobalt.orbitRadius);
    cobalt.orbitPhase = 0.9f;
    cobalt.surfaceGravity = 8.0f;
    cobalt.orbitColor = {0.45f, 0.65f, 1.0f};
    bodies.push_back(std::move(cobalt));

    // Agujero negro de regreso, arriba de Albor, con el disco azul.
    bodies.push_back(makeBlackHole("Regreso", Vec3{0.0f, 420.0f, 0.0f}, 10.0f, Vec3{0.4f, 0.6f, 1.0f}));
    blackHoleIndex = (int)bodies.size() - 1;
    // Se llega (y se reaparece) en el plano de las orbitas, por fuera de la
    // de Cobalto, mirando hacia Arena: asi se ve el sistema al llegar.
    spawnPoint = arrivalPoint = Vec3{-80.0f, 30.0f, 390.0f};
    spawnLook = ALT_ARENA;

    // Cielo mas calido: Via Lactea dorada y nebulosas doradas, rojas y cian.
    SkyPalette palette;
    palette.background = {0.025f, 0.012f, 0.03f};
    palette.band = {0.45f, 0.34f, 0.22f};
    palette.bandNormal = {-0.5f, 0.6f, 0.6f};
    palette.nebulae[0] = {1.0f, 0.6f, 0.2f};
    palette.nebulae[1] = {0.9f, 0.15f, 0.25f};
    palette.nebulae[2] = {0.2f, 0.8f, 0.9f};
    palette.armA = {1.0f, 0.75f, 0.5f};
    palette.armB = {1.0f, 0.5f, 0.7f};

    std::vector<std::thread> bakers;
    bakers.emplace_back([this]() { bakeSun(bodies[0].textures, ALBOR_SEED, 256); });
    bakers.emplace_back([this, palette]() { skybox.bake(ALT_SKY_SEED, 512, palette); });
    bakers.emplace_back([this]() { bakeRocketBall(bodies[ALT_ARENA].textures, 512); });
    bakers.emplace_back([this]() { bakeCobalt(bodies[ALT_COBALT].textures, COBALT_SEED, 384); });
    for (std::thread& t : bakers) t.join();

    makeStars(ALT_STAR_SEED, ALT_STAR_COUNT);
    std::printf("Universo alterno listo: Albor, Arena (futbol) y Cobalto (anillos)\n");
}
