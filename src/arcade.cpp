#include "arcade.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include "audio.h"
#include "font.h"
#include "hud.h"
#include "mesh_builder.h"
#include "voxels.h"

static std::mt19937 arcadeRng(2026);
static float randomUnit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(arcadeRng); }

const uint32_t HUD_WHITE = 0xFFE8ECF4, HUD_GREY = 0xFFB0B8C8, HUD_GOLD = 0xFFFFD860, HUD_PINK = 0xFFFF80D0,
               HUD_CYAN = 0xFF60D0FF;

static void centered(Framebuffer& fb, int y, const char* text, uint32_t color, int scale = 1) {
    drawText(fb, (fb.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

// Caja con un solo material (muros, puntos, disparos).
static void box(MeshBuilder& b, Vec3 lo, Vec3 hi, int mat) {
    Vec3 p[8];
    for (int i = 0; i < 8; ++i) p[i] = Vec3{i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
    Vec3 c = (lo + hi) * 0.5f;
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& f : faces) {
        b.triangle(p[f[0]], p[f[1]], p[f[2]], mat, c);
        b.triangle(p[f[0]], p[f[2]], p[f[3]], mat, c);
    }
}

static int glowMaterial(MeshBuilder& b, const Vec3& color, float voxelSize) {
    int m = b.material("Neon", color, 0.0f, 1.0f, 1.0f);
    b.mesh.materials[m].voxelSize = voxelSize;
    return m;
}

// ============================================================== Pac-Man

const int MAZE_W = 19, MAZE_H = 21;
const int HOUSE_X = 9, HOUSE_Y = 9, DOOR_EXIT_Y = 7; // casa de los fantasmas y la celda de salida
// # muro, . punto, o pildora de poder, - puerta de la casa, G casa,
// T tunel (se sale por un lado y se entra por el otro), P inicio de Pac-Man,
// espacio = afuera del laberinto.
static const char* MAZE[MAZE_H] = {
    "###################",
    "#........#........#",
    "#o##.###.#.###.##o#",
    "#.................#",
    "#.##.#.#####.#.##.#",
    "#....#...#...#....#",
    "####.###.#.###.####",
    "   #.#.......#.#   ",
    "####.#.##-##.#.####",
    "TTTT...#GGG#...TTTT",
    "####.#.#####.#.####",
    "   #.#.......#.#   ",
    "####.#.#####.#.####",
    "#........#........#",
    "#.##.###.#.###.##.#",
    "#o.#.....P.....#.o#",
    "##.#.#.#####.#.#.##",
    "#....#...#...#....#",
    "#.######.#.######.#",
    "#.................#",
    "###################",
};

// Pac-Man de 13 x 13: un circulo con la boca abierta hacia +X ("half" = la
// mitad del angulo de la boca).
static std::vector<std::string> pacSprite(float half) {
    std::vector<std::string> rows;
    for (int y = 0; y < 13; ++y) {
        std::string row;
        for (int x = 0; x < 13; ++x) {
            float dx = x - 6.0f, dy = y - 6.0f;
            bool inside = dx * dx + dy * dy <= 6.3f * 6.3f;
            bool mouth = dx > 0.0f && std::fabs(std::atan2(dy, dx)) < half;
            row += inside && !mouth ? 'Y' : '.';
        }
        rows.push_back(row);
    }
    return rows;
}

static const std::vector<std::string> GHOST = {
    "....######....", "..##########..", ".############.", ".##WWW##WWW##.", "###WEE##WEE###", "###WWW##WWW###",
    "##############", "##############", "##############", "##############", "##.###..###.##", "#...##..##...#",
};
static const std::vector<std::string> GHOST_SCARED = {
    "....######....", "..##########..", ".############.", ".############.", "####WW##WW####", "####WW##WW####",
    "##############", "##############", "##W##W##W##W##", "#W##W##W##W##W", "##.###..###.##", "#...##..##...#",
};

Vec3 PacmanGame::world(float x, float y, float height) const { return Vec3{x - 8.5f, height, y - 9.5f}; }

void PacmanGame::build() {
    // Muros de neon violeta (cada celda es un cubo de 1 x 0.9 x 1) y la
    // puerta de la casa de los fantasmas.
    MeshBuilder w;
    int wall = glowMaterial(w, Vec3{0.38f, 0.26f, 1.0f}, 1.0f);
    int door = glowMaterial(w, Vec3{1.0f, 0.55f, 0.85f}, 0.0f);
    for (int y = 0; y < MAZE_H; ++y)
        for (int x = 0; x < MAZE_W; ++x) {
            Vec3 lo{x - 9.0f, 0.0f, y - 10.0f};
            if (MAZE[y][x] == '#') box(w, lo, lo + Vec3{1.0f, 0.9f, 1.0f}, wall);
            if (MAZE[y][x] == '-') box(w, lo + Vec3{0.0f, 0.3f, 0.4f}, lo + Vec3{1.0f, 0.5f, 0.6f}, door);
        }
    walls = w.mesh;

    MeshBuilder f;
    int floor = glowMaterial(f, Vec3{0.07f, 0.03f, 0.13f}, 1.0f);
    box(f, Vec3{-9.0f, -0.2f, -10.0f}, Vec3{10.0f, 0.0f, 11.0f}, floor);
    floorMesh = f.mesh;

    MeshBuilder p;
    box(p, Vec3{-0.22f, 0.0f, -0.22f}, Vec3{0.22f, 0.44f, 0.22f}, glowMaterial(p, Vec3{1.0f, 0.75f, 0.9f}, 0.0f));
    power = p.mesh;

    // Personajes: sprites de pixel art en cubos.
    const float px = 0.075f;
    const float halves[3] = {0.02f, 0.4f, 0.8f};
    for (int i = 0; i < 3; ++i) pacFrames[i] = makeVoxelSprite(pacSprite(halves[i]), {{'Y', {1.0f, 0.88f, 0.1f}}}, px, 4);
    const Vec3 colors[4] = {{1.0f, 0.12f, 0.12f}, {1.0f, 0.55f, 0.85f}, {0.2f, 0.95f, 1.0f}, {1.0f, 0.6f, 0.15f}};
    for (int i = 0; i < 4; ++i)
        ghostMeshes[i] = makeVoxelSprite(GHOST, {{'#', colors[i]}, {'W', {1, 1, 1}}, {'E', {0.15f, 0.25f, 1.0f}}}, px, 4);
    frightMesh = makeVoxelSprite(GHOST_SCARED, {{'#', {0.15f, 0.2f, 0.95f}}, {'W', {1.0f, 0.85f, 0.8f}}}, px, 4);
    flashMesh = makeVoxelSprite(GHOST_SCARED, {{'#', {0.95f, 0.95f, 1.0f}}, {'W', {1.0f, 0.2f, 0.2f}}}, px, 4);
    eyesMesh = makeVoxelSprite(GHOST, {{'W', {1, 1, 1}}, {'E', {0.15f, 0.25f, 1.0f}}}, px, 4);

    // Camara fija sobre el laberinto, un poco inclinada.
    eye = Vec3{0.5f, 23.0f, 14.5f};
    target = Vec3{0.5f, 0.0f, 1.8f};
    restart();
}

void PacmanGame::restart() {
    points = 0;
    lives = 3;
    level = 1;
    resetLevel(true);
}

void PacmanGame::resetLevel(bool newMaze) {
    if (newMaze) {
        maze.assign(MAZE, MAZE + MAZE_H);
        dotsLeft = 0;
        for (const std::string& row : maze)
            for (char c : row) dotsLeft += c == '.' || c == 'o';
        rebuildDots();
    }
    pac = Mover{9.0f, 15.0f, -1, 0};
    wantDx = -1;
    wantDy = 0;
    // Blinky empieza afuera; los demas esperan en la casa y salen por turnos.
    const int corners[4][2] = {{18, -3}, {0, -3}, {18, 23}, {0, 23}};
    const float starts[4][2] = {{9, 7}, {9, 9}, {8, 9}, {10, 9}};
    const float release[4] = {0.0f, 1.0f, 4.0f, 7.0f};
    for (int i = 0; i < 4; ++i) {
        Ghost& g = ghosts[i];
        g.m = Mover{starts[i][0], starts[i][1], i == 0 ? -1 : 0, 0};
        g.state = i == 0 ? GhostState::Active : GhostState::House;
        g.release = release[i];
        g.frightened = false;
        g.corner[0] = corners[i][0];
        g.corner[1] = corners[i][1];
    }
    frightTimer = 0.0f;
    modeTimer = 0.0f;
    modeIndex = 0;
    chase = false;
    phase = Phase::Ready;
    phaseTimer = 2.0f;
}

void PacmanGame::rebuildDots() {
    MeshBuilder b;
    int mat = glowMaterial(b, Vec3{1.0f, 0.85f, 0.95f}, 0.0f);
    for (int y = 0; y < MAZE_H; ++y)
        for (int x = 0; x < MAZE_W; ++x)
            if (maze[y][x] == '.') {
                Vec3 c = world((float)x, (float)y, 0.0f);
                box(b, c + Vec3{-0.1f, 0.25f, -0.1f}, c + Vec3{0.1f, 0.45f, 0.1f}, mat);
            }
    dots = b.mesh;
}

bool PacmanGame::walkable(int cx, int cy, bool ghost, bool door) const {
    if (cy == HOUSE_Y && (cx < 0 || cx >= MAZE_W)) return true; // por el tunel
    if (cx < 0 || cy < 0 || cx >= MAZE_W || cy >= MAZE_H) return false;
    char c = maze[cy][cx];
    if (c == '#' || c == ' ') return false;
    if (c == '-' || c == 'G') return ghost && door;
    return true;
}

// Al llegar al centro de una celda: Pac-Man come lo que haya y gira si puede
// hacia donde se le pide; un fantasma elige su siguiente direccion.
void PacmanGame::onCenter(Mover& m, bool isPac, int ghostIndex) {
    int cx = (int)std::lround(m.x), cy = (int)std::lround(m.y);
    if (!isPac) {
        chooseGhostDir(ghostIndex);
        return;
    }
    if (cx >= 0 && cx < MAZE_W) {
        char& c = maze[cy][cx];
        if (c == '.' || c == 'o') {
            points += c == '.' ? 10 : 50;
            dotsLeft--;
            if (c == 'o') { // pildora de poder: los fantasmas se asustan y dan la vuelta
                frightTimer = std::max(2.0f, 6.5f - 0.5f * (level - 1));
                combo = 0;
                for (Ghost& g : ghosts)
                    if (g.state == GhostState::Active) {
                        g.frightened = true;
                        g.m.dx = -g.m.dx;
                        g.m.dy = -g.m.dy;
                    }
            }
            bool dot = c == '.';
            if (dot) {
                // "Waka": los puntos alternan entre dos tonos.
                static bool high = false;
                high = !high;
                audio::play(audio::Sfx::Dot, 1.0f, high ? 1.15f : 0.9f);
            } else {
                audio::play(audio::Sfx::PowerUp);
            }
            c = '_';
            if (dot) rebuildDots();
        }
    }
    if ((wantDx || wantDy) && walkable(cx + wantDx, cy + wantDy, false, false)) {
        m.dx = wantDx;
        m.dy = wantDy;
    } else if (!walkable(cx + m.dx, cy + m.dy, false, false)) {
        m.dx = m.dy = 0;
    }
}

void PacmanGame::chooseGhostDir(int index) {
    Ghost& g = ghosts[index];
    int cx = (int)std::lround(g.m.x), cy = (int)std::lround(g.m.y);
    if (g.state == GhostState::Eaten && cx == HOUSE_X && cy == DOOR_EXIT_Y) { // llego a la puerta: entra a la casa
        g.m = Mover{(float)HOUSE_X, (float)HOUSE_Y, 0, 0};
        g.state = GhostState::Leaving;
        return;
    }
    // Celda objetivo segun el fantasma y el modo.
    int tx, ty;
    if (g.state == GhostState::Eaten) {
        tx = HOUSE_X;
        ty = DOOR_EXIT_Y;
    } else if (!chase) {
        tx = g.corner[0];
        ty = g.corner[1];
    } else {
        int px = (int)std::lround(pac.x), py = (int)std::lround(pac.y);
        switch (index) {
            case 0: tx = px; ty = py; break;                                   // Blinky: directo
            case 1: tx = px + 4 * pac.dx; ty = py + 4 * pac.dy; break;         // Pinky: adelante
            case 2: {                                                          // Inky: con Blinky
                int ax = px + 2 * pac.dx, ay = py + 2 * pac.dy;
                tx = 2 * ax - (int)std::lround(ghosts[0].m.x);
                ty = 2 * ay - (int)std::lround(ghosts[0].m.y);
                break;
            }
            default: {                                                         // Clyde: de lejos si, de cerca no
                float d2 = (float)((px - cx) * (px - cx) + (py - cy) * (py - cy));
                tx = d2 > 64.0f ? px : g.corner[0];
                ty = d2 > 64.0f ? py : g.corner[1];
            }
        }
    }
    // Entre las direcciones posibles (sin dar media vuelta) la que deja mas
    // cerca del objetivo; asustado, una al azar.
    const int dirs[4][2] = {{0, -1}, {-1, 0}, {0, 1}, {1, 0}};
    bool door = g.state == GhostState::Eaten;
    int options[4], count = 0, best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < 4; ++i) {
        if (dirs[i][0] == -g.m.dx && dirs[i][1] == -g.m.dy && (g.m.dx || g.m.dy)) continue;
        if (!walkable(cx + dirs[i][0], cy + dirs[i][1], true, door)) continue;
        options[count++] = i;
        float dx = (float)(cx + dirs[i][0] - tx), dy = (float)(cy + dirs[i][1] - ty);
        if (dx * dx + dy * dy < bestD) {
            bestD = dx * dx + dy * dy;
            best = i;
        }
    }
    if (count == 0) { // callejon sin salida: media vuelta
        g.m.dx = -g.m.dx;
        g.m.dy = -g.m.dy;
        return;
    }
    if (g.frightened) best = options[(int)(randomUnit() * count) % count];
    g.m.dx = dirs[best][0];
    g.m.dy = dirs[best][1];
}

// Avanza "distance" celdas en la direccion actual; en cada centro de celda
// que cruza decide la siguiente direccion (onCenter).
void PacmanGame::advance(Mover& m, float distance, bool isPac, int ghostIndex) {
    for (int guard = 0; guard < 8 && distance > 1e-5f; ++guard) {
        if (m.dx == 0 && m.dy == 0) {
            onCenter(m, isPac, ghostIndex);
            if (m.dx == 0 && m.dy == 0) return;
        }
        float& coord = m.dx != 0 ? m.x : m.y;
        int d = m.dx != 0 ? m.dx : m.dy;
        float next = d > 0 ? std::floor(coord + 1e-4f) + 1.0f : std::ceil(coord - 1e-4f) - 1.0f;
        float gap = std::fabs(next - coord);
        // Con tolerancia: si el paso deja al personaje casi en el centro (por
        // el redondeo de los float), cuenta como llegar, para no saltarse la
        // decision de girar en esa celda.
        if (distance + 1e-4f < gap) {
            coord += d * distance;
            return;
        }
        coord = next;
        distance -= gap;
        // Tunel: al salir por un lado se aparece por el otro.
        if (m.x <= -1.0f) m.x = (float)MAZE_W - 1.0f;
        else if (m.x >= (float)MAZE_W) m.x = 0.0f;
        onCenter(m, isPac, ghostIndex);
    }
}

void PacmanGame::stepPacman(float dt) {
    // Dar media vuelta se puede en cualquier momento.
    if ((wantDx || wantDy) && wantDx == -pac.dx && wantDy == -pac.dy) {
        pac.dx = wantDx;
        pac.dy = wantDy;
    }
    bool moving = pac.dx || pac.dy;
    advance(pac, (6.0f + 0.3f * (level - 1)) * dt, true, -1);
    if (moving) mouth += dt;
}

void PacmanGame::stepGhosts(float dt, Particles& particles) {
    float base = 5.0f + 0.3f * (level - 1);
    for (int i = 0; i < 4; ++i) {
        Ghost& g = ghosts[i];
        switch (g.state) {
            case GhostState::House:
                g.m.y = HOUSE_Y + 0.15f * std::sin(time * 6.0f + i);
                g.release -= dt;
                if (g.release <= 0.0f) g.state = GhostState::Leaving;
                break;
            case GhostState::Leaving: { // al centro de la casa y luego arriba, por la puerta
                float step = 3.0f * dt;
                if (std::fabs(g.m.x - HOUSE_X) > 0.01f) {
                    g.m.x += clampf(HOUSE_X - g.m.x, -step, step);
                } else {
                    g.m.x = (float)HOUSE_X;
                    g.m.y -= step;
                    if (g.m.y <= DOOR_EXIT_Y) {
                        g.m = Mover{(float)HOUSE_X, (float)DOOR_EXIT_Y, -1, 0};
                        g.state = GhostState::Active;
                        g.frightened = false;
                    }
                }
                break;
            }
            case GhostState::Active:
                advance(g.m, (g.frightened ? 3.0f : base) * dt, false, i);
                break;
            case GhostState::Eaten:
                advance(g.m, 11.0f * dt, false, i);
                break;
        }
        // Choque con Pac-Man.
        if (g.state != GhostState::Active) continue;
        float dx = g.m.x - pac.x, dy = g.m.y - pac.y;
        if (dx * dx + dy * dy > 0.7f * 0.7f) continue;
        if (g.frightened) {
            g.state = GhostState::Eaten;
            g.frightened = false;
            combo++;
            points += 200 << (combo - 1);
            audio::play(audio::Sfx::Pickup, 1.0f, 0.7f + 0.15f * combo); // comerse un fantasma
            particles.burst(world(g.m.x, g.m.y, 0.4f), Vec3{0, 0, 0}, 30, 1.0f, 4.0f, 1.5f, 0.2f, 0.5f);
        } else {
            phase = Phase::Dying;
            phaseTimer = 1.6f;
            audio::play(audio::Sfx::Die);
            particles.burst(world(pac.x, pac.y, 0.4f), Vec3{0, 0, 0}, 60, 1.0f, 5.0f, 2.0f, 0.3f, 0.8f);
            return;
        }
    }
}

void PacmanGame::update(const ShipInput& in, float dt, Particles& particles) {
    if (dt <= 0.0f) return;
    time += dt;
    // Direccion pedida: W o flecha arriba, S o abajo, A o izquierda, D o derecha.
    if (in.throttle > 0 || in.pitch > 0) { wantDx = 0; wantDy = -1; }
    else if (in.throttle < 0 || in.pitch < 0) { wantDx = 0; wantDy = 1; }
    else if (in.steer > 0) { wantDx = -1; wantDy = 0; }
    else if (in.steer < 0) { wantDx = 1; wantDy = 0; }

    switch (phase) {
        case Phase::Ready:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) phase = Phase::Playing;
            return;
        case Phase::Dying:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) {
                lives--;
                if (lives <= 0) phase = Phase::GameOver;
                else resetLevel(false);
            }
            return;
        case Phase::LevelClear:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) {
                level++;
                resetLevel(true);
            }
            return;
        case Phase::GameOver:
            return;
        case Phase::Playing:
            break;
    }

    // Dispersarse y perseguir por turnos (7 s, 20 s, 7 s, 20 s, 5 s y despues siempre perseguir).
    const float modes[6] = {7.0f, 20.0f, 7.0f, 20.0f, 5.0f, 1e9f};
    modeTimer += dt;
    if (modeTimer > modes[modeIndex] && modeIndex < 5) {
        modeTimer = 0.0f;
        modeIndex++;
        chase = modeIndex % 2 == 1;
    }
    if (frightTimer > 0.0f) {
        frightTimer -= dt;
        if (frightTimer <= 0.0f)
            for (Ghost& g : ghosts) g.frightened = false;
    }

    // Pasos chicos para que nadie se salte una celda ni un choque.
    const int sub = 4;
    for (int s = 0; s < sub && phase == Phase::Playing; ++s) {
        stepPacman(dt / sub);
        stepGhosts(dt / sub, particles);
    }
    best = std::max(best, points);
    if (phase == Phase::Playing && dotsLeft <= 0) {
        phase = Phase::LevelClear;
        phaseTimer = 2.0f;
    }
}

void PacmanGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float t) const {
    u.model = Mat4::identity();
    drawMesh(fb, floorMesh, u, voxelGlowShader, stats);
    drawMesh(fb, walls, u, voxelGlowShader, stats);
    drawMesh(fb, dots, u, voxelGlowShader, stats);
    for (int y = 0; y < MAZE_H; ++y)
        for (int x = 0; x < MAZE_W; ++x)
            if (maze[y][x] == 'o') {
                float pulse = 0.75f + 0.25f * std::sin(t * 8.0f);
                u.model = translate(world((float)x, (float)y, 0.1f)) * scale(pulse);
                drawMesh(fb, power, u, voxelGlowShader, stats);
            }

    // Pac-Man: mira hacia donde va y abre y cierra la boca.
    float angle = std::atan2(-(float)pac.dy, (float)pac.dx);
    if (!pac.dx && !pac.dy) angle = std::atan2(-(float)wantDy, (float)wantDx);
    const int cycle[4] = {0, 1, 2, 1};
    int frame = cycle[(int)(mouth * 14.0f) % 4];
    float shrink = phase == Phase::Dying ? std::max(0.05f, phaseTimer / 1.6f) : 1.0f;
    if (phase != Phase::GameOver) {
        u.model = translate(world(pac.x, pac.y, 0.05f)) * rotateY(angle) * scale(shrink);
        drawMesh(fb, pacFrames[frame], u, voxelGlowShader, stats);
    }

    if (phase == Phase::Dying || phase == Phase::GameOver) return;
    for (int i = 0; i < 4; ++i) {
        const Ghost& g = ghosts[i];
        const Mesh* mesh = &ghostMeshes[i];
        if (g.state == GhostState::Eaten) mesh = &eyesMesh;
        else if (g.frightened) mesh = frightTimer < 2.0f && std::fmod(t, 0.4f) < 0.2f ? &flashMesh : &frightMesh;
        u.model = translate(world(g.m.x, g.m.y, 0.05f));
        drawMesh(fb, *mesh, u, voxelGlowShader, stats);
    }
}

void PacmanGame::drawHud(Framebuffer& fb, float fps, bool showHelp) const {
    char line[96];
    darkenRect(fb, 8, 6, 220, 46, 0.55f);
    drawText(fb, 14, 11, "PAC-MAN", HUD_GOLD, 2);
    std::snprintf(line, sizeof(line), "PUNTOS %d   RECORD %d", points, best);
    drawText(fb, 14, 29, line, HUD_WHITE);
    std::snprintf(line, sizeof(line), "VIDAS %d   NIVEL %d", lives, level);
    drawText(fb, 14, 40, line, HUD_PINK);
    std::snprintf(line, sizeof(line), "FPS %.0f", fps);
    drawText(fb, fb.width() - textWidth(line) - 12, 10, line, 0xFF60E880);

    const int H = fb.height();
    if (phase == Phase::Ready) centered(fb, H / 2 - 30, "LISTO!", HUD_GOLD, 4);
    if (phase == Phase::LevelClear) centered(fb, H / 2 - 30, "NIVEL COMPLETO", HUD_CYAN, 4);
    if (phase == Phase::GameOver) {
        centered(fb, H / 2 - 40, "GAME OVER", 0xFFFF6060, 5);
        centered(fb, H / 2 + 4, "ENTER: JUGAR DE NUEVO", HUD_WHITE, 2);
    }
    if (showHelp) {
        static const char* help[] = {"W A S D / FLECHAS  MOVER", "ENTER  REINICIAR", "U  DESPEGAR AL ESPACIO",
                                     "H  OCULTAR AYUDA"};
        darkenRect(fb, 8, 58, 200, 52, 0.6f);
        for (int i = 0; i < 4; ++i) drawText(fb, 14, 63 + i * 11, help[i], HUD_WHITE);
    } else {
        drawText(fb, fb.width() - textWidth("H: AYUDA   U: DESPEGAR") - 12, H - 18, "H: AYUDA   U: DESPEGAR", HUD_GREY);
    }
}

// ============================================================== Galaga

static const std::vector<std::string> FIGHTER = {
    "......W......", "......W......", ".....WWW.....", ".....WWW.....", "..R..WWW..R..", "..R..WWW..R..",
    "..W.WWWWW.W..", "R.WWWBWBWWW.R", "R.WWWWWWWWW.R", "RWWW.WWW.WWWR", "RWW..WWW..WWR", "RW...R.R...WR",
    "R....R.R....R",
};
static const std::vector<std::string> BEE = {
    "...Y...Y...", "....Y.Y....", "..BBYYYBB..", ".BBRYYYRBB.", "BB.YYYYY.BB", "B..YRYRY..B", "...YYYYY...",
    "....Y.Y....",
};
static const std::vector<std::string> BUTTERFLY = {
    "..R.....R..", "...R...R...", ".RRWWWWWRR.", "RRBWWRWWBRR", "RB.WWWWW.BR", "R..WBWBW..R", "...WWWWW...",
    "..R.....R..",
};
static const std::vector<std::string> BOSS = {
    "....G.G....", "...GGGGG...", ".PPGYGYGPP.", "PPGGGGGGGPP", "PP.GGGGG.PP", "P..G.G.G..P", "..GG...GG..",
};

const std::vector<std::string>& galagaFighterSprite() { return FIGHTER; }

const float FIELD_X = 13.0f;
const float PLAYER_Z = 14.0f;

