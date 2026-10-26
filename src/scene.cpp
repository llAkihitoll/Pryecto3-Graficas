#include "scene.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include <thread>
#include <utility>
#include "geometry.h"
#include "shaders.h"

// Semillas fijas: con la misma semilla se generan las mismas superficies.
const unsigned STAR_SEED = 2026;
const unsigned SUN_SEED = 7;
const unsigned DUNE_SEED = 42;
const unsigned GLACIA_SEED = 1337;
const unsigned VERGEL_SEED = 99;
const unsigned AURELIA_SEED = 314;
const unsigned IGNIS_SEED = 666;
const int STAR_COUNT = 5000;
const unsigned SKY_SEED = 31;

// Subdivisiones de cada nivel de detalle y desde cuantos pixeles de radio
// en pantalla se usa. Mientras mas grande se ve, mas triangulos necesita
// para que el borde no se vea poligonal.
const int LOD_SUBDIVISIONS[] = {5, 4, 3, 2};       // 20480, 5120, 1280, 320 triangulos
const float LOD_MIN_PIXELS[] = {250.0f, 80.0f, 25.0f, 0.0f};
const int LOD_COUNT = 4;

void CelestialBody::buildLods(const Material& material) {
    lods.clear();
    for (int i = 0; i < LOD_COUNT; ++i) lods.push_back(makeIcosphere(LOD_SUBDIVISIONS[i], material));
    if (shape) {
        sphereLods = lods;
        for (Mesh& m : lods) displaceSphere(m, shape);
    }
}

int CelestialBody::chooseLod(float screenRadius) const {
    for (int i = 0; i < LOD_COUNT; ++i) {
        if (screenRadius >= LOD_MIN_PIXELS[i]) return i;
    }
    return LOD_COUNT - 1;
}

Vec3 CelestialBody::position(float time) const {
    if (orbitPeriod <= 0.0f) return Vec3{0, 0, 0};
    float angle = orbitPhase + 2.0f * PI * time / orbitPeriod;
    return Vec3{orbitRadius * std::sin(angle), 0.0f, orbitRadius * std::cos(angle)};
}

Vec3 CelestialBody::velocity(float time) const {
    if (orbitPeriod <= 0.0f) return Vec3{0, 0, 0};
    float omega = 2.0f * PI / orbitPeriod; // radianes por segundo
    float angle = orbitPhase + omega * time;
    return Vec3{orbitRadius * omega * std::cos(angle), 0.0f, -orbitRadius * omega * std::sin(angle)};
}

// Orden de las transformaciones (de derecha a izquierda): escalar la esfera
// unitaria al radio, girarla sobre su eje, inclinar el eje y llevarla a su
// lugar en la orbita.
float CelestialBody::spinAngle(float time) const { return spinPeriod > 0.0f ? 2.0f * PI * time / spinPeriod : 0.0f; }

Mat4 CelestialBody::frameMatrix(float time) const {
    return translate(position(time)) * rotateZ(axialTilt) * rotateY(spinAngle(time));
}

Mat4 CelestialBody::modelMatrix(float time) const { return frameMatrix(time) * scale(radius); }

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Ruidos del terreno, con las mismas semillas que el horneado de las
// texturas, para que la forma y los objetos coincidan con lo que se ve.
static const Noise& duneTerrain() {
    static Noise noise(DUNE_SEED);
    return noise;
}

// Que tanto se hunden o levantan los vertices de Duna, en radios del planeta
// (0.09 * radio 10 = hasta ~0.9 unidades).
const float DUNE_RELIEF = 0.09f;

// Forma deformada de Duna: la misma altura que se uso para la textura y el
// mapa de normales, ahora tambien moviendo la geometria.
static float duneShape(const Vec3& dir) {
    return 1.0f + DUNE_RELIEF * clampf(duneHeight(duneTerrain(), dir), -1.0f, 1.0f);
}

