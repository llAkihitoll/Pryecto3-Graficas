#pragma once
#include <string>
#include <vector>
#include "framebuffer.h"
#include "mesh.h"
#include "particles.h"
#include "renderer.h"
#include "ship.h"

// Minijuegos de arcade de los planetas Pac-Man y Galaga (universo alterno).
// Al aterrizar en uno se cambia a su juego, en 3D y hecho de cubos: los
// personajes son sprites de pixel art convertidos en cubos (voxels.h). El
// fondo es el cielo morado del universo alterno (lo dibuja main.cpp).
// Teclas: las del carro (W A S D o flechas, Espacio); Enter reinicia.
// Sprite de la nave de Galaga (el planeta Galaga tiene esa forma).
const std::vector<std::string>& galagaFighterSprite();

class ArcadeGame {
public:
    virtual ~ArcadeGame() = default;
    virtual void build() = 0;   // modelos (una vez, al arrancar)
    virtual void enter() = 0;   // al aterrizar: partida nueva
    virtual void restart() = 0; // Enter
    virtual void update(const ShipInput& input, float dt, Particles& particles) = 0;
    virtual void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float time) const = 0;
    virtual void drawHud(Framebuffer& fb, float fps, bool showHelp) const = 0;
    virtual const char* name() const = 0;
    virtual int score() const = 0;

    Vec3 eye, target; // camara fija mirando el tablero desde arriba
};

// Pac-Man: laberinto de 19 x 21 celdas con puntos y pildoras de poder y los
// cuatro fantasmas con su forma clasica de perseguir:
//   Blinky (rojo) va a donde esta Pac-Man; Pinky (rosa) a 4 celdas delante;
//   Inky (cian) al punto opuesto a Blinky respecto a 2 celdas delante;
//   Clyde (naranja) lo persigue de lejos y huye a su esquina de cerca.
// Alternan dispersarse (cada uno a su esquina) y perseguir. Con una pildora
// de poder se asustan y se los puede comer; vuelven como ojos a su casa.
class PacmanGame : public ArcadeGame {
public:
    void build() override;
    void enter() override { restart(); }
    void restart() override;
    void update(const ShipInput& input, float dt, Particles& particles) override;
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float time) const override;
    void drawHud(Framebuffer& fb, float fps, bool showHelp) const override;
    const char* name() const override { return "Pac-Man"; }
    int score() const override { return points; }

private:
    // Algo que se mueve por la rejilla: posicion en celdas (los centros son
    // enteros) y direccion (-1, 0 o 1 en cada eje).
    struct Mover {
        float x = 0, y = 0;
        int dx = 0, dy = 0;
    };
    enum class GhostState { House, Leaving, Active, Eaten };
    struct Ghost {
        Mover m;
        GhostState state = GhostState::House;
        float release = 0.0f; // segundos que espera en la casa
        bool frightened = false;
        int corner[2];        // esquina a la que va al dispersarse
    };
    enum class Phase { Ready, Playing, Dying, LevelClear, GameOver };

    std::vector<std::string> maze; // estado actual (los puntos comidos se borran)
    Mover pac;
    int wantDx = 0, wantDy = 0;
    Ghost ghosts[4];
    Phase phase = Phase::Ready;
    float phaseTimer = 0.0f, modeTimer = 0.0f, frightTimer = 0.0f, mouth = 0.0f;
    int modeIndex = 0;
    bool chase = false;
    int points = 0, best = 0, lives = 3, level = 1, combo = 0, dotsLeft = 0;
    float time = 0.0f;

    Mesh walls, floorMesh, dots, power, frightMesh, flashMesh, eyesMesh;
    Mesh pacFrames[3];
    Mesh ghostMeshes[4];

    void resetLevel(bool newMaze);
    void rebuildDots();
    bool walkable(int cx, int cy, bool ghost, bool door) const;
    void advance(Mover& m, float distance, bool isPac, int ghostIndex);
    void chooseGhostDir(int index);
    void stepPacman(float dt);
    void stepGhosts(float dt, Particles& particles);
    void onCenter(Mover& m, bool isPac, int ghostIndex);
    Vec3 world(float x, float y, float height) const;
};

// Galaga: la nave se mueve abajo y dispara hacia arriba (maximo 2 disparos a
// la vez). Los enemigos entran volando a su lugar en la formacion, que se
// mece de lado a lado; de vez en cuando uno se lanza en picada disparando y,
// si no lo derriban, vuelve a entrar por arriba. Abejas (50), mariposas (80)
// y jefes (150, aguantan dos disparos); en picada valen el doble.
class GalagaGame : public ArcadeGame {
public:
    void build() override;
    void enter() override { restart(); }
    void restart() override;
    void update(const ShipInput& input, float dt, Particles& particles) override;
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float time) const override;
    void drawHud(Framebuffer& fb, float fps, bool showHelp) const override;
    const char* name() const override { return "Galaga"; }
    int score() const override { return points; }

