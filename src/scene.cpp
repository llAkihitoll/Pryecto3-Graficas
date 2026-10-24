#include "scene.h"
#include <algorithm>
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
const int STAR_COUNT = 1800;

// Subdivisiones de cada nivel de detalle y desde cuantos pixeles de radio
// en pantalla se usa. Mientras mas grande se ve, mas triangulos necesita
// para que el borde no se vea poligonal.
const int LOD_SUBDIVISIONS[] = {5, 4, 3, 2};       // 20480, 5120, 1280, 320 triangulos
const float LOD_MIN_PIXELS[] = {250.0f, 80.0f, 25.0f, 0.0f};
const int LOD_COUNT = 4;

void CelestialBody::buildLods(const Material& material) {
    lods.clear();
    for (int i = 0; i < LOD_COUNT; ++i) lods.push_back(makeIcosphere(LOD_SUBDIVISIONS[i], material));
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
Mat4 CelestialBody::modelMatrix(float time) const {
    float spin = spinPeriod > 0.0f ? 2.0f * PI * time / spinPeriod : 0.0f;
    return translate(position(time)) * rotateZ(axialTilt) * rotateY(spin) * scale(radius);
}

void Scene::build() {
    CelestialBody sun;
    sun.name = "Sol";
    sun.buildLods(Material{"Sol", Vec3{1.0f, 0.8f, 0.3f}});
    sun.shader = sunShader;
    sun.radius = 30.0f;
    sun.spinPeriod = 60.0f;
    bodies.push_back(std::move(sun));

    CelestialBody dune;
    dune.name = "Duna";
    dune.buildLods(Material{"Duna", Vec3{0.85f, 0.55f, 0.32f}});
    dune.shader = duneShader;
    dune.radius = 10.0f;
    dune.orbitRadius = 140.0f;
    dune.orbitPeriod = 240.0f;
    dune.spinPeriod = 40.0f;
    dune.axialTilt = radians(15.0f);
    bodies.push_back(std::move(dune));

    // Las fases iniciales estan elegidas para que, desde donde arranca el
    // carro, Glacia y Vergel se vean a los lados de Duna.
    CelestialBody glacia;
    glacia.name = "Glacia";
    glacia.buildLods(Material{"Glacia", Vec3{0.8f, 0.9f, 1.0f}});
    glacia.shader = glaciaShader;
    glacia.radius = 8.0f;
    glacia.orbitRadius = 230.0f;
    glacia.orbitPeriod = 360.0f;
    glacia.orbitPhase = 0.83f;
    glacia.spinPeriod = 50.0f;
    glacia.axialTilt = radians(-25.0f);
    bodies.push_back(std::move(glacia));

    CelestialBody vergel;
    vergel.name = "Vergel";
    vergel.buildLods(Material{"Vergel", Vec3{0.3f, 0.6f, 0.3f}});
    vergel.shader = vergelShader;
    vergel.radius = 13.0f;
    vergel.orbitRadius = 330.0f;
    vergel.orbitPeriod = 480.0f;
    vergel.orbitPhase = 0.2f;
    vergel.spinPeriod = 70.0f;
    vergel.axialTilt = radians(10.0f);
    vergel.cloudPeriod = 150.0f;
    bodies.push_back(std::move(vergel));

    // Hornear las superficies de todos los cuerpos al mismo tiempo (cada
    // horneado ademas reparte sus 6 caras en hilos). Los cuerpos ya estan
    // todos en el vector, asi que no se mueven de memoria mientras tanto.
    std::vector<std::thread> bakers;
    bakers.emplace_back([this]() { bakeSun(bodies[0].textures, SUN_SEED, 256); });
    bakers.emplace_back([this]() { bakeDune(bodies[1].textures, DUNE_SEED, 512); });
    bakers.emplace_back([this]() { bakeGlacia(bodies[2].textures, GLACIA_SEED, 512); });
    bakers.emplace_back([this]() { bakeVergel(bodies[3].textures, VERGEL_SEED, 512); });
    for (std::thread& t : bakers) t.join();

    // Direcciones uniformes sobre la esfera: z uniforme en [-1, 1] y un
    // angulo uniforme alrededor. La semilla fija da siempre el mismo cielo.
    std::mt19937 rng(STAR_SEED);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < STAR_COUNT; ++i) {
        float z = unit(rng) * 2.0f - 1.0f;
        float a = unit(rng) * 2.0f * PI;
        float r = std::sqrt(1.0f - z * z);
        // Muchas estrellas debiles y pocas brillantes.
        float b = 0.25f + 0.75f * std::pow(unit(rng), 3.0f);
        stars.push_back(Star{Vec3{r * std::cos(a), r * std::sin(a), z}, b});
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
        if (x < 1 || y < 1 || x >= W - 1 || y >= H - 1) continue;

        uint32_t g = (uint32_t)(s.brightness * 255.0f);
        uint32_t color = 0xFF000000u | (g << 16) | (g << 8) | std::min(255u, g + 30u);
        fb.setPixel(x, y, color);
        if (s.brightness > 0.8f) { // las mas brillantes se ven como una crucecita
            uint32_t dim = 0xFF000000u | ((g / 2) << 16) | ((g / 2) << 8) | (g / 2);
            fb.setPixel(x + 1, y, dim);
            fb.setPixel(x - 1, y, dim);
            fb.setPixel(x, y + 1, dim);
            fb.setPixel(x, y - 1, dim);
        }
    }
}

void Scene::drawBodies(Framebuffer& fb, Uniforms& u, RenderStats& stats, const Optimizations& opt) const {
    for (const CelestialBody& body : bodies) {
        Vec3 center = body.position(u.time);

        // Descarte: si la esfera del cuerpo no toca el volumen de vision,
        // no se transforma ni un solo vertice.
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
        drawMesh(fb, body.lods[body.currentLod], u, body.shader, stats);
    }
    u.textures = nullptr;
}