void GalagaGame::build() {
    const std::map<char, Vec3> shipColors = {{'W', {0.95f, 0.95f, 1.0f}}, {'R', {1.0f, 0.15f, 0.2f}}, {'B', {0.2f, 0.35f, 1.0f}}};
    fighter = makeVoxelSprite(FIGHTER, shipColors, 0.16f, 2);
    enemyMeshes[0] = makeVoxelSprite(BEE, {{'Y', {1.0f, 0.9f, 0.15f}}, {'B', {0.25f, 0.35f, 1.0f}}, {'R', {1.0f, 0.2f, 0.2f}}},
                                     0.14f, 2, true);
    enemyMeshes[1] = makeVoxelSprite(BUTTERFLY, {{'R', {1.0f, 0.15f, 0.25f}}, {'W', {0.95f, 0.95f, 1.0f}}, {'B', {0.25f, 0.35f, 1.0f}}},
                                     0.14f, 2, true);
    enemyMeshes[2] = makeVoxelSprite(BOSS, {{'G', {0.2f, 0.95f, 0.45f}}, {'P', {0.75f, 0.3f, 1.0f}}, {'Y', {1.0f, 0.9f, 0.2f}}},
                                     0.16f, 2, true);
    bossHurt = makeVoxelSprite(BOSS, {{'G', {0.35f, 0.55f, 1.0f}}, {'P', {1.0f, 0.4f, 0.8f}}, {'Y', {1.0f, 0.9f, 0.2f}}},
                               0.16f, 2, true);
    MeshBuilder a, b, f;
    box(a, Vec3{-0.07f, 0.1f, -0.35f}, Vec3{0.07f, 0.25f, 0.35f}, glowMaterial(a, Vec3{1.0f, 0.95f, 0.5f}, 0.0f));
    playerShot = a.mesh;
    box(b, Vec3{-0.09f, 0.1f, -0.22f}, Vec3{0.09f, 0.25f, 0.22f}, glowMaterial(b, Vec3{1.0f, 0.3f, 0.6f}, 0.0f));
    enemyShot = b.mesh;
    int edge = glowMaterial(f, Vec3{0.55f, 0.3f, 1.0f}, 0.0f);
    box(f, Vec3{-FIELD_X - 1.2f, -0.2f, -19.0f}, Vec3{-FIELD_X - 0.9f, 0.4f, 17.0f}, edge);
    box(f, Vec3{FIELD_X + 0.9f, -0.2f, -19.0f}, Vec3{FIELD_X + 1.2f, 0.4f, 17.0f}, edge);
    frame = f.mesh;

    // Estrellas que pasan hacia abajo (como el fondo de Galaga).
    stars.clear();
    for (int i = 0; i < 160; ++i)
        stars.push_back(Vec3{(randomUnit() * 2.0f - 1.0f) * 17.0f, -3.0f - randomUnit() * 4.0f, -22.0f + randomUnit() * 42.0f});

    eye = Vec3{0.0f, 25.0f, 17.0f};
    target = Vec3{0.0f, 0.0f, 0.5f};
    restart();
}

void GalagaGame::restart() {
    points = 0;
    lives = 3;
    wave = 1;
    playerX = 0.0f;
    alive = true;
    invulnerable = 0.0f;
    shots.clear();
    spawnWave();
    phase = Phase::Ready;
    phaseTimer = 1.5f;
}

void GalagaGame::spawnWave() {
    enemies.clear();
    struct Row { int type, count; float z, spacing; };
    const Row rows[4] = {{2, 4, -12.5f, 3.0f}, {1, 8, -10.0f, 2.4f}, {0, 10, -7.5f, 2.2f}, {0, 10, -5.3f, 2.2f}};
    for (int r = 0; r < 4; ++r)
        for (int i = 0; i < rows[r].count; ++i) {
            Enemy e;
            e.type = rows[r].type;
            e.slot = Vec3{(i - (rows[r].count - 1) * 0.5f) * rows[r].spacing, 0.0f, rows[r].z};
            float side = (r + i) % 2 ? 1.0f : -1.0f;
            e.from = Vec3{side * 18.0f, 0.0f, -22.0f};
            e.pos = e.from;
            e.delay = r * 0.45f + i * 0.07f;
            e.side = side;
            enemies.push_back(e);
        }
    diveTimer = 3.0f;
}

Vec3 GalagaGame::slotNow(const Enemy& e) const { return e.slot + Vec3{1.6f * std::sin(sway * 0.9f), 0.0f, 0.0f}; }

void GalagaGame::killPlayer(Particles& particles) {
    alive = false;
    lives--;
    respawn = 2.0f;
    audio::play(audio::Sfx::Hit, 1.0f, 0.6f); // la nave explota
    audio::play(audio::Sfx::Die, 0.8f);
    particles.burst(Vec3{playerX, 0.3f, PLAYER_Z}, Vec3{0, 0, 0}, 90, 1.0f, 7.0f, 2.5f, 0.4f, 1.0f);
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Shot& s) { return s.enemy; }), shots.end());
}

void GalagaGame::update(const ShipInput& in, float dt, Particles& particles) {
    if (dt <= 0.0f) return;
    time += dt;
    sway += dt;
    for (Vec3& s : stars) {
        s.z += 9.0f * dt;
        if (s.z > 20.0f) s.z -= 42.0f;
    }
    if (phase == Phase::GameOver) return;
    if (phase == Phase::Ready) {
        phaseTimer -= dt;
        if (phaseTimer <= 0.0f) phase = Phase::Playing;
    }
    if (phase == Phase::WaveClear) {
        phaseTimer -= dt;
        if (phaseTimer <= 0.0f) {
            wave++;
            spawnWave();
            phase = Phase::Playing;
        }
    }

    // Nave: A / D o flechas para moverse, Espacio para disparar.
    fireCooldown -= dt;
    invulnerable -= dt;
    if (alive) {
        playerX = clampf(playerX - in.steer * 14.0f * dt, -FIELD_X + 0.8f, FIELD_X - 0.8f);
        int mine = (int)std::count_if(shots.begin(), shots.end(), [](const Shot& s) { return !s.enemy; });
        if (in.brake && fireCooldown <= 0.0f && mine < 2 && phase != Phase::Ready) {
            shots.push_back(Shot{Vec3{playerX, 0.0f, PLAYER_Z - 1.0f}, Vec3{0.0f, 0.0f, -34.0f}, false});
            fireCooldown = 0.2f;
            audio::play(audio::Sfx::Laser);
        }
    } else {
        respawn -= dt;
        if (respawn <= 0.0f) {
            if (lives <= 0) {
                phase = Phase::GameOver;
                return;
            }
            alive = true;
            playerX = 0.0f;
            invulnerable = 2.5f;
        }
    }

    Vec3 player{playerX, 0.0f, PLAYER_Z};
    for (Enemy& e : enemies) {
        switch (e.state) {
            case EnemyState::Entering: { // curva desde arriba de un lado hasta su lugar
                e.t += dt;
                float s = (e.t - e.delay) / 1.7f;
                if (s < 0.0f) break;
                if (s >= 1.0f) {
                    e.state = EnemyState::Formation;
                    break;
                }
                Vec3 mid{-e.side * 6.0f, 0.0f, 4.0f};
                Vec3 end = slotNow(e);
                e.pos = lerp(lerp(e.from, mid, s), lerp(mid, end, s), s);
                break;
            }
            case EnemyState::Formation:
                e.pos = slotNow(e);
                break;
            case EnemyState::Diving: { // en picada hacia la nave, zigzagueando y disparando
                e.t += dt;
                e.pos.z += (9.0f + wave * 0.8f) * dt;
                e.pos.x += (e.side * 6.0f * std::cos(e.t * 2.4f) + (playerX - e.pos.x) * 0.8f) * dt;
                e.shotTimer -= dt;
                if (e.shotTimer <= 0.0f && e.pos.z < PLAYER_Z - 5.0f && alive) {
                    Vec3 dir = normalize(player - e.pos);
                    shots.push_back(Shot{e.pos, dir * 13.0f, true});
                    e.shotTimer = 0.9f;
                }
                if (e.pos.z > 20.0f) { // se fue por abajo: vuelve a entrar por arriba
                    e.state = EnemyState::Returning;
                    e.pos = Vec3{e.slot.x, 0.0f, -24.0f};
                }
                break;
            }
            case EnemyState::Returning: {
                Vec3 to = slotNow(e) - e.pos;
                float d = length(to);
                if (d < 0.3f) e.state = EnemyState::Formation;
                else e.pos += to * (std::min(d, 12.0f * dt) / d);
                break;
            }
            case EnemyState::Dead:
                break;
        }
    }

    // Cada cierto tiempo uno de la formacion se lanza en picada.
    diveTimer -= dt;
    if (diveTimer <= 0.0f && phase == Phase::Playing && alive) {
        std::vector<Enemy*> ready;
        for (Enemy& e : enemies)
            if (e.state == EnemyState::Formation) ready.push_back(&e);
        if (!ready.empty()) {
            Enemy* e = ready[(size_t)(randomUnit() * ready.size()) % ready.size()];
            e->state = EnemyState::Diving;
            e->t = 0.0f;
            e->side = randomUnit() < 0.5f ? -1.0f : 1.0f;
            e->shotTimer = 0.4f;
        }
        diveTimer = std::max(0.6f, 2.2f - 0.15f * wave) * (0.7f + 0.6f * randomUnit());
    }

    // Disparos y choques. Si un disparo le da a la nave, se destruye despues
    // de revisar todos (killPlayer borra disparos y no se puede hacer a mitad
    // de recorrer la lista).
    for (Shot& s : shots) s.pos += s.vel * dt;
    bool playerHit = false;
    for (size_t i = 0; i < shots.size();) {
        Shot& s = shots[i];
        bool remove = std::fabs(s.pos.z) > 25.0f || std::fabs(s.pos.x) > 20.0f;
        if (!remove && !s.enemy) {
            for (Enemy& e : enemies) {
                if (e.state == EnemyState::Dead || (e.state == EnemyState::Entering && e.t < e.delay)) continue;
                float dx = e.pos.x - s.pos.x, dz = e.pos.z - s.pos.z;
                if (dx * dx + dz * dz > 0.95f * 0.95f) continue;
                remove = true;
                e.hits++;
                if (e.type == 2 && e.hits < 2) { // el jefe aguanta un disparo
                    audio::play(audio::Sfx::Bump, 0.5f, 1.8f);
                    break;
                }
                audio::play(audio::Sfx::Hit);
                const int base[3] = {50, 80, 150};
                bool diving = e.state == EnemyState::Diving;
                points += base[e.type] * (diving ? (e.type == 2 ? 3 : 2) : 1);
                particles.burst(e.pos + Vec3{0.0f, 0.3f, 0.0f}, Vec3{0, 0, 0}, 35, 1.0f, 5.0f, 1.8f, 0.25f, 0.6f);
                e.state = EnemyState::Dead;
                break;
            }
        }
        if (!remove && s.enemy && alive && invulnerable <= 0.0f && !playerHit) {
            float dx = s.pos.x - playerX, dz = s.pos.z - PLAYER_Z;
            if (dx * dx + dz * dz < 0.7f * 0.7f) {
                remove = true;
                playerHit = true;
            }
        }
        if (remove) shots.erase(shots.begin() + i);
        else ++i;
    }
    if (playerHit) killPlayer(particles);
    if (alive && invulnerable <= 0.0f) {
        for (Enemy& e : enemies) {
            if (e.state != EnemyState::Diving) continue;
            float dx = e.pos.x - playerX, dz = e.pos.z - PLAYER_Z;
            if (dx * dx + dz * dz < 1.1f * 1.1f) {
                e.state = EnemyState::Dead;
                particles.burst(e.pos, Vec3{0, 0, 0}, 35, 1.0f, 5.0f, 1.8f, 0.25f, 0.6f);
                killPlayer(particles);
                break;
            }
        }
    }
    best = std::max(best, points);
    bool anyLeft = std::any_of(enemies.begin(), enemies.end(), [](const Enemy& e) { return e.state != EnemyState::Dead; });
    if (!anyLeft && phase == Phase::Playing) {
        phase = Phase::WaveClear;
        phaseTimer = 2.5f;
    }
}

void GalagaGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float t) const {
    for (size_t i = 0; i < stars.size(); ++i) {
        uint32_t c = i % 3 == 0 ? 0xFFFFFFFFu : (i % 3 == 1 ? 0xFFC8A0FFu : 0xFFFF90D8u);
        drawPoint3D(fb, u, stars[i], c, i % 5 == 0 ? 3 : 2);
    }
    u.model = Mat4::identity();
    drawMesh(fb, frame, u, voxelGlowShader, stats);

    if (alive && phase != Phase::GameOver && (invulnerable <= 0.0f || std::fmod(t, 0.2f) < 0.12f)) {
        u.model = translate(Vec3{playerX, 0.0f, PLAYER_Z});
        drawMesh(fb, fighter, u, voxelGlowShader, stats);
    }
    for (const Enemy& e : enemies) {
        if (e.state == EnemyState::Dead || (e.state == EnemyState::Entering && e.t < e.delay)) continue;
        const Mesh& mesh = e.type == 2 && e.hits == 1 ? bossHurt : enemyMeshes[e.type];
        float tilt = e.state == EnemyState::Diving ? e.side * 0.5f * std::sin(e.t * 2.4f) : 0.0f;
        // Aleteo: en la formacion se estiran un poco a lo ancho y vuelven.
        float flap = 1.0f + 0.08f * std::sin(t * 6.0f + e.slot.x);
        u.model = translate(e.pos) * rotateY(tilt) * scale3({flap, 1.0f, 1.0f});
        drawMesh(fb, mesh, u, voxelGlowShader, stats);
    }
    for (const Shot& s : shots) {
        u.model = translate(s.pos);
        drawMesh(fb, s.enemy ? enemyShot : playerShot, u, voxelGlowShader, stats);
    }
}

