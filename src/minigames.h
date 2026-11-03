#pragma once
#include <vector>
#include "framebuffer.h"
#include "mesh.h"
#include "particles.h"
#include "renderer.h"
#include "scene.h"
#include "ship.h"

// Minijuegos de los planetas del universo alterno. Los dos planetas no giran
// sobre su eje, asi que todo (balon, porterias, anillos) se guarda en
// coordenadas relativas al centro del planeta y solo se le suma su posicion
// en la orbita.

// Que tan lejos del polo llega la porteria (radianes, medido desde el centro
// del planeta). La textura de Arena pinta las lineas con el mismo valor.
const float ARENA_GOAL_ANGLE = 0.30f;

// Futbol como en Rocket League, pero sobre un planeta: la cancha da la vuelta
// completa, la porteria azul esta en el polo norte y la naranja en el sur. El
// carro (equipo azul) tiene que meter el balon en la naranja. El balon tiene
// su propia fisica: cae hacia el centro del planeta, rebota, rueda con
// friccion y sale disparado cuando el carro lo golpea.
class SoccerGame {
public:
    void build(float planetRadius);

    // Avanza el balon. El carro solo cuenta si esta cerca de la superficie.
    void update(float dt, const Ship& ship, const CelestialBody& planet, float time, Particles& particles);

    // Balon y porterias (u.time debe ser el tiempo de la escena).
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& planet) const;

    // Marcador e instrucciones. "landed" = el carro esta en la superficie.
    void drawHud(Framebuffer& fb, bool landed) const;

    int blueScore = 0, orangeScore = 0;

    // Solo para pruebas: pone el balon quieto en ese punto (relativo al planeta).
    void placeBallForTest(const Vec3& rel) { ballPos = rel; ballVel = Vec3{0, 0, 0}; }
    int ballTouches() const { return touches; }
    Vec3 ballPosition() const { return ballPos; }

private:
    float planetRadius = 1.0f;
    Vec3 ballPos, ballVel;            // relativos al centro del planeta
    Vec3 ballX{1, 0, 0}, ballY{0, 1, 0}; // orientacion del balon (gira al rodar)
    Vec3 lastCar;                     // posicion relativa del carro en el cuadro anterior
    bool hasLastCar = false;
    float accumulator = 0.0f;
    float goalTimer = 0.0f;           // segundos que quedan del festejo
    bool lastGoalOwn = false;         // el ultimo gol fue en la porteria propia
    int touches = 0;
    Mesh ballMesh, goalsMesh;

    void resetBall(const Vec3& carRel);
    void step(float h, const Vec3& carPos, const Vec3& carVel, bool carIn);
};

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
