#include "arcade.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
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
            particles.burst(world(g.m.x, g.m.y, 0.4f), Vec3{0, 0, 0}, 30, 1.0f, 4.0f, 1.5f, 0.2f, 0.5f);
        } else {
            phase = Phase::Dying;
            phaseTimer = 1.6f;
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
                if (e.type == 2 && e.hits < 2) break; // el jefe aguanta un disparo
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