void GalagaGame::drawHud(Framebuffer& fb, float fps, bool showHelp) const {
    char line[96];
    darkenRect(fb, 8, 6, 220, 46, 0.55f);
    drawText(fb, 14, 11, "GALAGA", HUD_CYAN, 2);
    std::snprintf(line, sizeof(line), "PUNTOS %d   RECORD %d", points, best);
    drawText(fb, 14, 29, line, HUD_WHITE);
    std::snprintf(line, sizeof(line), "VIDAS %d   OLEADA %d", std::max(0, lives), wave);
    drawText(fb, 14, 40, line, HUD_PINK);
    std::snprintf(line, sizeof(line), "FPS %.0f", fps);
    drawText(fb, fb.width() - textWidth(line) - 12, 10, line, 0xFF60E880);

    const int H = fb.height();
    if (phase == Phase::Ready) centered(fb, H / 2 - 30, "LISTO!", HUD_GOLD, 4);
    if (phase == Phase::WaveClear) centered(fb, H / 2 - 30, "OLEADA COMPLETA", HUD_CYAN, 4);
    if (phase == Phase::GameOver) {
        centered(fb, H / 2 - 40, "GAME OVER", 0xFFFF6060, 5);
        centered(fb, H / 2 + 4, "ENTER: JUGAR DE NUEVO", HUD_WHITE, 2);
    }
    if (showHelp) {
        static const char* help[] = {"A / D O FLECHAS  MOVER", "ESPACIO  DISPARAR", "ENTER  REINICIAR", "U  DESPEGAR AL ESPACIO",
                                     "H  OCULTAR AYUDA"};
        darkenRect(fb, 8, 58, 200, 63, 0.6f);
        for (int i = 0; i < 5; ++i) drawText(fb, 14, 63 + i * 11, help[i], HUD_WHITE);
    } else {
        drawText(fb, fb.width() - textWidth("H: AYUDA   U: DESPEGAR") - 12, H - 18, "H: AYUDA   U: DESPEGAR", HUD_GREY);
    }
}

// ============================================================== Centipede

// Sprite redondo de 9 x 9 (los segmentos): orilla oscura y un brillo arriba.
static std::vector<std::string> roundSprite(char body, char dark, char light) {
    std::vector<std::string> rows;
    for (int y = 0; y < 9; ++y) {
        std::string row;
        for (int x = 0; x < 9; ++x) {
            float dx = x - 4.0f, dy = y - 4.0f, r = std::sqrt(dx * dx + dy * dy);
            char c = '.';
            if (r <= 4.3f) c = r > 3.3f ? dark : body;
            if (r <= 2.0f && dx < 0.0f && dy < 0.0f) c = light;
            row += c;
        }
        rows.push_back(row);
    }
    return rows;
}

static const std::vector<std::string> MUSHROOM = {
    "..PPPPP..", ".PPWPPPP.", "PPPPPWPPP", "PWPPPPPWP", "PPPPPPPPP", "...SSS...", "...SSS...", "...SSS...",
};
static const std::vector<std::string> SHOOTER = {
    "...W...", "..WWW..", ".WCWCW.", "WWWWWWW", ".WWWWW.", "..C.C..", "..C.C..",
};
static const std::vector<std::string> SPIDER = {
    "P.........P", ".P..PPP..P.", "..PPPPPPP..", "PPPRPPPRPPP", "..PPPPPPP..", ".P..PPP..P.", "P.........P",
};

Vec3 CentipedeGame::world(float x, float y, float height) const { return Vec3{x - 10.0f, height, y - 11.5f}; }

void CentipedeGame::build() {
    body = makeVoxelSprite(roundSprite('G', 'D', 'L'), {{'G', {0.35f, 0.9f, 0.3f}}, {'D', {0.1f, 0.5f, 0.2f}}, {'L', {0.8f, 1.0f, 0.6f}}},
                           0.11f, 3);
    std::vector<std::string> head = roundSprite('P', 'D', 'L');
    head[2][2] = head[2][6] = 'K'; // ojos
    head[3][2] = head[3][6] = 'W';
    headMesh = makeVoxelSprite(head, {{'P', {1.0f, 0.35f, 0.55f}}, {'D', {0.6f, 0.1f, 0.3f}}, {'L', {1.0f, 0.8f, 0.9f}},
                                      {'K', {0.05f, 0.05f, 0.05f}}, {'W', {1, 1, 1}}},
                               0.11f, 3);
    // Hongos: con cada disparo se pierde una parte (de 8 filas a 2).
    for (int hp = 1; hp <= 4; ++hp) {
        std::vector<std::string> rows(MUSHROOM.begin(), MUSHROOM.begin() + hp * 2);
        mushroomMeshes[hp - 1] =
            makeVoxelSprite(rows, {{'P', {0.85f, 0.3f, 0.85f}}, {'W', {1, 1, 1}}, {'S', {0.95f, 0.9f, 0.75f}}}, 0.1f, 3);
    }
    player = makeVoxelSprite(SHOOTER, {{'W', {0.9f, 0.95f, 1.0f}}, {'C', {0.3f, 0.9f, 1.0f}}}, 0.13f, 3);
    spider = makeVoxelSprite(SPIDER, {{'P', {0.75f, 0.35f, 1.0f}}, {'R', {1.0f, 0.2f, 0.2f}}}, 0.12f, 2);
    MeshBuilder s, f, b;
    box(s, Vec3{-0.06f, 0.1f, -0.3f}, Vec3{0.06f, 0.3f, 0.3f}, glowMaterial(s, Vec3{1.0f, 0.95f, 0.4f}, 0.0f));
    shot = s.mesh;
    box(f, Vec3{-10.5f, -0.2f, -12.0f}, Vec3{10.5f, 0.0f, 12.0f - PLAYER_ROWS}, glowMaterial(f, Vec3{0.06f, 0.03f, 0.11f}, 1.0f));
    box(f, Vec3{-10.5f, -0.2f, 12.0f - PLAYER_ROWS}, Vec3{10.5f, 0.0f, 12.0f}, glowMaterial(f, Vec3{0.12f, 0.05f, 0.2f}, 1.0f));
    floorMesh = f.mesh;
    int edge = glowMaterial(b, Vec3{0.4f, 0.95f, 0.45f}, 0.0f);
    box(b, Vec3{-11.0f, -0.2f, -12.0f}, Vec3{-10.6f, 0.5f, 12.0f}, edge);
    box(b, Vec3{10.6f, -0.2f, -12.0f}, Vec3{11.0f, 0.5f, 12.0f}, edge);
    border = b.mesh;

    eye = Vec3{0.0f, 25.0f, 15.5f};
    target = Vec3{0.0f, 0.0f, 1.2f};
    restart();
}

void CentipedeGame::scatterMushrooms(int count) {
    for (int i = 0; i < count; ++i) {
        int x = (int)(randomUnit() * COLS) % COLS;
        int y = 1 + (int)(randomUnit() * (ROWS - PLAYER_ROWS - 1)) % (ROWS - PLAYER_ROWS - 1);
        mushrooms[y][x] = 4;
    }
}

void CentipedeGame::restart() {
    points = 0;
    lives = 3;
    wave = 1;
    for (auto& row : mushrooms)
        for (int& m : row) m = 0;
    scatterMushrooms(32);
    px = COLS / 2.0f;
    py = ROWS - 1.0f;
    shotAlive = false;
    spiderAlive = false;
    spiderTimer = 5.0f;
    spawnCentipede();
    phase = Phase::Ready;
    phaseTimer = 1.5f;
}

void CentipedeGame::spawnCentipede() {
    segments.clear();
    int length = std::min(11, 8 + wave);
    for (int i = 0; i < length; ++i) {
        Segment s;
        s.gx = 10 - i;
        s.gy = 0;
        s.head = i == 0;
        stepSegment(s);
        segments.push_back(s);
    }
}

// Siguiente celda de un segmento: de lado si esta libre; si no (orilla u
// hongo) baja (o sube, dentro de la zona del jugador) una fila y da la vuelta.
void CentipedeGame::stepSegment(Segment& s) {
    int nx = s.gx + s.dir;
    bool blocked = nx < 0 || nx >= COLS || mushrooms[s.gy][nx] > 0;
    if (!blocked) {
        s.tx = nx;
        s.ty = s.gy;
        return;
    }
    if (s.vdir > 0 && s.gy >= ROWS - 1) s.vdir = -1;
    else if (s.vdir < 0 && s.gy <= ROWS - PLAYER_ROWS) s.vdir = 1;
    s.tx = s.gx;
    s.ty = s.gy + s.vdir;
    s.dir = -s.dir;
}

Vec3 CentipedeGame::segmentPos(const Segment& s) const {
    return lerp(world((float)s.gx, (float)s.gy, 0.0f), world((float)s.tx, (float)s.ty, 0.0f), s.t);
}

void CentipedeGame::update(const ShipInput& in, float dt, Particles& particles) {
    if (dt <= 0.0f) return;
    time += dt;
    switch (phase) {
        case Phase::Ready:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) phase = Phase::Playing;
            return;
        case Phase::Dying:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) {
                if (lives <= 0) {
                    phase = Phase::GameOver;
                    return;
                }
                spawnCentipede();
                spiderAlive = false;
                spiderTimer = 4.0f;
                px = COLS / 2.0f;
                py = ROWS - 1.0f;
                phase = Phase::Ready;
                phaseTimer = 1.2f;
            }
            return;
        case Phase::WaveClear:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) {
                wave++;
                scatterMushrooms(6);
                spawnCentipede();
                phase = Phase::Playing;
            }
            return;
        case Phase::GameOver:
            return;
        case Phase::Playing:
            break;
    }

    // Jugador: se mueve en su zona (abajo) y no atraviesa hongos.
    auto free = [&](float x, float y) {
        int cx = (int)std::lround(x), cy = (int)std::lround(y);
        return cx < 0 || cy < 0 || cx >= COLS || cy >= ROWS || mushrooms[cy][cx] == 0;
    };
    float up = in.throttle != 0.0f ? in.throttle : in.pitch;
    float nx = clampf(px - in.steer * 12.0f * dt, 0.0f, COLS - 1.0f);
    float ny = clampf(py - up * 10.0f * dt, (float)(ROWS - PLAYER_ROWS), ROWS - 1.0f);
    if (free(nx, py)) px = nx;
    if (free(px, ny)) py = ny;
    if (in.brake && !shotAlive) {
        shotAlive = true;
        shotX = px;
        shotY = py - 0.5f;
        audio::play(audio::Sfx::Laser, 0.6f, 1.3f);
    }

    // Disparo: sube rapido; se revisa en pasos cortos para no saltarse nada.
    if (shotAlive) {
        float travel = 45.0f * dt;
        for (int k = 0; k < 8 && shotAlive; ++k) {
            shotY -= travel / 8.0f;
            if (shotY < -0.5f) {
                shotAlive = false;
                break;
            }
            int cx = (int)std::lround(shotX), cy = (int)std::lround(shotY);
            if (cy >= 0 && cy < ROWS && cx >= 0 && cx < COLS && mushrooms[cy][cx] > 0) {
                mushrooms[cy][cx]--;
                points += mushrooms[cy][cx] == 0 ? 1 : 0;
                audio::play(audio::Sfx::Hit, 0.3f, 1.8f); // hongo
                shotAlive = false;
                break;
            }
            Vec3 sp = world(shotX, shotY, 0.0f);
            for (size_t i = 0; i < segments.size(); ++i) {
                Segment& s = segments[i];
                if (!s.alive || length(segmentPos(s) - sp) > 0.65f) continue;
                // El segmento se vuelve hongo y el de atras pasa a ser cabeza.
                s.alive = false;
                points += s.head ? 100 : 10;
                audio::play(audio::Sfx::Hit, 0.8f, s.head ? 0.85f : 1.1f);
                Vec3 p = segmentPos(s);
                int mx = (int)std::lround(p.x + 10.0f), my = (int)std::lround(p.z + 11.5f);
                if (mx >= 0 && mx < COLS && my >= 0 && my < ROWS) mushrooms[my][mx] = 4;
                if (i + 1 < segments.size() && segments[i + 1].alive) segments[i + 1].head = true;
                particles.burst(p + Vec3{0.0f, 0.3f, 0.0f}, Vec3{0, 0, 0}, 20, 1.0f, 4.0f, 1.2f, 0.2f, 0.5f);
                shotAlive = false;
                break;
            }
            if (shotAlive && spiderAlive && length(world(spiderX, spiderY, 0.0f) - sp) < 0.8f) {
                points += 600;
                audio::play(audio::Sfx::Hit, 1.0f, 0.7f);
                spiderAlive = false;
                spiderTimer = 5.0f;
                particles.burst(world(spiderX, spiderY, 0.3f), Vec3{0, 0, 0}, 40, 1.0f, 5.0f, 1.6f, 0.3f, 0.7f);
                shotAlive = false;
            }
        }
    }

    // Ciempies.
    float speed = 8.0f + 0.8f * wave;
    for (Segment& s : segments) {
        if (!s.alive) continue;
        s.t += speed * dt;
        while (s.t >= 1.0f) {
            s.t -= 1.0f;
            s.gx = s.tx;
            s.gy = s.ty;
            stepSegment(s);
        }
    }

    // Arana: entra por un lado, zigzaguea en la zona del jugador y se come hongos.
    if (!spiderAlive) {
        spiderTimer -= dt;
        if (spiderTimer <= 0.0f) {
            bool left = randomUnit() < 0.5f;
            spiderAlive = true;
            spiderX = left ? -1.0f : (float)COLS;
            spiderY = ROWS - PLAYER_ROWS + randomUnit() * PLAYER_ROWS;
            spiderVx = (left ? 1.0f : -1.0f) * (4.0f + 0.3f * wave);
            spiderVy = randomUnit() < 0.5f ? 6.0f : -6.0f;
        }
    } else {
        spiderX += spiderVx * dt;
        spiderY += spiderVy * dt;
        if (spiderY < ROWS - PLAYER_ROWS - 1.0f) spiderVy = std::fabs(spiderVy);
        if (spiderY > ROWS - 1.0f) spiderVy = -std::fabs(spiderVy);
        if (randomUnit() < dt * 0.8f) spiderVy = -spiderVy;
        int cx = (int)std::lround(spiderX), cy = (int)std::lround(spiderY);
        if (cx >= 0 && cx < COLS && cy >= 0 && cy < ROWS) mushrooms[cy][cx] = 0;
        if (spiderX < -2.0f || spiderX > COLS + 1.0f) {
            spiderAlive = false;
            spiderTimer = 3.0f;
        }
    }

    // Choque con el jugador.
    Vec3 me = world(px, py, 0.0f);
    bool hit = spiderAlive && length(world(spiderX, spiderY, 0.0f) - me) < 0.8f;
    for (const Segment& s : segments) hit = hit || (s.alive && length(segmentPos(s) - me) < 0.75f);
    if (hit) {
        lives--;
        phase = Phase::Dying;
        phaseTimer = 1.5f;
        audio::play(audio::Sfx::Die);
        particles.burst(me + Vec3{0.0f, 0.3f, 0.0f}, Vec3{0, 0, 0}, 70, 1.0f, 6.0f, 2.0f, 0.4f, 0.9f);
    }
    best = std::max(best, points);
    bool any = std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return s.alive; });
    if (!any && phase == Phase::Playing) {
        phase = Phase::WaveClear;
        phaseTimer = 2.0f;
    }
}

void CentipedeGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float t) const {
    u.model = Mat4::identity();
    drawMesh(fb, floorMesh, u, voxelGlowShader, stats);
    drawMesh(fb, border, u, voxelGlowShader, stats);
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x)
            if (mushrooms[y][x] > 0) {
                u.model = translate(world((float)x, (float)y, 0.0f));
                drawMesh(fb, mushroomMeshes[mushrooms[y][x] - 1], u, voxelGlowShader, stats);
            }
    for (const Segment& s : segments) {
        if (!s.alive) continue;
        // Cada segmento se mece un poco arriba y abajo, como si caminara.
        float bob = 0.06f * std::sin(t * 12.0f + s.gx * 0.9f);
        u.model = translate(segmentPos(s) + Vec3{0.0f, bob, 0.0f});
        drawMesh(fb, s.head ? headMesh : body, u, voxelGlowShader, stats);
    }
    if (phase != Phase::Dying && phase != Phase::GameOver) {
        u.model = translate(world(px, py, 0.0f));
        drawMesh(fb, player, u, voxelGlowShader, stats);
    }
    if (shotAlive) {
        u.model = translate(world(shotX, shotY, 0.0f));
        drawMesh(fb, shot, u, voxelGlowShader, stats);
    }
    if (spiderAlive) {
        u.model = translate(world(spiderX, spiderY, 0.05f)) * scale3({1.0f, 1.0f, 1.0f + 0.15f * std::sin(t * 18.0f)});
        drawMesh(fb, spider, u, voxelGlowShader, stats);
    }
}

void CentipedeGame::drawHud(Framebuffer& fb, float fps, bool showHelp) const {
    char line[96];
    darkenRect(fb, 8, 6, 220, 46, 0.55f);
    drawText(fb, 14, 11, "CENTIPEDE", 0xFF70F070, 2);
    std::snprintf(line, sizeof(line), "PUNTOS %d   RECORD %d", points, best);
    drawText(fb, 14, 29, line, HUD_WHITE);
    std::snprintf(line, sizeof(line), "VIDAS %d   OLEADA %d", std::max(0, lives), wave);
    drawText(fb, 14, 40, line, HUD_PINK);
    std::snprintf(line, sizeof(line), "FPS %.0f", fps);
    drawText(fb, fb.width() - textWidth(line) - 12, 10, line, 0xFF60E880);
    const int H = fb.height();
    if (phase == Phase::Ready) centered(fb, H / 2 - 30, "LISTO!", HUD_GOLD, 4);
    if (phase == Phase::WaveClear) centered(fb, H / 2 - 30, "OLEADA COMPLETA", HUD_CYAN, 4);
    if (phase == Phase::GameOver) {
        centered(fb, H / 2 - 40, "GAME OVER", 0xFFFF6060, 5);
        centered(fb, H / 2 + 4, "ENTER: JUGAR DE NUEVO", HUD_WHITE, 2);
    }
    if (showHelp) {
        static const char* help[] = {"W A S D / FLECHAS  MOVER", "ESPACIO  DISPARAR", "ENTER  REINICIAR", "U  DESPEGAR AL ESPACIO",
                                     "H  OCULTAR AYUDA"};
        darkenRect(fb, 8, 58, 200, 63, 0.6f);
        for (int i = 0; i < 5; ++i) drawText(fb, 14, 63 + i * 11, help[i], HUD_WHITE);
    } else {
        drawText(fb, fb.width() - textWidth("H: AYUDA   U: DESPEGAR") - 12, H - 18, "H: AYUDA   U: DESPEGAR", HUD_GREY);
    }
}

// ============================================================== Snake

static const std::vector<std::string> SNAKE_BODY = {
    "DGGGGGGD", "GGLGGGGG", "GLGGGGGG", "GGGGGGGG", "GGGGGGGG", "GGGGGGDG", "GGGGGDGG", "DGGGGGGD",
};
// Cabeza mirando a +X, con ojos y la lengua bifida.
static const std::vector<std::string> SNAKE_HEAD = {
    "..GGGGGG....", ".GGGGGGWKG..", "GGGGGGGWKGG.", "GGGGGGGGGGRR", "GGGGGGGGGG.R",
    "GGGGGGGGGGRR", "GGGGGGGWKGG.", ".GGGGGGWKG..", "..GGGGGG....",
};
static const std::vector<std::string> APPLE = {
    "....L....", "...LB....", "..RRBRR..", ".RRRRRRR.", "RRWRRRRRR", "RWRRRRRRR", "RRRRRRRRR", ".RRRRRRR.", "..RR.RR..",
};

Vec3 SnakeGame::world(float x, float y, float height) const { return Vec3{x - 11.5f, height, y - 8.5f}; }

void SnakeGame::build() {
    const std::map<char, Vec3> green = {{'G', {0.3f, 0.9f, 0.35f}}, {'L', {0.75f, 1.0f, 0.6f}}, {'D', {0.1f, 0.5f, 0.2f}},
                                        {'W', {1, 1, 1}}, {'K', {0.05f, 0.05f, 0.05f}}, {'R', {1.0f, 0.2f, 0.3f}}};
    segment = makeVoxelSprite(SNAKE_BODY, green, 0.115f, 4);
    headMesh = makeVoxelSprite(SNAKE_HEAD, green, 0.1f, 5);
    fruitMesh = makeVoxelSprite(APPLE, {{'R', {1.0f, 0.15f, 0.2f}}, {'W', {1.0f, 0.8f, 0.8f}}, {'L', {0.3f, 0.9f, 0.3f}},
                                        {'B', {0.5f, 0.3f, 0.15f}}},
                                0.1f, 4);
    // Tablero en cuadros y orilla de neon.
    MeshBuilder f, b;
    int a = glowMaterial(f, Vec3{0.1f, 0.05f, 0.18f}, 0.0f), c = glowMaterial(f, Vec3{0.14f, 0.07f, 0.24f}, 0.0f);
    for (int y = 0; y < ROWS; ++y)
        for (int x = 0; x < COLS; ++x) {
            Vec3 p = world((float)x, (float)y, 0.0f);
            box(f, p + Vec3{-0.5f, -0.2f, -0.5f}, p + Vec3{0.5f, 0.0f, 0.5f}, (x + y) % 2 ? a : c);
        }
    floorMesh = f.mesh;
    int edge = glowMaterial(b, Vec3{1.0f, 0.4f, 0.85f}, 0.0f);
    float x0 = -12.0f, x1 = 12.0f, z0 = -9.0f, z1 = 9.0f;
    box(b, Vec3{x0 - 0.4f, -0.2f, z0 - 0.4f}, Vec3{x1 + 0.4f, 0.6f, z0}, edge);
    box(b, Vec3{x0 - 0.4f, -0.2f, z1}, Vec3{x1 + 0.4f, 0.6f, z1 + 0.4f}, edge);
    box(b, Vec3{x0 - 0.4f, -0.2f, z0}, Vec3{x0, 0.6f, z1}, edge);
    box(b, Vec3{x1, -0.2f, z0}, Vec3{x1 + 0.4f, 0.6f, z1}, edge);
    border = b.mesh;

    eye = Vec3{0.0f, 20.0f, 13.5f};
    target = Vec3{0.0f, 0.0f, 1.0f};
    restart();
}

void SnakeGame::placeFruit() {
    for (int tries = 0; tries < 500; ++tries) {
        Cell c{(int)(randomUnit() * COLS) % COLS, (int)(randomUnit() * ROWS) % ROWS};
        bool onBody = false;
        for (const Cell& b : body) onBody = onBody || (b.x == c.x && b.y == c.y);
        if (!onBody) {
            fruit = c;
            return;
        }
    }
}

void SnakeGame::restart() {
    body = {{12, 9}, {11, 9}, {10, 9}, {9, 9}};
    previous = body;
    dx = 1;
    dy = 0;
    pending.clear();
    grow = 0;
    points = 0;
    interval = 0.15f;
    stepTimer = 0.0f;
    placeFruit();
    phase = Phase::Ready;
    phaseTimer = 1.2f;
}

void SnakeGame::update(const ShipInput& in, float dt, Particles& particles) {
    if (dt <= 0.0f) return;
    time += dt;
    // Direccion pedida (cada tecla nueva se guarda una vez, en orden).
    int wx = 0, wy = 0;
    if (in.throttle > 0 || in.pitch > 0) wy = -1;
    else if (in.throttle < 0 || in.pitch < 0) wy = 1;
    else if (in.steer > 0) wx = -1;
    else if (in.steer < 0) wx = 1;
    if ((wx || wy) && (wx != lastWantX || wy != lastWantY) && pending.size() < 2) pending.push_back(Cell{wx, wy});
    lastWantX = wx;
    lastWantY = wy;

    if (phase == Phase::GameOver) return;
    if (phase == Phase::Ready) {
        phaseTimer -= dt;
        if (phaseTimer > 0.0f) return;
        phase = Phase::Playing;
    }
    stepTimer += dt;
    while (stepTimer >= interval && phase == Phase::Playing) {
        stepTimer -= interval;
        // Aplicar el siguiente giro pedido (no se puede dar media vuelta).
        while (!pending.empty()) {
            Cell w = pending.front();
            pending.erase(pending.begin());
            if ((w.x == -dx && w.y == -dy) || (w.x == dx && w.y == dy)) continue;
            dx = w.x;
            dy = w.y;
            break;
        }
        Cell head{body[0].x + dx, body[0].y + dy};
        bool dead = head.x < 0 || head.y < 0 || head.x >= COLS || head.y >= ROWS;
        // La cola se mueve en este mismo paso (si no esta creciendo): no cuenta.
        size_t check = grow > 0 ? body.size() : body.size() - 1;
        for (size_t i = 0; i < check && !dead; ++i) dead = body[i].x == head.x && body[i].y == head.y;
        if (dead) {
            phase = Phase::GameOver;
            audio::play(audio::Sfx::Bump, 0.8f); // choca con la pared o con su cuerpo
            audio::play(audio::Sfx::Die);
            particles.burst(world((float)body[0].x, (float)body[0].y, 0.4f), Vec3{0, 0, 0}, 70, 1.0f, 6.0f, 2.0f, 0.4f, 0.9f);
            return;
        }
        std::vector<Cell> old = body;
        body.insert(body.begin(), head);
        if (grow > 0) grow--;
        else body.pop_back();
        previous.assign(body.size(), old[0]);
        for (size_t i = 1; i < body.size(); ++i) previous[i] = old[i - 1];
        if (head.x == fruit.x && head.y == fruit.y) {
            points += 10;
            grow += 3;
            audio::play(audio::Sfx::Pickup); // fruta
            interval = std::max(0.06f, interval * 0.95f);
            particles.burst(world((float)fruit.x, (float)fruit.y, 0.4f), Vec3{0, 0, 0}, 25, 1.0f, 4.0f, 1.2f, 0.2f, 0.5f);
            placeFruit();
        }
    }
    best = std::max(best, points);
}

void SnakeGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float t) const {
    u.model = Mat4::identity();
    drawMesh(fb, floorMesh, u, voxelGlowShader, stats);
    drawMesh(fb, border, u, voxelGlowShader, stats);
    // Fruta: flota y gira.
    u.model = translate(world((float)fruit.x, (float)fruit.y, 0.15f + 0.12f * std::sin(t * 4.0f))) * rotateY(t * 2.0f);
    drawMesh(fb, fruitMesh, u, voxelGlowShader, stats);
    // Serpiente: cada parte se desliza de su celda anterior a la nueva.
    float f = phase == Phase::Playing ? clampf(stepTimer / interval, 0.0f, 1.0f) : 1.0f;
    for (size_t i = body.size(); i-- > 0;) {
        Vec3 a = world((float)previous[i].x, (float)previous[i].y, 0.0f);
        Vec3 b = world((float)body[i].x, (float)body[i].y, 0.0f);
        if (i == 0) {
            u.model = translate(lerp(a, b, f)) * rotateY(std::atan2(-(float)dy, (float)dx));
            drawMesh(fb, headMesh, u, voxelGlowShader, stats);
        } else {
            u.model = translate(lerp(a, b, f));
            drawMesh(fb, segment, u, voxelGlowShader, stats);
        }
    }
}

