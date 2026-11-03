#pragma once
#include <vector>
#include "framebuffer.h"
#include "mesh.h"
#include "renderer.h"
#include "scene.h"
#include "ship.h"

// Minijuego de Cobalto, en el universo alterno. El planeta no gira sobre su
// eje, asi que los anillos se guardan relativos a su centro y solo se les
// suma su posicion en la orbita.

// Carrera contra reloj: una fila de anillos flota alrededor de Cobalto. Hay
// que cruzarlos en orden; el reloj empieza en el primero y para en el ultimo.
// El siguiente anillo es dorado, los demas azules. Como estan a distintas
// alturas, hay que usar las flechas para subir y bajar.
class RingRace {
public:
    void build(float planetRadius);
    void update(float dt, const Ship& ship, const CelestialBody& planet, float time);
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& planet) const;
    void drawHud(Framebuffer& fb, bool landed) const;

    // Solo para pruebas: pone el carro 4 unidades antes del siguiente anillo,
    // mirando a traves de el.
    void placeShipForTest(Ship& ship, const Vec3& planetCenter) const;
    int nextGate() const { return next; }
    float bestTime() const { return best; }

private:
    struct Gate {
        Vec3 position; // relativa al centro del planeta
        Vec3 axis;     // direccion en que hay que cruzarlo
    };
    std::vector<Gate> gates;
    int next = 0;
    bool running = false;
    float timer = 0.0f, best = 0.0f, last = 0.0f;
    float finishFlash = 0.0f; // segundos que se muestra el tiempo al terminar
    float distanceToNext = 0.0f;
    Mesh nextRing, laterRing;
};