void Scene::buildProps(unsigned seed) {
    propSeed = seed;
    // El terreno usa las semillas de los planetas (para que coincida con la
    // textura); la distribucion usa la semilla de objetos.
    Noise density(seed);
    const Noise& duneTerrain = ::duneTerrain();
    Noise glaciaTerrain(GLACIA_SEED), vergelTerrain(VERGEL_SEED);
    std::vector<const Mesh*> rocks;
    for (const Mesh& r : rockModels) rocks.push_back(&r);

    // Duna: balizas en las tierras altas y rocas fuera de los campos de dunas
    // y del fondo de los crateres grandes.
    std::vector<PropInstance> dune;
    scatterProps(ScatterRule{{&beaconModel}, 300, 0.55f, 0.5f, 0.6f, 0.8f,
                             [&](const Vec3& p) { return smoothstep(0.08f, 0.25f, duneBaseRelief(duneTerrain, p)); }},
                 seed + 1, density, dune);
    scatterProps(ScatterRule{rocks, 400, 0.45f, 0.1f, 0.3f, 0.85f,
                             [&](const Vec3& p) {
                                 if (duneCrater(duneTerrain, p) < -0.3f) return 0.0f;
                                 return smoothstep(-0.2f, 0.0f, duneBaseRelief(duneTerrain, p));
                             }},
                 seed + 2, density, dune);

    // Glacia: grupos de cristales que brotan sobre las grietas.
    std::vector<PropInstance> glacia;
    scatterProps(ScatterRule{{&crystalModel}, 1500, 0.4f, 0.1f, 0.35f, 0.8f,
                             [&](const Vec3& p) { return glaciaCrack(glaciaTerrain, p); }},
                 seed + 3, density, glacia);

    // Vergel: arboles en tierra firme (no en el mar ni en las montanas) y
    // rocas en las montanas.
    std::vector<PropInstance> vergel;
    scatterProps(ScatterRule{{&treeModel}, 1500, 0.42f, 0.055f, 0.4f, 0.75f,
                             [&](const Vec3& p) {
                                 float e = vergelElevation(vergelTerrain, p);
                                 return smoothstep(VERGEL_SEA_LEVEL + 0.03f, VERGEL_SEA_LEVEL + 0.06f, e) *
                                        (1.0f - smoothstep(0.2f, 0.26f, e));
                             }},
                 seed + 4, density, vergel);
    scatterProps(ScatterRule{rocks, 400, 0.4f, 0.07f, 0.4f, 1.0f,
                             [&](const Vec3& p) { return smoothstep(0.22f, 0.3f, vergelElevation(vergelTerrain, p)); }},
                 seed + 5, density, vergel);

    // Ignis: rocas sobre el basalto, nunca sobre la lava.
    std::vector<PropInstance> ignis;
    Noise ignisTerrain(IGNIS_SEED);
    scatterProps(ScatterRule{rocks, 250, 0.5f, 0.16f, 0.35f, 0.8f,
                             [&](const Vec3& p) { return ignisLava(ignisTerrain, p) > 0.05f ? 0.0f : 1.0f; }},
                 seed + 6, density, ignis);

    const std::vector<PropInstance>* lists[] = {nullptr, &dune, &glacia, &vergel, nullptr, &ignis};
    const float heights[] = {0.0f, 3.6f, 2.0f, 2.7f, 0.0f, 1.0f}; // altura maxima de cada planeta (modelo * escala)
    std::printf("Objetos de superficie (semilla %u):", seed);
    for (int i = 1; i < (int)bodies.size(); ++i) {
        if (!lists[i]) continue; // Aurelia es de gas: no tiene objetos encima
        // Los objetos se apoyan en la altura real del terreno si esta deformado.
        bodies[i].props = mergeProps(*lists[i], bodies[i].radius, deformTerrain ? bodies[i].shape : nullptr);
        bodies[i].propCount = (int)lists[i]->size();
        bodies[i].propHeight = heights[i];
        std::printf(" %s %d (%zu triangulos)", bodies[i].name.c_str(), bodies[i].propCount,
                    bodies[i].props.triangles.size());
    }
    std::printf("\n");
}