private:
    enum class EnemyState { Entering, Formation, Diving, Returning, Dead };
    struct Enemy {
        int type = 0; // 0 abeja, 1 mariposa, 2 jefe
        Vec3 slot;    // lugar en la formacion (sin el vaiven)
        Vec3 pos, from;
        EnemyState state = EnemyState::Entering;
        float t = 0.0f, delay = 0.0f, side = 1.0f, shotTimer = 0.0f;
        int hits = 0;
    };
    struct Shot {
        Vec3 pos, vel;
        bool enemy;
    };
    enum class Phase { Ready, Playing, WaveClear, GameOver };

    std::vector<Enemy> enemies;
    std::vector<Shot> shots;
    std::vector<Vec3> stars;
    float playerX = 0.0f, fireCooldown = 0.0f, respawn = 0.0f, invulnerable = 0.0f, diveTimer = 0.0f;
    float sway = 0.0f, phaseTimer = 0.0f, time = 0.0f;
    bool alive = true;
    Phase phase = Phase::Ready;
    int points = 0, best = 0, lives = 3, wave = 1;

    Mesh fighter, enemyMeshes[3], bossHurt, playerShot, enemyShot, frame;

    void spawnWave();
    Vec3 slotNow(const Enemy& e) const;
    void killPlayer(Particles& particles);
};

// Centipede: el ciempies baja serpenteando por un campo de hongos. Avanza de
// lado y, al chocar con un hongo o con la orilla, baja una fila y da la
// vuelta; al llegar abajo sube y baja dentro de la zona del jugador. Un
// disparo a un segmento lo vuelve hongo y el de atras pasa a ser cabeza (el
// ciempies se parte). Los hongos aguantan 4 disparos. Una arana zigzaguea en
// la zona del jugador y se come los hongos. Cabeza 100, cuerpo 10, arana 600.
class CentipedeGame : public ArcadeGame {
public:
    void build() override;
    void enter() override { restart(); }
    void restart() override;
    void update(const ShipInput& input, float dt, Particles& particles) override;
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float time) const override;
    void drawHud(Framebuffer& fb, float fps, bool showHelp) const override;
    const char* name() const override { return "Centipede"; }
    int score() const override { return points; }

private:
    static const int COLS = 21, ROWS = 24, PLAYER_ROWS = 5;
    struct Segment {
        int gx = 0, gy = 0, tx = 0, ty = 0; // celda actual y la siguiente
        float t = 0.0f;                     // avance entre las dos (0..1)
        int dir = 1, vdir = 1;              // de lado y hacia abajo / arriba
        bool head = false, alive = true;
    };
    enum class Phase { Ready, Playing, Dying, WaveClear, GameOver };

    std::vector<Segment> segments;
    int mushrooms[ROWS][COLS] = {}; // vida de cada hongo (0 = no hay)
    float px = 10.0f, py = 22.0f;   // jugador, en celdas
    bool shotAlive = false;
    float shotX = 0.0f, shotY = 0.0f;
    bool spiderAlive = false;
    float spiderX = 0.0f, spiderY = 0.0f, spiderVx = 0.0f, spiderVy = 0.0f, spiderTimer = 4.0f;
    Phase phase = Phase::Ready;
    float phaseTimer = 0.0f, time = 0.0f;
    int points = 0, best = 0, lives = 3, wave = 1;

    Mesh body, headMesh, mushroomMeshes[4], player, spider, shot, floorMesh, border;

    void spawnCentipede();
    void scatterMushrooms(int count);
    void stepSegment(Segment& s);
    Vec3 segmentPos(const Segment& s) const;
    Vec3 world(float x, float y, float height) const;
};

// Snake: la serpiente avanza una celda a la vez por un tablero de 24 x 18.
// Cada fruta la hace crecer y la acelera un poco; chocar con la pared o con
// su propio cuerpo termina la partida. Las teclas se guardan en orden (hasta
// dos) para poder hacer giros rapidos, y no se puede dar media vuelta.
class SnakeGame : public ArcadeGame {
public:
    void build() override;
    void enter() override { restart(); }
    void restart() override;
    void update(const ShipInput& input, float dt, Particles& particles) override;
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float time) const override;
    void drawHud(Framebuffer& fb, float fps, bool showHelp) const override;
    const char* name() const override { return "Snake"; }
    int score() const override { return points; }

private:
    static const int COLS = 24, ROWS = 18;
    struct Cell {
        int x, y;
    };
    enum class Phase { Ready, Playing, GameOver };

    std::vector<Cell> body, previous; // body[0] = cabeza; previous = donde estaba cada parte antes del ultimo paso
    int dx = 1, dy = 0;
    std::vector<Cell> pending; // giros pedidos que todavia no se aplican
    int lastWantX = 0, lastWantY = 0;
    int grow = 0;
    Cell fruit{0, 0};
    float stepTimer = 0.0f, interval = 0.15f, time = 0.0f, phaseTimer = 0.0f;
    Phase phase = Phase::Ready;
    int points = 0, best = 0;

    Mesh segment, headMesh, fruitMesh, floorMesh, border;

    void placeFruit();
    Vec3 world(float x, float y, float height) const;
};
