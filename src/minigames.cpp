#include "minigames.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "font.h"
#include "geometry.h"
#include "hud.h"
#include "mesh_builder.h"
#include "shaders.h"

// La fisica del balon avanza en pasos fijos, igual que la del carro.
const float GAME_STEP = 1.0f / 120.0f;

const float BALL_RADIUS = 2.2f;
const float CAR_HIT_RADIUS = 1.5f; // esfera del carro para golpear el balon
const float BALL_BOUNCE = 0.55f;   // rebote contra el suelo
const float BALL_ROLL_FRICTION = 0.35f; // perdida de velocidad rodando (por segundo)
const float BALL_MAX_SPEED = 40.0f;
const float HIT_BOUNCE = 0.6f;     // rebote contra el carro
const float HIT_KICK = 3.0f;       // empujon extra al golpearlo (como el "flip" de Rocket League)
const float GOAL_CELEBRATION = 3.0f;

const int RACE_GATES = 14;
const float GATE_RADIUS = 2.6f; // radio del hueco del anillo

const uint32_t HUD_WHITE = 0xFFE8ECF4, HUD_GREY = 0xFF98A2B8, HUD_BLUE = 0xFF5AA8FF, HUD_ORANGE = 0xFFFFA040,
               HUD_GOLD = 0xFFFFD860, HUD_GREEN = 0xFF60E880;

// Los 12 vertices del icosaedro: centros de los pentagonos negros del balon
// (un icosaedro truncado, como el balon de futbol clasico).
static const Vec3* ballPatches() {
    static Vec3 dirs[12];
    static bool ready = false;
    if (!ready) {
        const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
        const Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                               {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
        for (int i = 0; i < 12; ++i) dirs[i] = normalize(base[i]);
        ready = true;
    }
    return dirs;
}

// Balon: blanco con 12 manchas negras alrededor de los vertices del
// icosaedro. Se lee con la posicion local, asi las manchas giran con el
// balon cuando rueda. Luz de la estrella con bastante luz ambiente para que
// se vea tambien en el lado de noche de la cancha.
static Vec3 ballShader(const Fragment& f, const Uniforms& u) {
    Vec3 dir = normalize(f.localPos);
    float best = 0.0f;
    const Vec3* patches = ballPatches();
    for (int i = 0; i < 12; ++i) best = std::max(best, dot(dir, patches[i]));
    Vec3 albedo = best > 0.95f ? Vec3{0.06f, 0.06f, 0.08f} : Vec3{0.95f, 0.95f, 0.97f};

    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float diffuse = std::max(0.0f, dot(f.normal, toLight));
    Vec3 half = normalize(toLight + toCamera);
    float spec = std::pow(std::max(0.0f, dot(f.normal, half)), 40.0f) * 0.4f;
    float edge = 1.0f - std::max(0.0f, dot(f.normal, toCamera));
    return albedo * (0.38f + 0.7f * diffuse) + Vec3{spec, spec, spec} + Vec3{0.4f, 0.6f, 1.0f} * (edge * edge * 0.25f);
}

// Rota los ejes (x, y) del balon alrededor de un eje unitario y los vuelve
// a dejar perpendiculares.
static void rotateBasis(Vec3& x, Vec3& y, const Vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    auto rot = [&](const Vec3& v) { return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c)); };
    x = normalize(rot(x));
    y = rot(y);
    y = normalize(y - x * dot(x, y));
}