void Scene::build(unsigned seed) {
    CelestialBody sun;
    sun.name = "Sol";
    sun.buildLods(Material{"Sol", Vec3{1.0f, 0.8f, 0.3f}});
    sun.shader = sunShader;
    sun.radius = 30.0f;
    sun.spinPeriod = 60.0f;
    sun.surfaceGravity = 4.0f; // suave: si no, el carro quieto se iria cayendo al sol
    bodies.push_back(std::move(sun));

    CelestialBody dune;
    dune.name = "Duna";
    dune.shape = duneShape;
    dune.buildLods(Material{"Duna", Vec3{0.85f, 0.55f, 0.32f}});
    dune.shader = duneShader;
    dune.radius = 10.0f;
    dune.orbitRadius = 140.0f;
    dune.orbitPeriod = keplerPeriod(dune.orbitRadius);
    dune.spinPeriod = 40.0f;
    dune.axialTilt = radians(15.0f);
    dune.surfaceGravity = 10.0f;
    dune.orbitColor = {0.95f, 0.6f, 0.3f};
    bodies.push_back(std::move(dune));

    // Las fases iniciales estan elegidas para que, desde donde arranca el
    // carro, Glacia y Vergel se vean a los lados de Duna.
    CelestialBody glacia;
    glacia.name = "Glacia";
    glacia.buildLods(Material{"Glacia", Vec3{0.8f, 0.9f, 1.0f}});
    glacia.shader = glaciaShader;
    glacia.radius = 8.0f;
    glacia.orbitRadius = 230.0f;
    glacia.orbitPeriod = keplerPeriod(glacia.orbitRadius);
    glacia.orbitPhase = 0.83f;
    glacia.spinPeriod = 50.0f;
    glacia.axialTilt = radians(-25.0f);
    glacia.surfaceGravity = 7.0f;
    glacia.orbitColor = {0.55f, 0.85f, 1.0f};
    bodies.push_back(std::move(glacia));

    CelestialBody vergel;
    vergel.name = "Vergel";
    vergel.buildLods(Material{"Vergel", Vec3{0.3f, 0.6f, 0.3f}});
    vergel.shader = vergelShader;
    vergel.radius = 13.0f;
    vergel.orbitRadius = 330.0f;
    vergel.orbitPeriod = keplerPeriod(vergel.orbitRadius);
    vergel.orbitPhase = 0.2f;
    vergel.spinPeriod = 70.0f;
    vergel.axialTilt = radians(10.0f);
    vergel.surfaceGravity = 12.0f;
    vergel.orbitColor = {0.4f, 0.9f, 0.45f};
    vergel.cloudPeriod = 150.0f;
    bodies.push_back(std::move(vergel));

    CelestialBody aurelia;
    aurelia.name = "Aurelia";
    aurelia.buildLods(Material{"Aurelia", Vec3{0.9f, 0.75f, 0.55f}});
    aurelia.shader = aureliaShader;
    aurelia.radius = 18.0f;
    aurelia.orbitRadius = 450.0f;
    aurelia.orbitPeriod = keplerPeriod(aurelia.orbitRadius);
    aurelia.orbitPhase = 0.55f;
    aurelia.spinPeriod = 90.0f;
    aurelia.axialTilt = radians(20.0f);
    aurelia.surfaceGravity = 14.0f;
    aurelia.orbitColor = {0.95f, 0.8f, 0.55f};
    // Tres anillos con huecos entre ellos (los huecos son geometria real).
    aurelia.ring.materials = {Material{"Anillo interior", {0.70f, 0.62f, 0.50f}},
                              Material{"Anillo medio", {0.90f, 0.82f, 0.68f}},
                              Material{"Anillo exterior", {0.60f, 0.52f, 0.42f}}};
    addRing(aurelia.ring, 1.35f, 1.65f, 128, 0);
    addRing(aurelia.ring, 1.72f, 2.15f, 128, 1);
    addRing(aurelia.ring, 2.22f, 2.45f, 128, 2);
    aurelia.ringOuter = 2.45f;
    bodies.push_back(std::move(aurelia));

    CelestialBody ignis;
    ignis.name = "Ignis";
    ignis.buildLods(Material{"Ignis", Vec3{0.3f, 0.2f, 0.15f}});
    ignis.shader = ignisShader;
    ignis.radius = 9.0f;
    ignis.orbitRadius = 85.0f;
    ignis.orbitPeriod = keplerPeriod(ignis.orbitRadius);
    ignis.orbitPhase = 2.4f;
    ignis.spinPeriod = 35.0f;
    ignis.axialTilt = radians(5.0f);
    ignis.surfaceGravity = 9.0f;
    ignis.orbitColor = {1.0f, 0.35f, 0.1f};
    bodies.push_back(std::move(ignis));

    // Hornear las superficies de todos los cuerpos al mismo tiempo (cada
    // horneado ademas reparte sus 6 caras en hilos). Los cuerpos ya estan
    // todos en el vector, asi que no se mueven de memoria mientras tanto.
    std::vector<std::thread> bakers;
    bakers.emplace_back([this]() { bakeSun(bodies[0].textures, SUN_SEED, 256); });
    bakers.emplace_back([this]() { skybox.bake(SKY_SEED, 512); });
    bakers.emplace_back([this]() { bakeDune(bodies[1].textures, DUNE_SEED, 512); });
    bakers.emplace_back([this]() { bakeGlacia(bodies[2].textures, GLACIA_SEED, 512); });
    bakers.emplace_back([this]() { bakeVergel(bodies[3].textures, VERGEL_SEED, 512); });
    bakers.emplace_back([this]() { bakeAurelia(bodies[4].textures, AURELIA_SEED, 512); });
    bakers.emplace_back([this]() { bakeIgnis(bodies[5].textures, IGNIS_SEED, 512); });
    for (std::thread& t : bakers) t.join();

    for (unsigned i = 0; i < 3; ++i) rockModels.push_back(makeRock(100 + i)); // 3 variantes de roca
    treeModel = makeAlienTree();
    crystalModel = makeCrystalCluster();
    beaconModel = makeBeacon();
    buildProps(seed);

    // Direcciones uniformes sobre la esfera: z uniforme en [-1, 1] y un
    // angulo uniforme alrededor. La semilla fija da siempre el mismo cielo.
    std::mt19937 rng(STAR_SEED);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < STAR_COUNT; ++i) {
        float z = unit(rng) * 2.0f - 1.0f;
        float a = unit(rng) * 2.0f * PI;
        float r = std::sqrt(1.0f - z * z);
        // Muchas estrellas debiles y pocas brillantes.
        float b = 0.15f + 0.85f * std::pow(unit(rng), 4.0f);
        // Color segun su temperatura: pocas azules (calientes), muchas
        // blancas, algunas amarillas y pocas anaranjadas (frias).
        float t = unit(rng);
        Vec3 color = t < 0.15f ? Vec3{0.65f, 0.75f, 1.0f}
                   : t < 0.65f ? Vec3{1.0f, 1.0f, 1.0f}
                   : t < 0.9f  ? Vec3{1.0f, 0.92f, 0.7f}
                               : Vec3{1.0f, 0.7f, 0.45f};
        stars.push_back(Star{Vec3{r * std::cos(a), r * std::sin(a), z}, b, color});
    }
}