void SnakeGame::drawHud(Framebuffer& fb, float fps, bool showHelp) const {
    char line[96];
    darkenRect(fb, 8, 6, 220, 46, 0.55f);
    drawText(fb, 14, 11, "SNAKE", 0xFF70F070, 2);
    std::snprintf(line, sizeof(line), "PUNTOS %d   RECORD %d", points, best);
    drawText(fb, 14, 29, line, HUD_WHITE);
    std::snprintf(line, sizeof(line), "LARGO %d", (int)body.size());
    drawText(fb, 14, 40, line, HUD_PINK);
    std::snprintf(line, sizeof(line), "FPS %.0f", fps);
    drawText(fb, fb.width() - textWidth(line) - 12, 10, line, 0xFF60E880);
    const int H = fb.height();
    if (phase == Phase::Ready) centered(fb, H / 2 - 30, "LISTO!", HUD_GOLD, 4);
    if (phase == Phase::GameOver) {
        centered(fb, H / 2 - 40, "GAME OVER", 0xFFFF6060, 5);
        centered(fb, H / 2 + 4, "ENTER: JUGAR DE NUEVO", HUD_WHITE, 2);
    }
    if (showHelp) {
        static const char* help[] = {"W A S D / FLECHAS  GIRAR", "ENTER  REINICIAR", "U  DESPEGAR AL ESPACIO", "H  OCULTAR AYUDA"};
        darkenRect(fb, 8, 58, 200, 52, 0.6f);
        for (int i = 0; i < 4; ++i) drawText(fb, 14, 63 + i * 11, help[i], HUD_WHITE);
    } else {
        drawText(fb, fb.width() - textWidth("H: AYUDA   U: DESPEGAR") - 12, H - 18, "H: AYUDA   U: DESPEGAR", HUD_GREY);
    }
}

// ============================================================== Infierno

const int HELL_SIZE = 25;
const float PLAYER_RADIUS = 0.28f, EYE_HEIGHT = 0.62f, WALL_VOXEL = 0.5f;
const int WALL_LAYERS = 3; // los muros miden 1.5 (3 cubos de 0.5)
// Mapa propio: # muro de piedra, R muro con runas que brillan, ~ lava (se
// puede pisar pero quema; los demonios no la cruzan), S portal por donde
// salen los demonios, H botiquin, A carga de plasma, P inicio del jugador.
static const char* HELL_MAP[HELL_SIZE] = {
    "#########################",
    "#S....#.......A.#......S#",
    "#.##..#..~~~....#..###..#",
    "#.#H.....~~~.......#H...#",
    "#.#..##R#....#R##..#....#",
    "#....#.........#.......##",
    "###..#..#...#..#..##....#",
    "#.......#...#.......#...#",
    "#..~~................~~.#",
    "#..~~..R##.....##R..~~..#",
    "#.......#.......#.......#",
    "####....#...P...#....####",
    "#S......#.......#......S#",
    "####....###...###....####",
    "#.......................#",
    "#..A..#...~~~~~...#..A..#",
    "#.....#...~~~~~...#.....#",
    "#..####...........####..#",
    "#.......##R...R##.......#",
    "#..H.................H..#",
    "###..#....#...#....#..###",
    "#....#....#...#....#....#",
    "#.~~.##...........##.~~.#",
    "#S..........A..........S#",
    "#########################",
};

// Demonios propios en pixel art (se paran de frente al jugador).
// Diablillo: rojo, con cuernos de hueso, ojos amarillos y colmillos.
static const std::vector<std::string> IMP_SPRITE = {
    "H..........H", "HH........HH", ".HH.RRRR.HH.", "..HRRRRRRH..", "..RRYRRYRR..", "..RRKRRKRR..", "..RRRRRRRR..",
    "...RWDDWR...", "....RRRR....", ".DRRRRRRRRD.", "D.RRRRRRRR.D", "D..RRDDRR..D", "...RR..RR...", "..DDD..DDD..",
};
// Bruto: morado oscuro, enorme, con cuernos curvos, ojos verdes y garras.
static const std::vector<std::string> BRUTE_SPRITE = {
    "..H..........H..", ".HH..........HH.", ".H...QQQQQQ...H.", ".HH.QPPPPPPQ.HH.", "..HHPPPPPPPPHH..", "...QPGGPPGGPQ...",
    "...QPPPPPPPPQ...", "...QPWKKKKWPQ...", "..QQPPWPPWPPQQ..", ".QPPPPPPPPPPPPQ.", "QPPQPPPPPPPPQPPQ", "QPPQPPPQQPPPQPPQ",
    "QPQ.PPPQQPPP.QPQ", "WW..PPP..PPP..WW", "....QQQ..QQQ....", "...QQQQ..QQQQ...",
};
const float IMP_PIXEL = 0.07f, BRUTE_PIXEL = 0.085f;
const float DEMON_RADIUS[2] = {0.3f, 0.45f};
const float DEMON_HEIGHT[2] = {14 * IMP_PIXEL, 16 * BRUTE_PIXEL};

