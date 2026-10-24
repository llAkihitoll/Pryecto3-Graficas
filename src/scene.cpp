#include "scene.h"
#include <algorithm>
#include <random>
#include "geometry.h"
#include "shaders.h"

const unsigned STAR_SEED = 2026;
const int STAR_COUNT = 1800;

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
    sun.mesh = makeIcosphere(3, Material{"Sol", Vec3{1.0f, 0.8f, 0.3f}});
    sun.shader = sunShader;
    sun.radius = 30.0f;
    sun.spinPeriod = 60.0f;
    bodies.push_back(sun);

    CelestialBody dune;
    dune.name = "Duna";
    dune.mesh = makeIcosphere(4, Material{"Duna", Vec3{0.85f, 0.55f, 0.32f}});
    dune.shader = planetShader;
    dune.radius = 10.0f;
    dune.orbitRadius = 140.0f;
    dune.orbitPeriod = 240.0f;
    dune.spinPeriod = 40.0f;
    dune.axialTilt = radians(15.0f);
    bodies.push_back(dune);

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

void Scene::drawBodies(Framebuffer& fb, Uniforms& u, RenderStats& stats) const {
    for (const CelestialBody& body : bodies) {
        u.model = body.modelMatrix(u.time);
        drawMesh(fb, body.mesh, u, body.shader, stats);
    }
}