void Scene::drawStars(Framebuffer& fb, const Uniforms& u) const {
    const int W = fb.width(), H = fb.height();
    for (const Star& s : stars) {
        // Direccion a espacio de camara: w = 0 ignora la posicion de la
        // camara, por eso las estrellas no se acercan aunque la nave avance.
        Vec3 v = u.view.transformDir(s.direction);
        if (v.z > -0.01f) continue; // detras de la camara
        Vec4 c = u.projection * Vec4(v, 1.0f);
        int x = (int)((c.x / c.w * 0.5f + 0.5f) * W);
        int y = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * H);
        if (x < 2 || y < 2 || x >= W - 2 || y >= H - 2) continue;

        // Se suma al cielo (mezcla aditiva) para que las estrellas tenues
        // encima de una nebulosa no la tapen con un punto negro.
        auto add = [&](int px, int py, float k) {
            uint32_t p = fb.getPixel(px, py);
            int r = std::min(255, (int)((p >> 16) & 0xFF) + (int)(s.color.x * k * 255.0f));
            int g = std::min(255, (int)((p >> 8) & 0xFF) + (int)(s.color.y * k * 255.0f));
            int b = std::min(255, (int)(p & 0xFF) + (int)(s.color.z * k * 255.0f));
            fb.setPixel(px, py, 0xFF000000u | (r << 16) | (g << 8) | b);
        };
        add(x, y, s.brightness);
        if (s.brightness > 0.75f) { // las mas brillantes: una cruz de destello
            float k = s.brightness * 0.45f;
            add(x + 1, y, k); add(x - 1, y, k); add(x, y + 1, k); add(x, y - 1, k);
            if (s.brightness > 0.92f) {
                add(x + 2, y, k * 0.4f); add(x - 2, y, k * 0.4f); add(x, y + 2, k * 0.4f); add(x, y - 2, k * 0.4f);
            }
        }
    }
}