static float hellHash(int x, int y, int z) {
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

// Que tan cerca de una arista del cubo esta el punto (como en voxels.cpp).
static float cubeEdge(const Vec3& p, float size) {
    if (size <= 0.0f) return 0.0f;
    float d[3] = {p.x / size, p.y / size, p.z / size};
    for (float& v : d) v = std::fabs(v - std::round(v));
    std::sort(d, d + 3);
    float t = clampf((d[1] - 0.04f) / 0.06f, 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

// Piedra de la fortaleza: luz de arriba (cada cara de los cubos con su tono),
// orillas oscuras y niebla morada con la distancia (se funde con el cielo).
static Vec3 hellShader(const Fragment& f, const Uniforms& u) {
    const Material& m = *f.material;
    const Vec3 light = normalize(Vec3{0.35f, 0.85f, 0.4f});
    float diffuse = std::max(0.0f, dot(f.normal, light));
    Vec3 lit = m.color * (0.45f + 0.75f * diffuse);
    Vec3 c = lerp(lit, m.color, m.emissive) * (1.0f - 0.35f * cubeEdge(f.localPos, m.voxelSize));
    float fog = clampf((length(f.worldPos - u.cameraPos) - 3.0f) / 16.0f, 0.0f, 0.75f);
    return lerp(c, Vec3{0.2f, 0.07f, 0.3f}, fog);
}

// Cara de un cubo (centro c, medio lado h) que mira hacia n.
static void cubeFace(MeshBuilder& b, const Vec3& c, float h, const Vec3& n, int mat) {
    Vec3 a = n.x != 0.0f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 bAxis = cross(n, a);
    Vec3 fc = c + n * h;
    Vec3 p0 = fc - a * h - bAxis * h, p1 = fc + a * h - bAxis * h, p2 = fc + a * h + bAxis * h, p3 = fc - a * h + bAxis * h;
    b.triangle(p0, p1, p2, mat, c);
    b.triangle(p0, p2, p3, mat, c);
}

bool InfiernoGame::solidCell(int x, int y) const {
    if (x < 0 || y < 0 || x >= HELL_SIZE || y >= HELL_SIZE) return true;
    char c = map[y][x];
    return c == '#' || c == 'R';
}

// Una celda del mapa ocupa [x, x+1] x [y, y+1] en el mundo (X, Z).
bool InfiernoGame::blocked(float x, float z, bool demon) const {
    int cx = (int)std::floor(x), cy = (int)std::floor(z);
    if (solidCell(cx, cy)) return true;
    return demon && map[cy][cx] == '~';
}

// Mueve un circulo por el mapa, primero en X y luego en Z, para que se
// deslice a lo largo de los muros en vez de quedarse pegado.
void InfiernoGame::moveCircle(Vec3& p, const Vec3& delta, float radius, bool demon) const {
    auto fits = [&](float x, float z) {
        int x0 = (int)std::floor(x - radius), x1 = (int)std::floor(x + radius);
        int z0 = (int)std::floor(z - radius), z1 = (int)std::floor(z + radius);
        for (int cz = z0; cz <= z1; ++cz)
            for (int cx = x0; cx <= x1; ++cx) {
                if (!blocked(cx + 0.5f, cz + 0.5f, demon)) continue;
                float nx = clampf(x, (float)cx, cx + 1.0f), nz = clampf(z, (float)cz, cz + 1.0f);
                if ((x - nx) * (x - nx) + (z - nz) * (z - nz) < radius * radius) return false;
            }
        return true;
    };
    if (fits(p.x + delta.x, p.z)) p.x += delta.x;
    if (fits(p.x, p.z + delta.z)) p.z += delta.z;
}

bool InfiernoGame::lineOfSight(const Vec3& a, const Vec3& b) const {
    Vec3 d{b.x - a.x, 0.0f, b.z - a.z};
    float len = length(d);
    int steps = (int)(len / 0.15f) + 1;
    for (int i = 1; i < steps; ++i) {
        float t = (float)i / steps;
        if (solidCell((int)std::floor(a.x + d.x * t), (int)std::floor(a.z + d.z * t))) return false;
    }
    return true;
}

// Campo de distancias: pasos desde cada celda hasta la del jugador, sin
// cruzar muros ni lava. Un demonio que no ve al jugador va a la celda vecina
// con menos pasos, asi lo encuentra rodeando los muros.
void InfiernoGame::computeFlow() {
    flow.assign(HELL_SIZE * HELL_SIZE, -1);
    std::vector<int> queue;
    int start = (int)std::floor(player.z) * HELL_SIZE + (int)std::floor(player.x);
    flow[start] = 0;
    queue.push_back(start);
    for (size_t i = 0; i < queue.size(); ++i) {
        int cx = queue[i] % HELL_SIZE, cy = queue[i] / HELL_SIZE;
        const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : dirs) {
            int nx = cx + d[0], ny = cy + d[1];
            if (solidCell(nx, ny) || map[ny][nx] == '~' || flow[ny * HELL_SIZE + nx] >= 0) continue;
            flow[ny * HELL_SIZE + nx] = flow[queue[i]] + 1;
            queue.push_back(ny * HELL_SIZE + nx);
        }
    }
}

Vec3 InfiernoGame::forward() const { return Vec3{std::sin(yaw), 0.0f, std::cos(yaw)}; }

void InfiernoGame::placeCamera() {
    float height = EYE_HEIGHT + std::sin(bob) * 0.025f * walkAmount;
    eye = Vec3{player.x, height - fall * (height - 0.15f), player.z};
    target = eye + forward() + Vec3{0.0f, -0.3f * fall, 0.0f};
}

void InfiernoGame::build() {
    map.assign(HELL_MAP, HELL_MAP + HELL_SIZE);

    // Muros de cubos de 0.5: solo se generan las caras que dan al aire.
    MeshBuilder w;
    const Vec3 stones[4] = {{0.34f, 0.16f, 0.14f}, {0.28f, 0.13f, 0.12f}, {0.4f, 0.2f, 0.16f}, {0.22f, 0.1f, 0.11f}};
    int stone[4];
    for (int i = 0; i < 4; ++i) {
        stone[i] = w.material("Piedra", stones[i], 0.0f, 1.0f, 0.0f);
        w.mesh.materials[stone[i]].voxelSize = WALL_VOXEL;
    }
    int rune = glowMaterial(w, Vec3{1.0f, 0.3f, 0.75f}, WALL_VOXEL);
    const int N = HELL_SIZE * 2;
    auto wallVoxel = [&](int i, int j, int k) {
        if (j < 0) return true; // el suelo tapa las caras de abajo
        if (j >= WALL_LAYERS) return false;
        if (i < 0 || k < 0 || i >= N || k >= N) return true;
        return solidCell(i / 2, k / 2);
    };
    const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < N; ++i)
        for (int k = 0; k < N; ++k)
            for (int j = 0; j < WALL_LAYERS; ++j) {
                if (!wallVoxel(i, j, k)) continue;
                bool runic = map[k / 2][i / 2] == 'R' && j == 1 && (i + k) % 2 == 0;
                int mat = runic ? rune : stone[(int)(hellHash(i, j, k) * 3.99f)];
                Vec3 c{(i + 0.5f) * WALL_VOXEL, (j + 0.5f) * WALL_VOXEL, (k + 0.5f) * WALL_VOXEL};
                for (const auto& d : dirs)
                    if (!wallVoxel(i + d[0], j + d[1], k + d[2]))
                        cubeFace(w, c, WALL_VOXEL * 0.5f, Vec3{(float)d[0], (float)d[1], (float)d[2]}, mat);
            }
    walls = w.mesh;

    // Suelo de baldosas oscuras y charcos de lava (malla aparte: brilla y late).
    MeshBuilder f, l;
    const Vec3 tiles[3] = {{0.16f, 0.09f, 0.12f}, {0.2f, 0.11f, 0.14f}, {0.13f, 0.07f, 0.1f}};
    int tile[3];
    for (int i = 0; i < 3; ++i) {
        tile[i] = f.material("Baldosa", tiles[i], 0.0f, 1.0f, 0.0f);
        f.mesh.materials[tile[i]].voxelSize = WALL_VOXEL;
    }
    const Vec3 lavas[3] = {{1.0f, 0.45f, 0.08f}, {1.0f, 0.7f, 0.15f}, {0.9f, 0.25f, 0.05f}};
    int lavaMat[3];
    for (int i = 0; i < 3; ++i) lavaMat[i] = glowMaterial(l, lavas[i], WALL_VOXEL);
    for (int i = 0; i < N; ++i)
        for (int k = 0; k < N; ++k) {
            char ch = map[k / 2][i / 2];
            if (ch == '#' || ch == 'R') continue;
            Vec3 c{(i + 0.5f) * WALL_VOXEL, -0.5f * WALL_VOXEL, (k + 0.5f) * WALL_VOXEL};
            int pick = (int)(hellHash(i, 7, k) * 2.99f);
            if (ch == '~') cubeFace(l, c, WALL_VOXEL * 0.5f, Vec3{0, 1, 0}, lavaMat[pick]);
            else cubeFace(f, c, WALL_VOXEL * 0.5f, Vec3{0, 1, 0}, tile[pick]);
        }
    floorMesh = f.mesh;
    lava = l.mesh;

    const std::map<char, Vec3> impColors = {{'R', {0.85f, 0.15f, 0.1f}}, {'D', {0.5f, 0.06f, 0.06f}}, {'Y', {1.0f, 0.9f, 0.2f}},
                                            {'K', {0.1f, 0.02f, 0.02f}}, {'H', {0.9f, 0.85f, 0.7f}}, {'W', {1, 1, 1}}};
    const std::map<char, Vec3> bruteColors = {{'P', {0.45f, 0.15f, 0.55f}}, {'Q', {0.22f, 0.06f, 0.3f}}, {'G', {0.4f, 1.0f, 0.3f}},
                                              {'K', {0.08f, 0.02f, 0.08f}}, {'H', {0.85f, 0.8f, 0.65f}}, {'W', {1, 1, 1}}};
    demonMeshes[0] = makeVoxelSprite(IMP_SPRITE, impColors, IMP_PIXEL, 2);
    demonMeshes[1] = makeVoxelSprite(BRUTE_SPRITE, bruteColors, BRUTE_PIXEL, 2);
    // Al recibir un disparo destellan en blanco.
    std::map<char, Vec3> white;
    for (char ch : std::string("RDYKHWPQG")) white[ch] = Vec3{1.0f, 0.95f, 0.9f};
    demonHurt[0] = makeVoxelSprite(IMP_SPRITE, white, IMP_PIXEL, 2);
    demonHurt[1] = makeVoxelSprite(BRUTE_SPRITE, white, BRUTE_PIXEL, 2);

    // Bola de fuego: cubos amarillos y naranjas.
    MeshBuilder fb;
    int core = glowMaterial(fb, Vec3{1.0f, 0.9f, 0.4f}, 0.0f), shell = glowMaterial(fb, Vec3{1.0f, 0.4f, 0.08f}, 0.0f);
    box(fb, Vec3{-0.07f, -0.07f, -0.07f}, Vec3{0.07f, 0.07f, 0.07f}, core);
    for (int a = 0; a < 3; ++a) {
        Vec3 e{a == 0 ? 0.11f : 0.06f, a == 1 ? 0.11f : 0.06f, a == 2 ? 0.11f : 0.06f};
        box(fb, -e, e, shell);
    }
    fireball = fb.mesh;

    // Lanzaplasma (en el espacio de la camara, el canon hacia -Z).
    MeshBuilder g;
    int metal = g.material("Metal", Vec3{0.35f, 0.33f, 0.4f}, 0.0f, 1.0f, 0.0f);
    int dark = g.material("Oscuro", Vec3{0.15f, 0.13f, 0.18f}, 0.0f, 1.0f, 0.0f);
    int coil = glowMaterial(g, Vec3{1.0f, 0.35f, 0.85f}, 0.0f);
    box(g, Vec3{-0.025f, -0.03f, -0.02f}, Vec3{0.025f, 0.02f, 0.09f}, metal);   // cuerpo
    box(g, Vec3{-0.013f, -0.017f, -0.13f}, Vec3{0.013f, 0.009f, -0.02f}, dark); // canon
    for (int i = 0; i < 3; ++i) {
        float z = -0.035f - i * 0.03f;
        box(g, Vec3{-0.02f, -0.024f, z - 0.008f}, Vec3{0.02f, 0.016f, z + 0.008f}, coil); // anillos de plasma
    }
    box(g, Vec3{-0.015f, -0.08f, 0.04f}, Vec3{0.015f, -0.03f, 0.07f}, dark); // mango
    gun = g.mesh;
    MeshBuilder fl;
    int flashMat = glowMaterial(fl, Vec3{1.0f, 0.75f, 1.0f}, 0.0f);
    box(fl, Vec3{-0.03f, -0.034f, -0.19f}, Vec3{0.03f, 0.026f, -0.13f}, flashMat);
    flash = fl.mesh;

    // Botiquin (blanco con cruz roja) y carga de plasma (amarilla con franja magenta).
    MeshBuilder m;
    int whiteMat = glowMaterial(m, Vec3{0.95f, 0.95f, 0.95f}, 0.0f), red = glowMaterial(m, Vec3{1.0f, 0.15f, 0.15f}, 0.0f);
    box(m, Vec3{-0.16f, 0.0f, -0.16f}, Vec3{0.16f, 0.22f, 0.16f}, whiteMat);
    box(m, Vec3{-0.17f, 0.08f, -0.04f}, Vec3{0.17f, 0.14f, 0.04f}, red);
    box(m, Vec3{-0.04f, 0.08f, -0.17f}, Vec3{0.04f, 0.14f, 0.17f}, red);
    box(m, Vec3{-0.04f, 0.0f, -0.12f}, Vec3{0.04f, 0.23f, 0.12f}, red);
    box(m, Vec3{-0.12f, 0.0f, -0.04f}, Vec3{0.12f, 0.23f, 0.04f}, red);
    medkit = m.mesh;
    MeshBuilder c;
    int brass = glowMaterial(c, Vec3{1.0f, 0.78f, 0.25f}, 0.0f), band = glowMaterial(c, Vec3{1.0f, 0.3f, 0.85f}, 0.0f);
    box(c, Vec3{-0.1f, 0.0f, -0.1f}, Vec3{0.1f, 0.3f, 0.1f}, brass);
    box(c, Vec3{-0.11f, 0.12f, -0.11f}, Vec3{0.11f, 0.18f, 0.11f}, band);
    cell = c.mesh;

    for (int y = 0; y < HELL_SIZE; ++y)
        for (int x = 0; x < HELL_SIZE; ++x) {
            char ch = map[y][x];
            if (ch == 'P') start = Vec3{x + 0.5f, 0.0f, y + 0.5f};
            if (ch == 'S') spawns.push_back(Vec3{x + 0.5f, 0.0f, y + 0.5f});
            if (ch == 'H' || ch == 'A') pickups.push_back(Pickup{x, y, ch, 0.0f});
        }
    restart();
}

void InfiernoGame::restart() {
    player = start;
    yaw = PI; // mirando hacia -Z (al norte del mapa)
    health = 100.0f;
    ammo = 40;
    points = 0;
    wave = 1;
    kills = 0;
    fall = 0.0f;
    hurtFlash = muzzle = recoil = 0.0f;
    demons.clear();
    fireballs.clear();
    for (Pickup& p : pickups) p.respawn = 0.0f;
    pending = pendingBrutes = 0;
    phase = Phase::Ready;
    phaseTimer = 2.0f;
    computeFlow();
    placeCamera();
}

void InfiernoGame::hurtPlayer(float amount) {
    if (phase == Phase::GameOver) return;
    health -= amount;
    hurtFlash = std::min(1.0f, hurtFlash + amount / 20.0f);
    // Golpes (bolas de fuego, zarpazos); la lava quema poco a poco y no se oye a cada cuadro.
    if (amount >= 5.0f) audio::play(audio::Sfx::Hurt);
    if (health <= 0.0f) {
        health = 0.0f;
        audio::play(audio::Sfx::Die);
        phase = Phase::GameOver;
        best = std::max(best, points);
    }
}

// Sale un demonio por un portal que no este encima del jugador.
void InfiernoGame::spawnDemon(Particles& particles) {
    if (spawns.empty()) return;
    Vec3 where = spawns[0];
    for (int tries = 0; tries < 12; ++tries) {
        where = spawns[(size_t)(randomUnit() * spawns.size()) % spawns.size()];
        if (length(where - player) > 6.0f) break;
    }
    Demon d;
    d.type = pendingBrutes > 0 && randomUnit() < 0.5f ? 1 : 0;
    if (d.type == 0 && pending <= pendingBrutes) d.type = 1; // los que quedan son brutos
    if (d.type == 1) pendingBrutes--;
    pending--;
    d.pos = where;
    d.hp = d.type ? 6.0f : 2.0f;
    d.attack = 1.0f + randomUnit();
    demons.push_back(d);
    particles.burst(where + Vec3{0.0f, 0.5f, 0.0f}, Vec3{0, 0, 0}, 40, 0.5f, 3.0f, 1.2f, 0.3f, 0.7f);
}

// Disparo instantaneo hacia el frente: pega en el primer demonio que cruza
// la linea de tiro antes del muro.
void InfiernoGame::shoot(Particles& particles) {
    ammo--;
    fireCooldown = 0.3f;
    audio::play(audio::Sfx::Plasma);
    muzzle = 0.07f;
    recoil = 1.0f;
    Vec3 f = forward();
    float wallT = 30.0f;
    for (float t = 0.05f; t < 30.0f; t += 0.05f)
        if (solidCell((int)std::floor(player.x + f.x * t), (int)std::floor(player.z + f.z * t))) {
            wallT = t;
            break;
        }
    Demon* hit = nullptr;
    float hitT = wallT;
    for (Demon& d : demons) {
        if (d.dying > 0.0f) continue;
        Vec3 rel{d.pos.x - player.x, 0.0f, d.pos.z - player.z};
        float t = dot(rel, f);
        if (t <= 0.0f || t >= hitT) continue;
        if (length(rel - f * t) < DEMON_RADIUS[d.type] + 0.15f) {
            hit = &d;
            hitT = t;
        }
    }
    Vec3 point = Vec3{player.x, 0.55f, player.z} + f * (hit ? hitT : wallT - 0.05f);
    if (!hit) {
        particles.burst(point, Vec3{0, 0, 0}, 10, 0.3f, 1.5f, 0.5f, 0.1f, 0.25f);
        return;
    }
    hit->hp -= 1.0f;
    hit->hurt = 0.12f;
    audio::play(audio::Sfx::Hit, hit->hp <= 0.0f ? 1.0f : 0.5f, hit->hp <= 0.0f ? 0.6f : 1.2f);
    particles.burst(point, Vec3{0, 0, 0}, 16, 0.5f, 2.5f, 0.6f, 0.15f, 0.35f);
    if (hit->hp <= 0.0f) {
        hit->dying = 0.001f;
        points += hit->type ? 300 : 100;
        kills++;
        particles.burst(hit->pos + Vec3{0.0f, 0.5f, 0.0f}, Vec3{0, 0, 0}, 45, 0.8f, 3.5f, 1.2f, 0.3f, 0.7f);
    }
}

void InfiernoGame::update(const ShipInput& in, float dt, Particles& particles) {
    time += dt;
    hurtFlash = std::max(0.0f, hurtFlash - dt * 1.5f);
    muzzle -= dt;
    recoil = std::max(0.0f, recoil - dt * 6.0f);
    fireCooldown -= dt;
    phaseTimer -= dt;
    if (phase == Phase::GameOver) {
        fall = std::min(1.0f, fall + dt * 1.5f); // cae al suelo
        placeCamera();
        return;
    }
    if ((phase == Phase::Ready || phase == Phase::WaveClear) && phaseTimer <= 0.0f) {
        if (phase == Phase::WaveClear) wave++;
        phase = Phase::Playing;
        pendingBrutes = wave - 1;
        pending = 3 + wave + pendingBrutes;
        spawnTimer = 0.0f;
    }

    // Jugador: W / S (o flechas) camina, A / D gira, Q / E paso de lado.
    yaw += in.steer * 2.6f * dt;
    Vec3 f = forward(), right{-f.z, 0.0f, f.x};
    Vec3 move = f * clampf(in.throttle + in.pitch, -1.0f, 1.0f) + right * in.roll;
    if (length(move) > 1.0f) move = normalize(move);
    moveCircle(player, move * 3.6f * dt, PLAYER_RADIUS, false);
    walkAmount += ((length(move) > 0.1f ? 1.0f : 0.0f) - walkAmount) * std::min(1.0f, dt * 8.0f);
    bob += dt * 9.0f * walkAmount;
    if (map[(int)std::floor(player.z)][(int)std::floor(player.x)] == '~') hurtPlayer(15.0f * dt); // la lava quema

    for (Pickup& p : pickups) {
        if (p.respawn > 0.0f) {
            p.respawn -= dt;
            continue;
        }
        if (length(Vec3{p.x + 0.5f, 0.0f, p.y + 0.5f} - player) > 0.55f) continue;
        if (p.kind == 'H' && health < 100.0f) {
            health = std::min(100.0f, health + 25.0f);
            p.respawn = 20.0f;
            audio::play(audio::Sfx::Pickup, 1.0f, 0.8f); // botiquin
        } else if (p.kind == 'A' && ammo < 99) {
            ammo = std::min(99, ammo + 12);
            p.respawn = 20.0f;
            audio::play(audio::Sfx::Pickup, 1.0f, 1.2f); // carga de plasma
        }
    }

    if (in.brake && fireCooldown <= 0.0f && ammo > 0 && phase != Phase::Ready) shoot(particles);

    if ((flowTimer -= dt) <= 0.0f) {
        computeFlow();
        flowTimer = 0.25f;
    }
    if (phase == Phase::Playing && pending > 0 && (spawnTimer -= dt) <= 0.0f && demons.size() < 10) {
        spawnDemon(particles);
        spawnTimer = 1.3f;
    }

    // Demonios.
    for (Demon& d : demons) {
        if (d.dying > 0.0f) {
            d.dying += dt;
            continue;
        }
        d.hurt -= dt;
        d.attack -= dt;
        Vec3 to{player.x - d.pos.x, 0.0f, player.z - d.pos.z};
        float dist = length(to);
        bool sees = dist < 14.0f && lineOfSight(d.pos, player);
        Vec3 goal = player;
        if (!sees) {
            // Hacia la celda vecina mas cercana al jugador segun el campo de distancias.
            int cx = (int)std::floor(d.pos.x), cy = (int)std::floor(d.pos.z);
            int bestSteps = flow[cy * HELL_SIZE + cx];
            const int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& dd : dirs) {
                int nx = cx + dd[0], ny = cy + dd[1];
                int steps = solidCell(nx, ny) ? -1 : flow[ny * HELL_SIZE + nx];
                if (steps >= 0 && (bestSteps < 0 || steps < bestSteps)) {
                    bestSteps = steps;
                    goal = Vec3{nx + 0.5f, 0.0f, ny + 0.5f};
                }
            }
        }
        float stopAt = d.type == 0 ? (sees ? 3.5f : 0.0f) : DEMON_RADIUS[1] + PLAYER_RADIUS;
        Vec3 dir{goal.x - d.pos.x, 0.0f, goal.z - d.pos.z};
        if (length(dir) > 0.01f && !(sees && dist < stopAt)) {
            float speed = d.type ? 2.1f : 1.5f;
            moveCircle(d.pos, normalize(dir) * speed * dt, DEMON_RADIUS[d.type], true);
            d.walk += dt * speed * 4.0f;
        }
        if (d.type == 0 && sees && dist < 9.0f && d.attack <= 0.0f) {
            // Lanza una bola de fuego hacia donde esta el jugador.
            Vec3 aim = normalize(to);
            fireballs.push_back(Fireball{d.pos + Vec3{0.0f, 0.55f, 0.0f} + aim * 0.4f, aim * 5.0f, 5.0f});
            d.attack = 2.6f + randomUnit() * 1.8f;
        }
        if (d.type == 1 && dist < DEMON_RADIUS[1] + PLAYER_RADIUS + 0.3f && d.attack <= 0.0f) {
            hurtPlayer(15.0f); // zarpazo
            d.attack = 0.9f;
        }
    }
    // Los demonios no se encimen entre ellos.
    for (size_t i = 0; i < demons.size(); ++i)
        for (size_t j = i + 1; j < demons.size(); ++j) {
            Demon &a = demons[i], &b = demons[j];
            if (a.dying > 0.0f || b.dying > 0.0f) continue;
            Vec3 d{b.pos.x - a.pos.x, 0.0f, b.pos.z - a.pos.z};
            float gap = DEMON_RADIUS[a.type] + DEMON_RADIUS[b.type], len = length(d);
            if (len < gap && len > 1e-4f) {
                Vec3 push = d * ((gap - len) * 0.5f / len);
                moveCircle(a.pos, -push, DEMON_RADIUS[a.type], true);
                moveCircle(b.pos, push, DEMON_RADIUS[b.type], true);
            }
        }
    demons.erase(std::remove_if(demons.begin(), demons.end(), [](const Demon& d) { return d.dying > 0.6f; }), demons.end());

    for (Fireball& b : fireballs) {
        b.pos += b.vel * dt;
        b.life -= dt;
        if (solidCell((int)std::floor(b.pos.x), (int)std::floor(b.pos.z))) {
            particles.burst(b.pos - b.vel * 0.02f, Vec3{0, 0, 0}, 14, 0.4f, 2.0f, 0.7f, 0.15f, 0.35f);
            b.life = 0.0f;
        } else if (length(Vec3{b.pos.x - player.x, 0.0f, b.pos.z - player.z}) < PLAYER_RADIUS + 0.12f) {
            particles.burst(b.pos, Vec3{0, 0, 0}, 20, 0.5f, 2.5f, 0.8f, 0.15f, 0.4f);
            hurtPlayer(8.0f);
            b.life = 0.0f;
        }
    }
    fireballs.erase(std::remove_if(fireballs.begin(), fireballs.end(), [](const Fireball& b) { return b.life <= 0.0f; }),
                    fireballs.end());

    if (phase == Phase::Playing && pending == 0 && demons.empty()) {
        phase = Phase::WaveClear;
        phaseTimer = 3.0f;
        points += 500 * wave;
        ammo = std::min(99, ammo + 10);
        health = std::min(100.0f, health + 15.0f);
    }
    placeCamera();
}

void InfiernoGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, float t) const {
    u.model = Mat4::identity();
    drawMesh(fb, floorMesh, u, hellShader, stats);
    drawMesh(fb, lava, u, voxelGlowShader, stats);
    drawMesh(fb, walls, u, hellShader, stats);

    for (const Pickup& p : pickups) {
        if (p.respawn > 0.0f) continue;
        Vec3 c{p.x + 0.5f, 0.08f + 0.06f * std::sin(t * 3.0f + p.x), p.y + 0.5f};
        u.model = translate(c) * rotateY(t * 1.5f);
        drawMesh(fb, p.kind == 'H' ? medkit : cell, u, hellShader, stats);
    }
    // Demonios: de frente a la camara; al morir se aplastan contra el suelo.
    for (const Demon& d : demons) {
        float squash = d.dying > 0.0f ? 1.0f - 0.9f * std::min(1.0f, d.dying / 0.5f) : 1.0f;
        float hop = d.dying > 0.0f ? 0.0f : std::fabs(std::sin(d.walk)) * 0.05f;
        float face = std::atan2(eye.x - d.pos.x, eye.z - d.pos.z);
        u.model = translate(Vec3{d.pos.x, hop, d.pos.z}) * scale3(Vec3{1.0f, squash, 1.0f}) *
                  translate(Vec3{0.0f, DEMON_HEIGHT[d.type] * 0.5f, 0.0f}) * rotateY(face) * rotateX(PI * 0.5f);
        drawMesh(fb, d.hurt > 0.0f ? demonHurt[d.type] : demonMeshes[d.type], u, hellShader, stats);
    }
    for (const Fireball& b : fireballs) {
        u.model = translate(b.pos) * rotateY(t * 9.0f) * rotateX(t * 7.0f);
        drawMesh(fb, fireball, u, voxelGlowShader, stats);
    }

    // El arma, pegada a la camara (abajo a la derecha), con vaiven al caminar
    // y retroceso al disparar.
    if (phase == Phase::GameOver) return;
    Vec3 f = normalize(target - eye);
    Vec3 right = normalize(cross(f, Vec3{0, 1, 0}));
    Vec3 up = cross(right, f);
    float sway = std::sin(bob * 0.5f) * 0.008f * walkAmount, lift = std::fabs(std::cos(bob * 0.5f)) * 0.006f * walkAmount;
    u.model = fromBasis(right, up, -f, eye) * translate(Vec3{0.07f + sway, -0.075f + lift, -0.3f + 0.03f * recoil}) *
              rotateX(0.15f * recoil) * scale(0.75f);
    drawMesh(fb, gun, u, hellShader, stats);
    if (muzzle > 0.0f) drawMesh(fb, flash, u, voxelGlowShader, stats);
}

void InfiernoGame::drawHud(Framebuffer& fb, float fps, bool showHelp) const {
    const int W = fb.width(), H = fb.height();
    if (hurtFlash > 0.0f) tintScreen(fb, Vec3{0.9f, 0.05f, 0.05f}, hurtFlash * 0.45f);

    // Mira al centro.
    if (phase != Phase::GameOver) {
        fillRect(fb, W / 2 - 6, H / 2, 4, 1, HUD_WHITE);
        fillRect(fb, W / 2 + 3, H / 2, 4, 1, HUD_WHITE);
        fillRect(fb, W / 2, H / 2 - 6, 1, 4, HUD_WHITE);
        fillRect(fb, W / 2, H / 2 + 3, 1, 4, HUD_WHITE);
    }

    char line[96];
    darkenRect(fb, 8, 6, 220, 46, 0.55f);
    drawText(fb, 14, 11, "INFIERNO", 0xFFFF5040, 2);
    std::snprintf(line, sizeof(line), "PUNTOS %d   RECORD %d", points, std::max(best, points));
    drawText(fb, 14, 29, line, HUD_WHITE);
    std::snprintf(line, sizeof(line), "OLEADA %d   DEMONIOS %d", wave, (int)demons.size() + pending);
    drawText(fb, 14, 40, line, HUD_PINK);
    std::snprintf(line, sizeof(line), "FPS %.0f", fps);
    drawText(fb, W - textWidth(line) - 12, 10, line, 0xFF60E880);

    // Barra de abajo: salud y plasma.
    darkenRect(fb, 0, H - 30, W, 30, 0.6f);
    uint32_t healthColor = health > 50.0f ? 0xFF60E880 : (health > 25.0f ? HUD_GOLD : 0xFFFF5040);
    std::snprintf(line, sizeof(line), "SALUD %3.0f", health);
    drawText(fb, 14, H - 23, line, healthColor, 2);
    std::snprintf(line, sizeof(line), "PLASMA %2d", ammo);
    drawText(fb, W - textWidth(line, 2) - 14, H - 23, line, ammo > 0 ? HUD_PINK : 0xFFFF5040, 2);

    // Mapa chico (arriba a la derecha): muros, lava, objetos, demonios y el jugador.
    const int cellPx = 3, mx = W - HELL_SIZE * cellPx - 12, my = 24;
    darkenRect(fb, mx - 2, my - 2, HELL_SIZE * cellPx + 4, HELL_SIZE * cellPx + 4, 0.55f);
    for (int y = 0; y < HELL_SIZE; ++y)
        for (int x = 0; x < HELL_SIZE; ++x) {
            char ch = map[y][x];
            if (ch == '#' || ch == 'R') fillRect(fb, mx + x * cellPx, my + y * cellPx, cellPx, cellPx, ch == 'R' ? 0xFFB04890 : 0xFF6A4A50);
            else if (ch == '~') fillRect(fb, mx + x * cellPx, my + y * cellPx, cellPx, cellPx, 0xFFE07020);
        }
    for (const Pickup& p : pickups)
        if (p.respawn <= 0.0f) fillRect(fb, mx + p.x * cellPx + 1, my + p.y * cellPx + 1, 1, 1, p.kind == 'H' ? 0xFF60E880 : HUD_GOLD);
    for (const Demon& d : demons)
        if (d.dying <= 0.0f) fillRect(fb, mx + (int)(d.pos.x * cellPx) - 1, my + (int)(d.pos.z * cellPx) - 1, 2, 2, 0xFFFF3030);
    int ppx = mx + (int)(player.x * cellPx), ppy = my + (int)(player.z * cellPx);
    fillRect(fb, ppx - 1, ppy - 1, 3, 3, HUD_CYAN);
    Vec3 f = forward();
    fillRect(fb, ppx + (int)std::lround(f.x * 3.0f), ppy + (int)std::lround(f.z * 3.0f), 1, 1, HUD_WHITE);

    if (phase == Phase::Ready) {
        centered(fb, H / 2 - 50, "OLEADA 1", HUD_GOLD, 4);
        centered(fb, H / 2 - 12, "PREPARATE", HUD_WHITE, 2);
    }
    if (phase == Phase::WaveClear) {
        std::snprintf(line, sizeof(line), "OLEADA %d SUPERADA", wave);
        centered(fb, H / 2 - 50, line, HUD_GOLD, 3);
        centered(fb, H / 2 - 20, "+SALUD  +PLASMA", HUD_WHITE, 2);
    }
    if (phase == Phase::Playing && ammo == 0) centered(fb, H / 2 + 20, "SIN PLASMA: BUSCA CARGAS AMARILLAS", HUD_GOLD);
    if (phase == Phase::GameOver) {
        centered(fb, H / 2 - 40, "TE ATRAPARON", 0xFFFF6060, 5);
        centered(fb, H / 2 + 4, "ENTER: JUGAR DE NUEVO", HUD_WHITE, 2);
    }
    if (showHelp) {
        static const char* help[] = {"W / S  CAMINAR   A / D  GIRAR", "Q / E  PASO DE LADO", "ESPACIO  DISPARAR",
                                     "ENTER  REINICIAR", "U  DESPEGAR AL ESPACIO", "H  OCULTAR AYUDA"};
        darkenRect(fb, 8, 58, 200, 74, 0.6f);
        for (int i = 0; i < 6; ++i) drawText(fb, 14, 63 + i * 11, help[i], HUD_WHITE);
    } else {
        drawText(fb, 14, 58, "H: AYUDA   U: DESPEGAR", HUD_GREY);
    }
}
