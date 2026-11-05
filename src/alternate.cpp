#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
#include "blackhole.h"
#include "arcade.h"
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

    // Planetas de arcade, mas lejos: Pac-Man (una esfera amarilla con la boca
    // abierta) y Galaga (con la forma de la nave del juego).
    CelestialBody pac;
    pac.name = "Pac-Man";
    pac.buildLods(Material{"Pac-Man", Vec3{1.0f, 0.85f, 0.1f}});
    pac.shader = voxelShader;
    pac.radius = 14.0f;
    pac.orbitRadius = 470.0f;
    pac.orbitPeriod = keplerPeriod(pac.orbitRadius);
    pac.orbitPhase = 2.3f;
    pac.spinPeriod = 40.0f;
    pac.surfaceGravity = 8.0f;
    pac.orbitColor = {1.0f, 0.85f, 0.2f};
    pac.cruiseAltitude = 1.7f; // se aterriza en su juego
    bodies.push_back(std::move(pac));

    CelestialBody galaga;
    galaga.name = "Galaga";
    galaga.buildLods(Material{"Galaga", Vec3{0.9f, 0.9f, 1.0f}});
    galaga.shader = voxelShader;
    galaga.radius = 18.0f;
    galaga.orbitRadius = 560.0f;
    galaga.orbitPeriod = keplerPeriod(galaga.orbitRadius);
    galaga.orbitPhase = 4.0f;
    galaga.spinPeriod = 60.0f;
    galaga.axialTilt = 1.25f; // casi de cara al plano de las orbitas: se ve la forma de la nave
    galaga.surfaceGravity = 8.0f;
    galaga.orbitColor = {0.4f, 0.9f, 1.0f};
    galaga.cruiseAltitude = 1.7f;
    bodies.push_back(std::move(galaga));

    // Mas planetas de arcade, todavia mas lejos: una oruga (Centipede) y una
    // serpiente enrollada (Snake).
    CelestialBody centipede;
    centipede.name = "Centipede";
    centipede.buildLods(Material{"Centipede", Vec3{0.35f, 0.9f, 0.3f}});
    centipede.shader = voxelShader;
    centipede.radius = 16.0f;
    centipede.orbitRadius = 650.0f;
    centipede.orbitPeriod = keplerPeriod(centipede.orbitRadius);
    centipede.orbitPhase = 5.4f;
    centipede.spinPeriod = 50.0f;
    centipede.axialTilt = 0.3f;
    centipede.surfaceGravity = 8.0f;
    centipede.orbitColor = {0.4f, 1.0f, 0.4f};
    centipede.cruiseAltitude = 1.7f;
    bodies.push_back(std::move(centipede));

    CelestialBody snake;
    snake.name = "Snake";
    snake.buildLods(Material{"Snake", Vec3{0.3f, 0.85f, 0.35f}});
    snake.shader = voxelShader;
    snake.radius = 17.0f;
    snake.orbitRadius = 740.0f;
    snake.orbitPeriod = keplerPeriod(snake.orbitRadius);
    snake.orbitPhase = 0.6f;
    snake.spinPeriod = 45.0f;
    snake.axialTilt = 0.5f;
    snake.surfaceGravity = 8.0f;
    snake.orbitColor = {0.9f, 1.0f, 0.4f};
    snake.cruiseAltitude = 1.7f;
    bodies.push_back(std::move(snake));

    // El ultimo y mas lejano: una cabeza de demonio (juego Infierno, estilo Doom).
    CelestialBody demon;
    demon.name = "Doom";
    demon.buildLods(Material{"Doom", Vec3{0.75f, 0.12f, 0.1f}});
    demon.shader = voxelShader;
    demon.radius = 18.0f;
    demon.orbitRadius = 830.0f;
    demon.orbitPeriod = keplerPeriod(demon.orbitRadius);
    demon.orbitPhase = 3.1f;
    demon.spinPeriod = 55.0f;
    demon.surfaceGravity = 8.0f;
    demon.orbitColor = {1.0f, 0.35f, 0.25f};
    demon.cruiseAltitude = 1.7f;
    bodies.push_back(std::move(demon));

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
        // Pac-Man: amarillo, con la boca (una cuna entre las quijadas, hacia
        // +X) cortada hasta el centro y un ojo negro de cada lado.
        auto mouth = [](const Vec3& p) { return p.x > 0.0f && std::fabs(std::atan2(p.y, p.x)) < 0.55f; };
        const Vec3 eyeA = normalize(Vec3{0.35f, 0.72f, 0.45f}), eyeB = normalize(Vec3{0.35f, 0.72f, -0.45f});
        bodies[ALT_PACMAN].lods.clear();
        bodies[ALT_PACMAN].lods.push_back(makeVoxelPlanet(40, [&](const Vec3& d) {
            bool eye = dot(d, eyeA) > 0.985f || dot(d, eyeB) > 0.985f;
            return VoxelCell{eye ? Vec3{0.05f, 0.03f, 0.08f} : Vec3{1.0f, 0.85f, 0.1f}, 0, 0.0f};
        }, mouth));
    });
    builders.emplace_back([&]() {
        // Galaga: el sprite de la nave visto desde arriba (plano XZ), con
        // grosor: el cuerpo blanco del centro mas alto que las alas.
        const std::vector<std::string>& ship = galagaFighterSprite();
        const int rows = (int)ship.size(), cols = (int)ship[0].size();
        const float half = 1.4f;
        auto pixel = [&](const Vec3& p) -> char {
            int c = (int)std::floor((p.x + half) / (2.0f * half) * cols);
            int r = (int)std::floor((p.z + half) / (2.0f * half) * rows);
            if (c < 0 || r < 0 || c >= cols || r >= rows) return '.';
            return ship[r][c];
        };
        auto thickness = [&](const Vec3& p, char ch) {
            int c = (int)std::floor((p.x + half) / (2.0f * half) * cols);
            if (ch == 'W') return std::abs(c - cols / 2) <= 1 ? 0.42f : 0.22f;
            return ch == 'B' ? 0.3f : 0.16f;
        };
        bodies[ALT_GALAGA].lods.clear();
        bodies[ALT_GALAGA].lods.push_back(makeVoxelSolid(40, half + 0.1f,
            [&](const Vec3& p) {
                char ch = pixel(p);
                return ch != '.' && std::fabs(p.y) < thickness(p, ch);
            },
            [&](const Vec3& p) {
                char ch = pixel(p);
                Vec3 c = ch == 'R' ? Vec3{1.0f, 0.15f, 0.2f} : (ch == 'B' ? Vec3{0.2f, 0.35f, 1.0f} : Vec3{0.92f, 0.92f, 1.0f});
                return VoxelCell{c, 0, ch == 'R' ? 0.4f : 0.0f};
            }));
    });
    builders.emplace_back([&]() {
        // Centipede: una oruga de 7 segmentos (esferas) en zigzag a lo largo
        // de X, con patitas debajo, y la cabeza rosa con ojos y antenas.
        const int count = 7;
        Vec3 centers[count];
        float radii[count];
        for (int k = 0; k < count; ++k) {
            centers[k] = Vec3{-1.15f + k * 0.38f, 0.0f, 0.3f * std::sin(k * 1.1f)};
            radii[k] = k == count - 1 ? 0.36f : 0.3f;
        }
        const Vec3 headC = centers[count - 1];
        auto nearSegment = [&](const Vec3& p, int& which) {
            which = -1;
            float best = 1e9f;
            for (int k = 0; k < count; ++k) {
                float d = length(p - centers[k]) - radii[k];
                if (d < best) {
                    best = d;
                    which = k;
                }
            }
            return best;
        };
        auto leg = [&](const Vec3& p) {
            for (int k = 0; k < count - 1; ++k) {
                Vec3 d = p - centers[k];
                if (std::fabs(d.x) < 0.07f && d.y < -0.18f && d.y > -0.5f && std::fabs(std::fabs(d.z) - 0.2f) < 0.07f) return true;
            }
            return false;
        };
        auto antenna = [&](const Vec3& p) {
            for (float side = -1.0f; side <= 1.0f; side += 2.0f) {
                Vec3 a = headC + Vec3{0.12f, 0.28f, side * 0.12f}, b = headC + Vec3{0.32f, 0.62f, side * 0.3f};
                Vec3 ab = b - a;
                float t = clampf(dot(p - a, ab) / dot(ab, ab), 0.0f, 1.0f);
                if (length(p - (a + ab * t)) < 0.05f) return true;
            }
            return false;
        };
        bodies[ALT_CENTIPEDE].lods.clear();
        bodies[ALT_CENTIPEDE].lods.push_back(makeVoxelSolid(44, 1.6f,
            [&](const Vec3& p) {
                int k;
                return nearSegment(p, k) < 0.0f || leg(p) || antenna(p);
            },
            [&](const Vec3& p) {
                int k;
                float d = nearSegment(p, k);
                if (d > 0.02f) return VoxelCell{Vec3{0.15f, 0.25f, 0.1f}, 0, 0.0f}; // patas y antenas
                if (k == count - 1) {
                    Vec3 o = normalize(p - headC);
                    // Ojos al frente (+X), un poco arriba.
                    bool eye = o.x > 0.6f && o.y > 0.15f && std::fabs(std::fabs(o.z) - 0.4f) < 0.16f;
                    return VoxelCell{eye ? Vec3{0.05f, 0.05f, 0.08f} : Vec3{1.0f, 0.4f, 0.6f}, 0, 0.0f};
                }
                Vec3 o = normalize(p - centers[k]);
                bool spot = o.y > 0.75f; // manchas amarillas arriba de cada segmento
                Vec3 green = k % 2 ? Vec3{0.25f, 0.7f, 0.22f} : Vec3{0.4f, 0.92f, 0.32f};
                return VoxelCell{spot ? Vec3{1.0f, 0.9f, 0.3f} : green, 0, 0.0f};
            }));
    });
    builders.emplace_back([&]() {
        // Snake: un tubo que se enrolla en espiral plana (la cola al centro) y
        // que al final levanta la cabeza como cobra, con ojos y lengua bifida.
        const int samples = 220;
        std::vector<Vec3> path(samples);
        std::vector<float> radius(samples);
        for (int i = 0; i < samples; ++i) {
            float t = (float)i / (samples - 1);
            float r = 0.22f + 0.95f * t, angle = 3.2f * PI * t;
            float lift = t > 0.86f ? (t - 0.86f) / 0.14f : 0.0f;
            path[i] = Vec3{r * std::cos(angle), 0.55f * lift * lift, r * std::sin(angle)};
            radius[i] = 0.06f + 0.14f * t;
        }
        const Vec3 head = path[samples - 1];
        const Vec3 forward = normalize(path[samples - 1] - path[samples - 4]);
        auto closest = [&](const Vec3& p, int& which) {
            float best = 1e9f;
            which = 0;
            for (int i = 0; i < samples; i += 1) {
                float d = length(p - path[i]) - radius[i];
                if (d < best) {
                    best = d;
                    which = i;
                }
            }
            return best;
        };
        auto tongue = [&](const Vec3& p) {
            Vec3 a = head + forward * 0.18f, b = head + forward * 0.42f;
            Vec3 ab = b - a;
            float t = clampf(dot(p - a, ab) / dot(ab, ab), 0.0f, 1.0f);
            return length(p - (a + ab * t)) < 0.04f;
        };
        bodies[ALT_SNAKE].lods.clear();
        bodies[ALT_SNAKE].lods.push_back(makeVoxelSolid(44, 1.45f,
            [&](const Vec3& p) {
                if (p.y < -0.3f || p.y > 0.85f) return false;
                int i;
                return closest(p, i) < 0.0f || length(p - head) < 0.21f || tongue(p);
            },
            [&](const Vec3& p) {
                int i;
                float d = closest(p, i);
                if (tongue(p) && d > 0.0f) return VoxelCell{Vec3{1.0f, 0.2f, 0.3f}, 0, 0.5f};
                if (length(p - head) < 0.23f) {
                    // Dos ojos chicos: adelante, arriba y a cada lado.
                    Vec3 o = normalize(p - head);
                    Vec3 side = normalize(cross(forward, Vec3{0, 1, 0}));
                    Vec3 up = Vec3{0, 1, 0};
                    bool eye = dot(o, normalize(forward * 0.7f + up * 0.5f + side * 0.55f)) > 0.94f ||
                               dot(o, normalize(forward * 0.7f + up * 0.5f - side * 0.55f)) > 0.94f;
                    return VoxelCell{eye ? Vec3{1.0f, 0.85f, 0.1f} : Vec3{0.3f, 0.85f, 0.35f}, 0, eye ? 0.6f : 0.0f};
                }
                // Escamas: rayas a lo largo del cuerpo y la panza amarilla.
                bool belly = p.y < path[i].y - radius[i] * 0.4f;
                bool stripe = (i / 6) % 2 == 0;
                Vec3 c = belly ? Vec3{0.95f, 0.85f, 0.4f} : (stripe ? Vec3{0.25f, 0.75f, 0.3f} : Vec3{0.12f, 0.45f, 0.2f});
                return VoxelCell{c, 0, 0.0f};
            }));
    });
    builders.emplace_back([&]() {
        // Doom: cabeza de demonio roja (la cara hacia +Z) con dos cuernos curvos de
        // hueso, orejas puntiagudas, ojos amarillos que brillan bajo unas cejas
        // oscuras y una boca abierta con colmillos y el fondo de lava.
        auto capsule = [](const Vec3& p, const Vec3& a, const Vec3& b, float ra, float rb) {
            Vec3 ab = b - a;
            float t = clampf(dot(p - a, ab) / dot(ab, ab), 0.0f, 1.0f);
            return length(p - (a + ab * t)) - (ra + (rb - ra) * t);
        };
        auto head = [](const Vec3& p) {
            Vec3 q{p.x, p.y / 0.92f, p.z / 0.95f};
            return dot(q, q) < 1.0f;
        };
        auto mouth = [](const Vec3& p) {
            float x = p.x / 0.6f;
            return p.z > 0.4f && std::fabs(p.x) < 0.6f && p.y < -0.2f && p.y > -0.58f + 0.28f * x * x;
        };
        // Cuernos: tubos que salen de arriba de la cabeza, se abren y se curvan hacia adentro.
        const int hornSamples = 30;
        std::vector<Vec3> horn[2];
        for (int side = 0; side < 2; ++side) {
            float s = side ? 1.0f : -1.0f;
            for (int i = 0; i < hornSamples; ++i) {
                float t = (float)i / (hornSamples - 1);
                horn[side].push_back(Vec3{s * (0.5f + 0.75f * t - 0.5f * t * t), 0.55f + 0.95f * t, -0.1f - 0.2f * t});
            }
        }
        auto hornAt = [&](const Vec3& p, float& along) {
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < hornSamples; ++i) {
                    float t = (float)i / (hornSamples - 1);
                    if (length(p - horn[side][i]) < 0.24f * (1.0f - t) + 0.035f) {
                        along = t;
                        return true;
                    }
                }
            return false;
        };
        auto ear = [&](const Vec3& p) {
            return capsule(p, Vec3{0.85f, 0.1f, -0.05f}, Vec3{1.35f, 0.4f, -0.25f}, 0.16f, 0.02f) < 0.0f ||
                   capsule(p, Vec3{-0.85f, 0.1f, -0.05f}, Vec3{-1.35f, 0.4f, -0.25f}, 0.16f, 0.02f) < 0.0f;
        };
        // Colmillos: conos que bajan del labio de arriba.
        auto fang = [&](const Vec3& p) {
            const float xs[4] = {-0.36f, -0.14f, 0.14f, 0.36f};
            for (float x : xs) {
                float z = 0.95f * std::sqrt(std::max(0.0f, 1.0f - x * x - 0.05f)) - 0.1f;
                if (capsule(p, Vec3{x, -0.18f, z}, Vec3{x, std::fabs(x) > 0.3f ? -0.4f : -0.34f, z}, 0.07f, 0.01f) < 0.0f)
                    return true;
            }
            return false;
        };
        const Vec3 eyeL = normalize(Vec3{-0.33f, 0.22f, 0.9f}), eyeR = normalize(Vec3{0.33f, 0.22f, 0.9f});
        bodies[ALT_DEMON].lods.clear();
        bodies[ALT_DEMON].lods.push_back(makeVoxelSolid(48, 1.75f,
            [&](const Vec3& p) {
                float along;
                return (head(p) && !mouth(p)) || hornAt(p, along) || ear(p) || fang(p);
            },
            [&](const Vec3& p) {
                float along;
                if (fang(p)) return VoxelCell{Vec3{0.96f, 0.93f, 0.85f}, 0, 0.1f};
                if (!head(p) && hornAt(p, along)) {
                    // Hueso claro que se oscurece hacia la punta, con anillos.
                    Vec3 c = lerp(Vec3{0.9f, 0.84f, 0.68f}, Vec3{0.25f, 0.18f, 0.18f}, along * along);
                    if ((int)(along * 9.0f) % 2) c = c * 0.85f;
                    return VoxelCell{c, 0, 0.0f};
                }
                if (ear(p) && !head(p)) return VoxelCell{Vec3{0.6f, 0.08f, 0.08f}, 0, 0.0f};
                // Adentro de la boca: brillo de lava.
                if (p.z > 0.3f && p.y < -0.15f && std::fabs(p.x) < 0.65f && length(p) < 0.97f)
                    return VoxelCell{Vec3{1.0f, 0.4f, 0.08f}, 0, 0.8f};
                Vec3 d = normalize(p);
                float l = dot(d, eyeL), r = dot(d, eyeR);
                if (l > 0.982f || r > 0.982f) {
                    bool pupil = std::fabs(d.x - (l > r ? eyeL.x : eyeR.x)) < 0.03f; // pupila de rendija
                    return VoxelCell{pupil ? Vec3{0.1f, 0.0f, 0.0f} : Vec3{1.0f, 0.9f, 0.15f}, 0, pupil ? 0.0f : 1.0f};
                }
                // Cejas: una franja oscura inclinada encima de cada ojo (enojado).
                if (d.z > 0.7f && std::fabs(d.x) > 0.12f && std::fabs(d.x) < 0.5f) {
                    float brow = 0.36f + 0.25f * (0.5f - std::fabs(d.x));
                    if (std::fabs(d.y - brow) < 0.05f) return VoxelCell{Vec3{0.25f, 0.03f, 0.05f}, 0, 0.0f};
                }
                // Piel roja con manchas mas oscuras.
                float spot = std::sin(p.x * 9.0f) * std::sin(p.y * 7.0f + 1.0f) * std::sin(p.z * 8.0f + 2.0f);
                return VoxelCell{spot > 0.35f ? Vec3{0.55f, 0.07f, 0.07f} : Vec3{0.78f, 0.13f, 0.1f}, 0, 0.0f};
            }));
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
    std::printf("Universo alterno listo: Albor, Arena, Entrenamiento, Pac-Man, Galaga, Centipede, Snake y Doom, de cubos (%zu triangulos)\n", tris);
}
