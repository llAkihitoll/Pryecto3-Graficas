#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
#include "blackhole.h"
#include "geometry.h"
#include "rlball.h"
#include "noise.h"
#include "scene.h"
#include "voxels.h"

// Universo alterno: al que se llega cruzando el agujero negro. Tiene su
// propia estrella (blanca), su propio cielo y dos planetas con minijuego. La
// estrella, los planetas y la decoracion (estrellas y galaxias) son de cubos.

const unsigned ALT_STAR_SEED = 808;
const unsigned ALT_SKY_SEED = 77;
const unsigned ALBOR_SEED = 11;
const unsigned TRAINING_SEED = 23;
const unsigned GALAXY_SEED = 4040;

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

// --- Arena: un balon de Rocket League gigante ---

// --- Entrenamiento: el planeta de los anillos y los tiros de practica ---

// Roca azul marmoleada con vetas que brillan (bordes de las celdas de Worley).
static void bakeTraining(PlanetTextures& tex, unsigned seed, int size) {
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

// Cada cuerpo del universo alterno es de cubos, con un solo nivel de
// detalle: los cubos se ven del mismo tamano de cerca y de lejos (si se
// usaran menos cubos a lo lejos, se notaria que cambian). cell dice el color,
// el relieve y el brillo en cada direccion.
static void voxelize(CelestialBody& body, int across, const std::function<VoxelCell(const Vec3&)>& cell) {
    body.lods.clear();
    body.lods.push_back(makeVoxelPlanet(across, cell));
}

void Scene::drawVoxelDecor(Framebuffer& fb, Uniforms& u, RenderStats& stats) const {
    u.model = Mat4::identity();
    drawMesh(fb, voxelStars, u, voxelGlowShader, stats);
    for (size_t i = 0; i < voxelGalaxies.size(); ++i) {
        u.model = galaxyFrames[i] * rotateY(u.time * 0.02f * (i % 2 ? -1.0f : 1.0f));
        drawMesh(fb, voxelGalaxies[i], u, voxelGlowShader, stats);
    }
}

void Scene::buildAlternate() {
    alternate = true;

    CelestialBody star;
    star.name = "Albor";
    star.buildLods(Material{"Albor", Vec3{0.9f, 0.95f, 1.0f}});
    star.shader = voxelGlowShader;
    star.radius = 48.0f; // mucho mas grande que el sol del universo normal
    star.spinPeriod = 55.0f;
    star.surfaceGravity = 4.0f;
    bodies.push_back(std::move(star));

    // Los planetas de los minijuegos no giran sobre su eje (spinPeriod = 0):
    // asi la cancha y los anillos se quedan quietos respecto al planeta.
    CelestialBody arena;
    arena.name = "Arena";
    arena.buildLods(Material{"Arena", Vec3{0.2f, 0.6f, 0.25f}});
    arena.shader = voxelShader;
    arena.radius = 22.0f;
    arena.orbitRadius = 185.0f;
    arena.orbitPeriod = keplerPeriod(arena.orbitRadius);
    arena.orbitPhase = -0.25f;
    arena.surfaceGravity = 10.0f;
    arena.orbitColor = {0.4f, 0.95f, 0.5f};
    arena.cruiseAltitude = 1.7f; // baja solo hasta el suelo: al tocarlo se aterriza en el mundo abierto
    bodies.push_back(std::move(arena));

    CelestialBody training;
    training.name = "Entrenamiento";
    training.buildLods(Material{"Entrenamiento", Vec3{0.2f, 0.35f, 0.7f}});
    training.shader = voxelShader;
    training.radius = 16.0f;
    training.orbitRadius = 300.0f;
    training.orbitPeriod = keplerPeriod(training.orbitRadius);
    training.orbitPhase = 0.9f;
    training.surfaceGravity = 8.0f;
    training.orbitColor = {0.45f, 0.65f, 1.0f};
    training.cruiseAltitude = 1.7f; // tambien se aterriza en su mundo abierto
    bodies.push_back(std::move(training));

    // Agujero negro de regreso, arriba de Albor, con el disco violeta.
    bodies.push_back(makeBlackHole("Regreso", Vec3{0.0f, 420.0f, 0.0f}, 10.0f, Vec3{0.7f, 0.4f, 1.0f}));
    blackHoleIndex = (int)bodies.size() - 1;
    // Se llega (y se reaparece) en el plano de las orbitas, por fuera de la
    // de Entrenamiento, mirando hacia Arena: asi se ve el sistema al llegar.
    spawnPoint = arrivalPoint = Vec3{-80.0f, 30.0f, 390.0f};
    spawnLook = ALT_ARENA;

    // Cielo de fantasia en tonos morados: fondo violeta, Via Lactea lila y
    // nebulosas violeta, magenta y lavanda, mas intensas que las del universo normal.
    SkyPalette palette;
    palette.background = {0.07f, 0.025f, 0.13f};
    palette.band = {0.6f, 0.35f, 0.85f};
    palette.bandNormal = {-0.5f, 0.6f, 0.6f};
    palette.nebulae[0] = {0.65f, 0.25f, 1.0f};
    palette.nebulae[1] = {1.0f, 0.3f, 0.8f};
    palette.nebulae[2] = {0.5f, 0.45f, 1.0f};
    palette.armA = {0.85f, 0.6f, 1.0f};
    palette.armB = {1.0f, 0.55f, 0.85f};
    palette.nebulaStrength = 0.95f;
    palette.haze = {0.13f, 0.04f, 0.22f}; // bruma morada: ninguna parte del cielo queda negra

    std::vector<std::thread> bakers;
    bakers.emplace_back([this]() { bakeSun(bodies[0].textures, ALBOR_SEED, 256); });
    bakers.emplace_back([this, palette]() { skybox.bake(ALT_SKY_SEED, 512, palette); });
    bakers.emplace_back([this]() { bakeRocketBall(bodies[ALT_ARENA].textures, 512); });
    bakers.emplace_back([this]() { bakeTraining(bodies[ALT_TRAINING].textures, TRAINING_SEED, 384); });
    for (std::thread& t : bakers) t.join();

    // Superficies de cubos, a partir de las texturas recien horneadas (cada
    // cuerpo en su propio hilo).
    const PlanetTextures& alborTex = bodies[0].textures;
    const PlanetTextures& trainingTex = bodies[ALT_TRAINING].textures;
    std::vector<std::thread> builders;
    builders.emplace_back([&]() {
        // Albor: cubos blancos y azulados que brillan; los granulos calientes sobresalen.
        voxelize(bodies[0], 72, [&](const Vec3& d) {
            Vec3 s = alborTex.color.sample(d);
            float heat = 0.6f + 0.5f * s.x - std::max(0.0f, s.z - 0.4f);
            return VoxelCell{whitePlasma(heat), s.x > 0.62f ? 1 : 0, 1.0f};
        });
    });
    builders.emplace_back([&]() {
        // Arena: el balon de Rocket League en cubos; las placas y lentes
        // (lo que mas brilla) sobresalen una capa.
        voxelize(bodies[ALT_ARENA], 48, [&](const Vec3& d) {
            float shine;
            Vec3 c = rocketBallFlatColor(d, shine);
            return VoxelCell{c, shine > 0.7f ? 1 : 0, 0.0f};
        });
    });
    builders.emplace_back([&]() {
        // Entrenamiento: terreno azul escalonado (lo mas claro es lo mas alto) y
        // vetas cian que brillan.
        voxelize(bodies[ALT_TRAINING], 36, [&](const Vec3& d) {
            if (trainingTex.mask.sample(d) > 0.5f) return VoxelCell{Vec3{0.25f, 0.85f, 1.0f}, 0, 0.9f};
            Vec3 c = trainingTex.color.sample(d);
            float l = (c.x + c.y + c.z) / 3.0f;
            return VoxelCell{c, l > 0.36f ? 2 : (l > 0.24f ? 1 : 0), 0.0f};
        });
    });
    builders.emplace_back([&]() {
        // Estrellas y galaxias de decoracion, lejos de todo el sistema.
        voxelStars = makeVoxelStars(ALT_STAR_SEED, 700, 2600.0f, 4200.0f);
        voxelGalaxies.push_back(makeVoxelGalaxy(GALAXY_SEED, 1400, 750.0f, {1.0f, 0.9f, 1.0f}, {0.55f, 0.3f, 1.0f}));
        voxelGalaxies.push_back(makeVoxelGalaxy(GALAXY_SEED + 1, 1000, 520.0f, {1.0f, 0.85f, 0.95f}, {1.0f, 0.35f, 0.75f}));
        // El disco (eje Y local) mira casi hacia el sistema, un poco inclinado,
        // para que desde aqui se vea la espiral y no solo una raya.
        Vec3 dirA = normalize(Vec3{0.55f, 0.4f, -0.7f}), dirB = normalize(Vec3{-0.75f, -0.2f, 0.55f});
        galaxyFrames.push_back(basisFromY(normalize(-dirA + Vec3{0.3f, 0.2f, 0.0f}), dirA * 3700.0f));
        galaxyFrames.push_back(basisFromY(normalize(-dirB + Vec3{0.0f, 0.35f, 0.25f}), dirB * 3500.0f));
    });
    for (std::thread& t : builders) t.join();
    size_t tris = 0;
    for (const CelestialBody& b : bodies)
        if (!b.blackHole) tris += b.lods[0].triangles.size();
    std::printf("Universo alterno listo: Albor, Arena (futbol) y Entrenamiento (anillos y tiros), de cubos (%zu triangulos)\n", tris);
}