void Scene::drawOrbits(Framebuffer& fb, const Uniforms& u) const {
    // La orbita es el mismo circulo que recorre position(): radio orbitRadius
    // en el plano XZ, alrededor del sol (que esta en el origen). Se dibuja
    // como puntos repartidos sobre el circulo, mas apagados que el planeta.
    const int DOTS = 512;
    for (const CelestialBody& b : bodies) {
        if (b.orbitPeriod <= 0.0f) continue; // el sol no orbita
        Vec3 c = b.orbitColor * 0.6f;
        uint32_t argb = 0xFF000000u | ((uint32_t)(c.x * 255) << 16) | ((uint32_t)(c.y * 255) << 8) | (uint32_t)(c.z * 255);
        for (int i = 0; i < DOTS; ++i) {
            float a = 2.0f * PI * i / DOTS;
            Vec3 p{b.orbitRadius * std::sin(a), 0.0f, b.orbitRadius * std::cos(a)};
            drawPoint3D(fb, u, p, argb, 2);
        }
    }
}

void Scene::drawBodies(Framebuffer& fb, Uniforms& u, RenderStats& stats, const Optimizations& opt) const {
    for (const CelestialBody& body : bodies) {
        Vec3 center = body.position(u.time);

        // Descarte: si la esfera del cuerpo no toca el volumen de vision,
        // no se transforma ni un solo vertice.
        // Los anillos sobresalen mucho del planeta: se prueban por separado
        // (se pueden ver los anillos aunque el planeta este fuera de cuadro).
        bool ringVisible = !body.ring.triangles.empty() &&
                           (!opt.cullObjects || sphereInFrustum(u, center, body.radius * body.ringOuter));
        if (ringVisible) {
            u.model = body.modelMatrix(u.time);
            u.objectCenter = center;
            u.objectRadius = body.radius;
            bool cull = u.cullBackFaces;
            u.cullBackFaces = false; // se ven por arriba y por abajo
            drawMesh(fb, body.ring, u, ringShader, stats);
            u.cullBackFaces = cull;
        }

        if (opt.cullObjects && !sphereInFrustum(u, center, body.radius)) {
            stats.objectsCulled++;
            body.currentLod = -1;
            continue;
        }
        stats.objectsDrawn++;

        // Nivel de detalle segun el tamano en pantalla (que depende de la distancia).
        body.currentLod = opt.autoLod ? body.chooseLod(projectedRadius(u, center, body.radius, fb.height())) : 0;

        u.model = body.modelMatrix(u.time);
        u.textures = &body.textures;
        u.textureMotion = body.cloudPeriod > 0.0f ? rotateY(2.0f * PI * u.time / body.cloudPeriod) : Mat4::identity();
        bool flat = !deformTerrain && !body.sphereLods.empty();
        drawMesh(fb, (flat ? body.sphereLods : body.lods)[body.currentLod], u, body.shader, stats);

        // Objetos de superficie: solo de cerca (LOD 0 o 1). Se dibujan con la
        // matriz del planeta sin escala, asi giran y orbitan junto con el.
        bool near = body.currentLod <= 1;
        if (!body.props.triangles.empty() && near &&
            (!opt.cullObjects || sphereInFrustum(u, center, body.radius + body.propHeight))) {
            u.model = body.frameMatrix(u.time);
            u.objectCenter = center;
            drawMesh(fb, body.props, u, propShader, stats);
            stats.propsDrawn += body.propCount;
        }
    }
    u.textures = nullptr;
}