void SoccerGame::build(float radius) {
    planetRadius = radius;
    ballMesh = makeIcosphere(3, Material{"Balon", {1, 1, 1}});

    // Porterias: un circulo de postes en cada polo unidos arriba por un
    // travesano. Los postes son blancos y la parte de arriba brilla con el
    // color del equipo.
    MeshBuilder b;
    int post = b.material("Poste", {0.85f, 0.88f, 0.95f}, 0.6f, 32.0f);
    int blue = b.material("Porteria azul", {0.3f, 0.6f, 1.0f}, 0.3f, 16.0f, 1.0f);
    int orange = b.material("Porteria naranja", {1.0f, 0.55f, 0.15f}, 0.3f, 16.0f, 1.0f);
    const int posts = 12;
    const float postHeight = 4.2f;
    for (int pole = 0; pole < 2; ++pole) {
        float sign = pole == 0 ? 1.0f : -1.0f;
        int glow = pole == 0 ? blue : orange;
        std::vector<Vec3> tops;
        for (int i = 0; i < posts; ++i) {
            float phi = 2.0f * PI * i / posts;
            float a = ARENA_GOAL_ANGLE;
            Vec3 dir{std::sin(a) * std::cos(phi), sign * std::cos(a), std::sin(a) * std::sin(phi)};
            b.frustum(6, 0.3f, 0.22f, -0.3f, postHeight, post, basisFromY(dir, dir * radius));
            Vec3 top = dir * (radius + postHeight);
            b.blob(1, top, {0.45f, 0.45f, 0.45f}, 0.0f, 1, glow);
            tops.push_back(top);
        }
        for (int i = 0; i < posts; ++i) {
            Vec3 p = tops[i], q = tops[(i + 1) % posts];
            b.frustum(4, 0.14f, 0.14f, 0.0f, length(q - p), glow, basisFromY(q - p, p));
        }
    }
    goalsMesh = b.mesh;
    resetBall(Vec3{1, 0, 0});
}

void SoccerGame::resetBall(const Vec3& carRel) {
    // Saque en el ecuador, en la direccion del carro (normalmente el carro
    // acaba de anotar en un polo, asi queda lejos y de frente al balon).
    Vec3 flat{carRel.x, 0.0f, carRel.z};
    Vec3 dir = length(flat) > 1e-3f ? normalize(flat) : Vec3{1, 0, 0};
    ballPos = dir * (planetRadius + BALL_RADIUS + 3.0f); // cae desde un poco arriba
    ballVel = Vec3{0, 0, 0};
}

void SoccerGame::step(float h, const Vec3& carPos, const Vec3& carVel, bool carIn) {
    // Gravedad del planeta: siempre hacia el centro.
    float d = length(ballPos);
    Vec3 n = ballPos * (1.0f / d);
    ballVel -= n * (10.0f * h);
    ballPos += ballVel * h;

    // Suelo: si el balon se hundio, se saca a la superficie y rebota.
    d = length(ballPos);
    n = ballPos * (1.0f / d);
    float floorDist = planetRadius + BALL_RADIUS;
    if (d < floorDist) {
        ballPos = n * floorDist;
        float vn = dot(ballVel, n);
        if (vn < 0.0f) ballVel -= n * (vn * (1.0f + BALL_BOUNCE));
        // Friccion al rodar: solo frena la parte horizontal.
        Vec3 tangent = ballVel - n * dot(ballVel, n);
        ballVel -= tangent * (1.0f - std::exp(-BALL_ROLL_FRICTION * h));
    }

    // Golpe del carro: esfera contra esfera. El carro es mucho mas pesado,
    // asi que solo cambia el balon: rebota con la velocidad relativa y se
    // le da un empujon extra hacia afuera del carro.
    if (carIn) {
        Vec3 dc = ballPos - carPos;
        float dist = length(dc);
        float minDist = BALL_RADIUS + CAR_HIT_RADIUS;
        if (dist < minDist && dist > 1e-4f) {
            Vec3 m = dc * (1.0f / dist);
            ballPos = carPos + m * minDist;
            float rel = dot(ballVel - carVel, m);
            if (rel < 0.0f) {
                ballVel -= m * (rel * (1.0f + HIT_BOUNCE));
                ballVel += m * HIT_KICK;
                touches++;
            }
        }
    }
    float speed = length(ballVel);
    if (speed > BALL_MAX_SPEED) ballVel = ballVel * (BALL_MAX_SPEED / speed);

    // Rodar: gira alrededor del eje perpendicular al suelo y al movimiento,
    // un angulo igual a lo avanzado entre el radio (sin patinar).
    n = normalize(ballPos);
    Vec3 tangent = ballVel - n * dot(ballVel, n);
    float rolled = length(tangent) * h / BALL_RADIUS;
    if (rolled > 1e-6f) rotateBasis(ballX, ballY, normalize(cross(n, tangent)), rolled);
}

