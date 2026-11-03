#include "minigames.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "font.h"
#include "geometry.h"
#include "hud.h"
#include "shaders.h"

const int RACE_GATES = 14;
const float GATE_RADIUS = 2.6f; // radio del hueco del anillo

const uint32_t HUD_WHITE = 0xFFE8ECF4, HUD_GREY = 0xFF98A2B8, HUD_GOLD = 0xFFFFD860, HUD_GREEN = 0xFF60E880;

// Panel centrado arriba de la pantalla con varias lineas de texto.
static void centeredPanel(Framebuffer& fb, int y, int width, int height) {
    darkenRect(fb, (fb.width() - width) / 2, y, width, height, 0.55f);
}

static void centeredText(Framebuffer& fb, int y, const char* text, uint32_t color, int scale = 1) {
    drawText(fb, (fb.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

void RingRace::build(float radius) {
    // Camino: una vuelta al planeta que sube y baja de latitud (una "ola"),
    // inclinada para que no sea el ecuador. La altura de cada anillo tambien
    // cambia, entre ~3 y ~7 unidades sobre el suelo.
    auto pathPoint = [&](float theta) {
        Vec3 dir = normalize(Vec3{std::cos(theta), 0.38f * std::sin(2.0f * theta), std::sin(theta)});
        dir = normalize(rotateX(0.5f).transformDir(dir));
        float altitude = 5.0f + 1.8f * std::sin(3.0f * theta + 1.0f);
        return dir * (radius + altitude);
    };
    gates.clear();
    for (int i = 0; i < RACE_GATES; ++i) {
        float theta = 2.0f * PI * i / RACE_GATES;
        Gate g;
        g.position = pathPoint(theta);
        g.axis = normalize(pathPoint(theta + 0.01f) - pathPoint(theta - 0.01f));
        gates.push_back(g);
    }
    Material gold{"Anillo siguiente", {1.0f, 0.8f, 0.25f}};
    gold.emissive = 0.9f;
    gold.specular = 0.8f;
    gold.shininess = 48.0f;
    Material blue{"Anillo", {0.3f, 0.6f, 1.0f}};
    blue.emissive = 0.4f;
    nextRing = makeTorus(GATE_RADIUS + 0.3f, 0.3f, 32, 8, gold);
    laterRing = makeTorus(GATE_RADIUS + 0.3f, 0.3f, 32, 8, blue);
}

void RingRace::update(float dt, const Ship& ship, const CelestialBody& planet, float time) {
    if (finishFlash > 0.0f) finishFlash -= dt;
    Vec3 car = ship.position - planet.position(time);
    distanceToNext = length(gates[next].position - car);
    // Si el carro se va del planeta (o explota) la vuelta se cancela.
    if (ship.crashed || length(car) > planet.radius * 3.0f) {
        running = false;
        next = 0;
        return;
    }
    if (running) timer += dt;

    // Cruzar el anillo: estar cerca de su plano y dentro del hueco.
    const Gate& g = gates[next];
    Vec3 d = car - g.position;
    float along = dot(d, g.axis);
    float radial = length(d - g.axis * along);
    if (std::fabs(along) < 1.6f && radial < GATE_RADIUS) {
        if (next == 0) {
            running = true;
            timer = 0.0f;
        }
        next++;
        if (next == (int)gates.size()) {
            last = timer;
            if (best <= 0.0f || last < best) best = last;
            std::printf("Carrera de anillos: %.2f s (mejor %.2f s)\n", last, best);
            finishFlash = 4.0f;
            running = false;
            next = 0;
        }
    }
}

void RingRace::placeShipForTest(Ship& ship, const Vec3& planetCenter) const {
    const Gate& g = gates[next];
    ship.position = planetCenter + g.position - g.axis * 4.0f;
    ship.lookAt(ship.position + g.axis);
    ship.velocity = g.axis * length(ship.velocity);
}

void RingRace::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& planet) const {
    Vec3 center = planet.position(u.time);
    if (!sphereInFrustum(u, center, planet.radius + 10.0f)) return;
    if (projectedRadius(u, center, planet.radius, fb.height()) < 40.0f) return;
    u.objectCenter = center;
    for (int i = 0; i < (int)gates.size(); ++i) {
        if (running && i < next) continue; // ya cruzado
        Vec3 world = center + gates[i].position;
        if (!sphereInFrustum(u, world, GATE_RADIUS + 0.6f)) continue;
        u.model = basisFromY(gates[i].axis, world);
        drawMesh(fb, i == next ? nextRing : laterRing, u, propShader, stats);
        stats.propsDrawn++;
    }
}

void RingRace::drawHud(Framebuffer& fb, bool landed) const {
    centeredPanel(fb, 8, 330, 52);
    centeredText(fb, 13, "COBALTO - CARRERA DE ANILLOS", HUD_GREY);
    char line[96];
    if (running) {
        std::snprintf(line, sizeof(line), "ANILLO %d/%d   %.1f S", next, (int)gates.size(), timer);
        centeredText(fb, 24, line, HUD_GOLD, 2);
    } else {
        centeredText(fb, 24, "CRUZA EL ANILLO DORADO", HUD_GOLD, 2);
    }
    if (best > 0.0f) std::snprintf(line, sizeof(line), "MEJOR %.1f S   SIGUIENTE A %.0f", best, distanceToNext);
    else std::snprintf(line, sizeof(line), "SIGUIENTE ANILLO A %.0f   (^ ~ SUBIR / BAJAR)", distanceToNext);
    centeredText(fb, 46, landed ? line : "BAJA A LA SUPERFICIE PARA CORRER", landed ? HUD_WHITE : HUD_GREY);

    if (finishFlash > 0.0f) {
        std::snprintf(line, sizeof(line), "TIEMPO %.1f S", last);
        centeredText(fb, fb.height() / 2 - 60, line, HUD_GREEN, 4);
        if (last <= best) centeredText(fb, fb.height() / 2 - 22, "NUEVO RECORD", HUD_GOLD, 2);
    }
}
