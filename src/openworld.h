#pragma once
#include <cmath>
#include <vector>
#include "framebuffer.h"
#include "mesh.h"
#include "noise.h"
#include "particles.h"
#include "planets.h"
#include "renderer.h"
#include "ship.h"

// Mundo abierto de Arena: al aterrizar en el planeta se cambia a esta escena
// plana (a escala del carro, que mide ~1.4). Hay colinas, arboles, rocas y
// montanas en el borde, y en el centro un estadio como el de Rocket League:
// cancha con paredes, dos porterias, pads de boost y un rival naranja que
// maneja la computadora. Aqui el carro maneja por el suelo: acelera, gira,
// salta, hace flips y puede volar con el boost.

// Modelos del carro para dibujar los dos equipos ([0] completo, [1] reducido).
struct CarAssets {
    const Mesh* blue[2];
    const Mesh* orange[2];
    const Mesh* flame;
};

// Carro sobre el suelo. Su rumbo es un angulo (yaw) alrededor de la vertical;
// en el aire ademas puede levantar la nariz (para volar con el boost) y hacer
// un flip al saltar por segunda vez.
struct GroundCar {
    Vec3 position;          // punto del suelo bajo el carro (las ruedas)
    Vec3 velocity;
    float yaw = 0.0f;       // 0 = mira hacia +Z
    float nose = 0.0f;      // inclinacion de la nariz en el aire (radianes)
    Vec3 up{0, 1, 0};       // vertical del terreno bajo el carro (suavizada)
    bool onGround = true;
    float boost = 33.0f;    // 0..100, como en Rocket League
    bool boosting = false;
    bool jumpHeld = false;  // Espacio en el paso anterior (para detectar el toque)
    bool canFlip = false;   // queda el segundo salto
    float airTime = 0.0f;
    float flipTime = -1.0f; // tiempo desde que empezo el flip (-1 = no hay)
    Vec3 flipDir;           // direccion del flip (horizontal)
    float maxDrive = 17.0f, maxBoost = 28.0f;

    Vec3 flatForward() const { return Vec3{std::sin(yaw), 0.0f, std::cos(yaw)}; }
    // Ejes del modelo (con el terreno, la nariz y el giro del flip).
    void axes(Vec3& forward, Vec3& upAxis, Vec3& right) const;
    Mat4 modelMatrix() const;
};

class OpenWorld {
public:
    // Terreno, estadio, arboles y rocas (con semilla fija).
    void build(unsigned seed);

    // Al aterrizar: el carro aparece afuera del estadio, frente a una entrada.
    void enter();
    // Pruebas: el carro en la posicion de saque, dentro del estadio.
    void placeAtKickoff();

    // Fisica (pasos fijos de 1/120 s), el rival, el partido y la camara.
    // Espacio (input.brake) es saltar.
    void update(const ShipInput& input, float dt, Particles& particles);
    void snapCamera();

    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CarAssets& car, float time) const;
    void drawHud(Framebuffer& fb, float fps, float frameMs, const RenderStats& stats, bool showHelp) const;

    void restartMatch(); // Enter: partido nuevo
    void respawn();      // R: volver a la entrada del estadio
    float groundHeight(float x, float z) const;

    bool ballCamera = false; // tecla C: la camara mira al balon
    Vec3 eye, target;
    Vec3 sunDirection;

    // Para el titulo y las pruebas.
    const GroundCar& player() const { return car; }
    const GroundCar& opponent() const { return rival; }
    Vec3 ballPosition() const { return ballPos; }
    int blueScore() const { return blue; }
    int orangeScore() const { return orange; }
    int ballTouches() const { return touches; }
    bool playerInStadium() const;

private:
    // Pared vertical vista desde arriba (segmento en XZ). Las "solo balon"
    // tapan las entradas laterales y, arriba del travesano, la boca de la
    // porteria: el carro pasa por ahi, el balon no.
    struct Wall {
        Vec2 a, b;
        bool ballOnly = false;
        float minY = -1e9f; // solo cuenta si el objeto llega a esta altura
    };
    struct Pad {
        Vec3 position;
        float cooldown = 0.0f; // segundos para que vuelva a estar lleno
    };
    enum class Phase { Waiting, Play, Goal, Countdown, Finished };

    Noise noise{0}; // relieve del terreno
    std::vector<Mesh> terrain;      // trozos del terreno (se descartan por separado)
    std::vector<Vec3> chunkCenters;
    std::vector<float> chunkRadii;
    Mesh stadium, ballMesh, shadowDisc, padActive, padEmpty;
    PlanetTextures ballTextures; // superficie del balon de Rocket League (rlball.h)
    // Arboles y rocas, en trozos de 6 x 6 para descartarlos por separado y
    // no dibujar los muy lejanos (la niebla ya casi los tapa).
    std::vector<Mesh> scenery;
    std::vector<Vec3> sceneryCenters;
    std::vector<Wall> walls;
    std::vector<Pad> pads;

    GroundCar car, rival;
    Vec3 ballPos, ballVel;
    Vec3 ballX{1, 0, 0}, ballY{0, 1, 0};
    float accumulator = 0.0f;

    Phase phase = Phase::Waiting;
    float phaseTimer = 0.0f;
    float clock = 180.0f; // segundos de partido
    int blue = 0, orange = 0;
    bool lastGoalBlue = true;
    bool overtime = false;
    int touches = 0;

    Vec3 cameraOffset; // del carro al ojo (suavizado)
    Vec3 exhaustLast[2][2];            // escapes de cada carro en el cuadro anterior
    bool wasBoosting[2] = {false, false};

    void step(const ShipInput& input, float h, Particles& particles);
    void driveCar(GroundCar& c, const ShipInput& in, float h);
    ShipInput rivalInput() const;
    void stepBall(float h);
    void hitBall(const GroundCar& c);
    void collideWalls(Vec3& p, float radius, Vec3& v, bool isBall, float bounce) const;
    void addBlock(float x0, float x1, float z0, float z1); // muro rectangular (gradas)
    void collideCars();
    void takePads(GroundCar& c);
    void kickoff();
    void scoreGoal(bool blueScored, Particles& particles);
    void updateCamera(float dt);
};