void SoccerGame::update(float dt, const Ship& ship, const CelestialBody& planet, float time, Particles& particles) {
    if (dt <= 0.0f) return;
    Vec3 center = planet.position(time);
    Vec3 car = ship.position - center;
    // Velocidad del carro respecto al planeta, a partir de cuanto se movio.
    // Un salto grande (reaparecer, llegar por el agujero) no cuenta.
    Vec3 carVel = hasLastCar && length(car - lastCar) < 5.0f ? (car - lastCar) * (1.0f / dt) : Vec3{0, 0, 0};
    lastCar = car;
    hasLastCar = true;
    bool carIn = !ship.crashed && length(car) < planetRadius + 6.0f;

    accumulator += dt;
    while (accumulator >= GAME_STEP) {
        step(GAME_STEP, car, carVel, carIn);
        accumulator -= GAME_STEP;
    }

    if (goalTimer > 0.0f) {
        goalTimer -= dt;
        if (goalTimer <= 0.0f) resetBall(car);
        return;
    }
    // Gol: el balon entro al circulo de una porteria.
    float y = normalize(ballPos).y;
    if (std::fabs(y) > std::cos(ARENA_GOAL_ANGLE)) {
        bool north = y > 0.0f;
        lastGoalOwn = north; // la azul (norte) es la propia
        if (north) orangeScore++;
        else blueScore++;
        goalTimer = GOAL_CELEBRATION;
        Vec3 where = center + Vec3{0.0f, north ? 1.0f : -1.0f, 0.0f} * (planetRadius + 2.0f);
        particles.burst(where, planet.velocity(time), 60, 0.0f, 3.0f, 25.0f, 0.3f, 0.6f);
        particles.burst(where, planet.velocity(time), 200, 4.0f, 16.0f, 6.0f, 0.8f, 1.8f);
        std::printf("%s  azul %d - %d naranja\n", north ? "Autogol" : "Gol", blueScore, orangeScore);
    }
}

void SoccerGame::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& planet) const {
    Vec3 center = planet.position(u.time);
    if (!sphereInFrustum(u, center, planetRadius + 6.0f)) return;
    // Solo de cerca: de lejos el balon y los postes ni se distinguirian.
    if (projectedRadius(u, center, planetRadius, fb.height()) < 60.0f) return;
    u.objectCenter = center;
    u.model = translate(center);
    drawMesh(fb, goalsMesh, u, propShader, stats);
    stats.propsDrawn += 24;

    Vec3 world = center + ballPos;
    if (sphereInFrustum(u, world, BALL_RADIUS)) {
        u.model = fromBasis(ballX, ballY, cross(ballX, ballY), world) * scale(BALL_RADIUS);
        drawMesh(fb, ballMesh, u, ballShader, stats);
        stats.objectsDrawn++;
    }
}

// Panel centrado arriba de la pantalla con varias lineas de texto.
static void centeredPanel(Framebuffer& fb, int y, int width, int height) {
    darkenRect(fb, (fb.width() - width) / 2, y, width, height, 0.55f);
}

static void centeredText(Framebuffer& fb, int y, const char* text, uint32_t color, int scale = 1) {
    drawText(fb, (fb.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

void SoccerGame::drawHud(Framebuffer& fb, bool landed) const {
    centeredPanel(fb, 8, 330, 52);
    centeredText(fb, 13, "ARENA - FUTBOL", HUD_GREY);
    // Marcador: cada parte con el color de su equipo.
    char blue[32], orange[32];
    std::snprintf(blue, sizeof(blue), "AZUL %d", blueScore);
    std::snprintf(orange, sizeof(orange), "%d NARANJA", orangeScore);
    int wBlue = textWidth(blue, 2), wDash = textWidth(" - ", 2), wOrange = textWidth(orange, 2);
    int x = (fb.width() - (wBlue + wDash + wOrange)) / 2;
    drawText(fb, x, 24, blue, HUD_BLUE, 2);
    drawText(fb, x + wBlue, 24, " - ", HUD_WHITE, 2);
    drawText(fb, x + wBlue + wDash, 24, orange, HUD_ORANGE, 2);
    centeredText(fb, 46, landed ? "METE EL BALON EN LA PORTERIA NARANJA (POLO SUR)" : "BAJA A LA SUPERFICIE PARA JUGAR",
                 landed ? HUD_WHITE : HUD_GREY);

    if (goalTimer > 0.0f) {
        const char* msg = lastGoalOwn ? "AUTOGOL" : "GOL!";
        centeredText(fb, fb.height() / 2 - 60, msg, lastGoalOwn ? 0xFFFF6060 : HUD_ORANGE, 5);
    }
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
