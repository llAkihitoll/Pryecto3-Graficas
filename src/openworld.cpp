#include "openworld.h"
#include <algorithm>
#include <cstdio>
#include <random>
#include "boost_flame.h"
#include "font.h"
#include "geometry.h"
#include "hud.h"
#include "mesh_builder.h"
#include "parallel.h"
#include "props.h"
#include "rlball.h"
#include "shaders.h"

// --- Medidas (en unidades del mundo; el carro mide ~1.4 de largo) ---
// Proporciones parecidas a la cancha de Rocket League.
const float FIELD_W = 40.0f;   // media anchura de la cancha (eje X)
const float FIELD_L = 52.0f;   // media longitud (eje Z): las porterias estan en z = +-52
const float CORNER = 13.0f;    // esquinas cortadas a 45 grados
const float GOAL_W = 7.5f;     // media anchura de la porteria
const float GOAL_H = 6.5f;     // altura del travesano
const float GOAL_D = 8.0f;     // fondo de la porteria
const float WALL_H = 9.0f;     // altura visible de las paredes
const float ENTRANCE = 5.0f;   // media anchura de las puertas laterales
const float CEILING = 24.0f;   // techo invisible para el balon
const float WORLD_LIMIT = 290.0f;
const float WORLD_HALF = 300.0f;

const float GRAVITY = 9.0f;
const float BALL_R = 1.15f;
const float CAR_WALL_R = 0.75f;  // circulo del carro contra las paredes
const float CAR_HIT_R = 0.85f;   // esfera del carro para pegarle al balon
const float CAR_HIT_UP = 0.3f;   // altura del centro de esa esfera sobre las ruedas
const float WHEEL_OFFSET = 0.258f; // del centro del modelo a la base de las ruedas
const float FLIP_TIME = 0.6f;
const float STEP = 1.0f / 120.0f;
// Saque en diagonal, como en Rocket League: cada carro en una esquina del
// circulo central, mirando al balon.
const Vec3 KICKOFF_BLUE{-14.0f, 0.0f, -26.0f};
const Vec3 SPAWN{70.0f, 0.0f, 0.0f}; // afuera, frente a la puerta del lado +X

// Cielo de fantasia del universo alterno: violeta arriba y lila rosado en el
// horizonte (tambien es el color de la niebla).
static const Vec3 SKY_TOP{0.36f, 0.24f, 0.72f};
static const Vec3 SKY_HORIZON{0.86f, 0.74f, 0.93f};
static const Vec3 WORLD_UP{0, 1, 0};

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static float approach(float rate, float h) { return 1.0f - std::exp(-rate * h); }
static Vec3 flat(const Vec3& v) { return Vec3{v.x, 0.0f, v.z}; }

// Formula de Rodrigues: gira v alrededor de un eje unitario.
static Vec3 rotateAround(const Vec3& v, const Vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

// Dentro de la cancha (octagono) o de una porteria.
static bool inField(float x, float z) {
    float ax = std::fabs(x), az = std::fabs(z);
    if (ax <= FIELD_W && az <= FIELD_L && ax + az <= FIELD_W + FIELD_L - CORNER) return true;
    return ax <= GOAL_W && az <= FIELD_L + GOAL_D;
}

// Explanada de asfalto alrededor del estadio (incluye las gradas).
static bool inPlaza(float x, float z) { return std::fabs(x) < FIELD_W + 22.0f && std::fabs(z) < FIELD_L + 28.0f; }

// ---------------------------------------------------------------- shaders

// Direccion hacia Albor. Esta tan lejos que es la misma para todo el mundo:
// se guarda una vez por cuadro en vez de normalizar en cada pixel.
static Vec3 sunDir{0, 1, 0};

// Niebla: lo lejano se mezcla con el color del horizonte, asi las montanas
// del borde se pierden en el cielo. d / (d + 200) crece parecido a
// 1 - exp(-d / 200) pero sin calcular una exponencial por pixel.
static Vec3 applyFog(const Vec3& c, const Fragment& f, const Uniforms& u) {
    float d = length(f.worldPos - u.cameraPos);
    return lerp(c, SKY_HORIZON, d / (d + 200.0f) * 0.9f);
}

// Cesped de la cancha: franjas, un tono del color de cada equipo hacia su
// porteria y las lineas blancas (central, circulo central y areas).
static Vec3 fieldColor(float x, float z) {
    if (std::fabs(z) > FIELD_L) return Vec3{0.12f, 0.14f, 0.16f}; // dentro de la porteria
    int stripe = (int)std::floor((z + 200.0f) / 8.0f);
    Vec3 grass = stripe % 2 ? Vec3{0.16f, 0.45f, 0.2f} : Vec3{0.19f, 0.52f, 0.23f};
    Vec3 team = z < 0.0f ? Vec3{0.2f, 0.45f, 1.0f} : Vec3{1.0f, 0.55f, 0.15f};
    grass = lerp(grass, team, 0.06f + 0.16f * smoothstep(25.0f, FIELD_L, std::fabs(z)));

    const float w = 0.18f; // media anchura de las lineas
    float az = std::fabs(z), ax = std::fabs(x);
    bool line = az < w;                                                  // linea central
    line = line || std::fabs(std::sqrt(x * x + z * z) - 9.0f) < w;      // circulo central
    line = line || (x * x + z * z < 0.5f);                              // punto central
    float boxW = GOAL_W + 6.0f, boxZ = FIELD_L - 8.0f;
    line = line || (std::fabs(ax - boxW) < w && az > boxZ);              // lados del area
    line = line || (std::fabs(az - boxZ) < w && ax < boxW);              // frente del area
    line = line || (std::fabs(az - FIELD_L) < w && ax < GOAL_W);         // linea de gol
    return line ? Vec3{0.92f, 0.94f, 0.92f} : grass;
}

// Terreno: la cancha, el asfalto alrededor del estadio, y afuera pasto que
// se vuelve roca en las pendientes y nieve en lo alto de las montanas.
static Vec3 groundShader(const Fragment& f, const Uniforms& u) {
    const Vec3& p = f.worldPos;
    Vec3 n = f.normal;
    Vec3 albedo;
    if (p.y < 0.2f && inField(p.x, p.z)) {
        albedo = fieldColor(p.x, p.z);
    } else if (inPlaza(p.x, p.z)) {
        bool seam = std::fabs(std::fmod(std::fabs(p.x) + 1000.0f, 6.0f)) < 0.12f ||
                    std::fabs(std::fmod(std::fabs(p.z) + 1000.0f, 6.0f)) < 0.12f;
        albedo = seam ? Vec3{0.22f, 0.22f, 0.24f} : Vec3{0.34f, 0.34f, 0.36f};
    } else {
        float var = 0.85f + 0.15f * std::sin(p.x * 0.31f + std::sin(p.z * 0.21f) * 2.0f) * std::sin(p.z * 0.27f);
        Vec3 grass = Vec3{0.27f, 0.52f, 0.2f} * var;
        Vec3 rock{0.46f, 0.43f, 0.4f};
        albedo = lerp(grass, rock, smoothstep(0.86f, 0.7f, n.y));
        albedo = lerp(albedo, Vec3{0.93f, 0.95f, 0.98f}, smoothstep(34.0f, 42.0f, p.y));
    }
    float diffuse = std::max(0.0f, dot(n, sunDir));
    return applyFog(albedo * (0.38f + 0.72f * diffuse), f, u);
}

// Estadio, arboles, rocas y pads: Lambert, brillo, luz propia y niebla.
static Vec3 worldShader(const Fragment& f, const Uniforms& u) {
    const Material& m = *f.material;
    const Vec3& toLight = sunDir;
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    Vec3 n = f.normal;
    if (dot(n, toCamera) < 0) n = -n;
    float diffuse = std::max(0.0f, dot(n, toLight));
    Vec3 half = normalize(toLight + toCamera);
    float spec = m.specular * std::pow(std::max(0.0f, dot(n, half)), m.shininess);
    Vec3 lit = m.color * (0.35f + 0.7f * diffuse) + Vec3{spec, spec, spec};
    return applyFog(lerp(lit, m.color * 1.2f, m.emissive), f, u);
}

// Balon de Rocket League: la textura se lee con la posicion local, asi gira
// con el balon cuando rueda.
static Vec3 ballShader(const Fragment& f, const Uniforms& u) {
    return shadeRocketBall(*u.textures, normalize(f.localPos), f.normal, sunDir, normalize(u.cameraPos - f.worldPos),
                           0.4f);
}

// Sombra suave: un disco que se dibuja con mezcla aditiva y color negativo,
// asi oscurece el suelo (mas en el centro, nada en el borde).
static Vec3 shadowShader(const Fragment& f, const Uniforms&) {
    float r2 = f.localPos.x * f.localPos.x + f.localPos.z * f.localPos.z;
    float k = clampf(1.0f - r2, 0.0f, 1.0f);
    return Vec3{-0.3f, -0.3f, -0.3f} * k;
}

// ---------------------------------------------------------------- carro

void GroundCar::axes(Vec3& f, Vec3& u, Vec3& r) const {
    u = up;
    f = normalize(flatForward() - u * dot(flatForward(), u)); // el frente sobre el terreno
    r = normalize(cross(f, u));
    if (nose != 0.0f) { // nariz arriba o abajo (en el aire)
        Vec3 f2 = f * std::cos(nose) + u * std::sin(nose);
        u = u * std::cos(nose) - f * std::sin(nose);
        f = f2;
    }
    if (flipTime >= 0.0f) { // vuelta completa hacia donde va el flip
        float angle = -2.0f * PI * std::min(1.0f, flipTime / FLIP_TIME);
        Vec3 axis = normalize(cross(flipDir, WORLD_UP));
        f = rotateAround(f, axis, angle);
        u = rotateAround(u, axis, angle);
    }
    r = normalize(cross(f, u));
}

Mat4 GroundCar::modelMatrix() const {
    Vec3 f, u, r;
    axes(f, u, r);
    // El modelo mira a -Z y su centro esta a WHEEL_OFFSET sobre las ruedas.
    return fromBasis(r, u, -f, position + up * WHEEL_OFFSET);
}

// ---------------------------------------------------------------- mundo

float OpenWorld::groundHeight(float x, float z) const {
    // Plano alrededor del estadio; afuera colinas que aparecen poco a poco y
    // montanas que cierran el borde del mundo.
    float dx = std::max(0.0f, std::fabs(x) - (FIELD_W + 22.0f));
    float dz = std::max(0.0f, std::fabs(z) - (FIELD_L + 28.0f));
    float away = std::sqrt(dx * dx + dz * dz);
    float hills = (0.5f + 0.5f * noise.fbm(Vec3{x * 0.014f, 0.0f, z * 0.014f}, 4)) * 18.0f;
    float edge = std::max(std::fabs(x), std::fabs(z));
    float mountains = smoothstep(200.0f, 295.0f, edge) * (45.0f + 20.0f * noise.perlin(Vec3{x * 0.02f, 3.0f, z * 0.02f}));
    return smoothstep(0.0f, 70.0f, away) * hills + mountains;
}

void OpenWorld::addBlock(float x0, float x1, float z0, float z1) {
    walls.push_back(Wall{{x0, z0}, {x1, z0}});
    walls.push_back(Wall{{x1, z0}, {x1, z1}});
    walls.push_back(Wall{{x1, z1}, {x0, z1}});
    walls.push_back(Wall{{x0, z1}, {x0, z0}});
}

// Caja (prisma) que va sobre el segmento a-b del piso, de y0 a y1, con
// grosor hacia afuera de "inside" (la cara interior queda sobre el segmento).
static void wallBox(MeshBuilder& b, Vec2 a, Vec2 c, float y0, float y1, float thickness, Vec2 inside, int mat) {
    Vec2 d{c.x - a.x, c.y - a.y};
    Vec2 n{-d.y, d.x};
    float len = std::sqrt(n.x * n.x + n.y * n.y);
    n = Vec2{n.x / len, n.y / len};
    Vec2 mid{(a.x + c.x) * 0.5f - inside.x, (a.y + c.y) * 0.5f - inside.y};
    if (n.x * mid.x + n.y * mid.y < 0) n = Vec2{-n.x, -n.y}; // hacia afuera
    Vec3 p[8];
    Vec2 q[4] = {a, c, {c.x + n.x * thickness, c.y + n.y * thickness}, {a.x + n.x * thickness, a.y + n.y * thickness}};
    for (int i = 0; i < 4; ++i) {
        p[i] = Vec3{q[i].x, y0, q[i].y};
        p[i + 4] = Vec3{q[i].x, y1, q[i].y};
    }
    Vec3 center = (p[0] + p[1] + p[2] + p[3] + p[4] + p[5] + p[6] + p[7]) * 0.125f;
    const int faces[6][4] = {{0, 1, 2, 3}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
    for (const auto& fc : faces) {
        b.triangle(p[fc[0]], p[fc[1]], p[fc[2]], mat, center);
        b.triangle(p[fc[0]], p[fc[2]], p[fc[3]], mat, center);
    }
}

// Caja alineada con los ejes.
static void box(MeshBuilder& b, Vec3 lo, Vec3 hi, int mat) {
    wallBox(b, Vec2{lo.x, lo.z}, Vec2{hi.x, lo.z}, lo.y, hi.y, hi.z - lo.z, Vec2{(lo.x + hi.x) * 0.5f, lo.z - 1.0f}, mat);
}

// Junta copias de modelos (ya giradas, escaladas y colocadas) en una malla.
static void appendModel(Mesh& out, const Mesh& model, int materialBase, const Mat4& xf) {
    int base = (int)out.vertices.size();
    for (const Vertex& v : model.vertices) {
        Vertex w = v;
        w.position = xf.transformPoint(v.position);
        w.normal = normalize(xf.transformDir(v.normal));
        out.vertices.push_back(w);
    }
    for (const Triangle& t : model.triangles)
        out.triangles.push_back(Triangle{t.a + base, t.b + base, t.c + base, t.material + materialBase});
}

void OpenWorld::build(unsigned seed) {
    noise = Noise(seed);
    sunDirection = normalize(Vec3{0.45f, 0.75f, 0.35f});

    // --- Terreno: rejilla de 150 x 150 celdas, partida en 5 x 5 trozos para
    // poder descartar los que no se ven.
    const int cells = 150, perChunk = 30;
    const float cell = 2.0f * WORLD_HALF / cells;
    auto vertexAt = [&](int i, int j) {
        float x = -WORLD_HALF + i * cell, z = -WORLD_HALF + j * cell;
        float h = groundHeight(x, z);
        // Normal por diferencias finitas de la altura.
        Vec3 n = normalize(Vec3{groundHeight(x - 1.0f, z) - groundHeight(x + 1.0f, z), 2.0f,
                                groundHeight(x, z - 1.0f) - groundHeight(x, z + 1.0f)});
        return Vertex{Vec3{x, h, z}, n};
    };
    terrain.clear();
    chunkCenters.clear();
    chunkRadii.clear();
    for (int cj = 0; cj < cells / perChunk; ++cj)
        for (int ci = 0; ci < cells / perChunk; ++ci) {
            Mesh m;
            m.materials.push_back(Material{"Terreno", {0.3f, 0.5f, 0.2f}});
            float lo = 1e9f, hi = -1e9f;
            for (int j = 0; j <= perChunk; ++j)
                for (int i = 0; i <= perChunk; ++i) {
                    m.vertices.push_back(vertexAt(ci * perChunk + i, cj * perChunk + j));
                    lo = std::min(lo, m.vertices.back().position.y);
                    hi = std::max(hi, m.vertices.back().position.y);
                }
            const int row = perChunk + 1;
            for (int j = 0; j < perChunk; ++j)
                for (int i = 0; i < perChunk; ++i) {
                    int a = j * row + i, b = a + 1, c = a + row, d = c + 1;
                    // Orden antihorario visto desde arriba (la cara mira a +Y).
                    m.triangles.push_back(Triangle{a, c, b, 0});
                    m.triangles.push_back(Triangle{b, c, d, 0});
                }
            float half = perChunk * cell * 0.5f;
            Vec3 center{-WORLD_HALF + (ci * perChunk) * cell + half, (lo + hi) * 0.5f,
                        -WORLD_HALF + (cj * perChunk) * cell + half};
            chunkCenters.push_back(center);
            chunkRadii.push_back(std::sqrt(2.0f * half * half + (hi - lo) * (hi - lo) * 0.25f));
            terrain.push_back(std::move(m));
        }

    // --- Paredes (vistas desde arriba). Octagono con las porterias y las
    // puertas laterales abiertas.
    walls.clear();
    const float W = FIELD_W, L = FIELD_L, C = CORNER, G = GOAL_W, E = ENTRANCE;
    for (int s = -1; s <= 1; s += 2) {
        float z = s * L, x = s * W;
        walls.push_back(Wall{{-W + C, z}, {-G, z}});           // linea de fondo, a los lados de la porteria
        walls.push_back(Wall{{G, z}, {W - C, z}});
        walls.push_back(Wall{{-G, z}, {G, z}, true, GOAL_H});  // arriba del travesano (solo el balon)
        walls.push_back(Wall{{-G, z}, {-G, z + s * GOAL_D}});  // porteria
        walls.push_back(Wall{{G, z}, {G, z + s * GOAL_D}});
        walls.push_back(Wall{{-G, z + s * GOAL_D}, {G, z + s * GOAL_D}});
        walls.push_back(Wall{{x, L - C}, {x, E}});             // pared lateral con su puerta
        walls.push_back(Wall{{x, -E}, {x, -L + C}});
        walls.push_back(Wall{{x, -E}, {x, E}, true});          // la puerta: solo el balon no pasa
        walls.push_back(Wall{{x, L - C}, {s * (W - C), L}});   // esquinas
        walls.push_back(Wall{{x, -L + C}, {s * (W - C), -L}});
    }

    MeshBuilder b;
    int wallMat = b.material("Pared", {0.17f, 0.2f, 0.27f}, 0.4f, 24.0f);
    int blueGlow = b.material("Azul", {0.25f, 0.55f, 1.0f}, 0.3f, 16.0f, 1.0f);
    int orangeGlow = b.material("Naranja", {1.0f, 0.55f, 0.15f}, 0.3f, 16.0f, 1.0f);
    int blueNet = b.material("Red azul", {0.2f, 0.35f, 0.6f}, 0.1f, 8.0f, 0.35f);
    int orangeNet = b.material("Red naranja", {0.6f, 0.35f, 0.15f}, 0.1f, 8.0f, 0.35f);
    int post = b.material("Poste", {0.9f, 0.92f, 0.95f}, 0.7f, 48.0f);
    int seats = b.material("Gradas", {0.3f, 0.32f, 0.38f}, 0.2f, 16.0f);
    int seatsBlue = b.material("Gradas azules", {0.18f, 0.3f, 0.55f}, 0.2f, 16.0f);
    int seatsOrange = b.material("Gradas naranjas", {0.6f, 0.33f, 0.14f}, 0.2f, 16.0f);
    int metal = b.material("Metal", {0.55f, 0.57f, 0.6f}, 0.6f, 40.0f);
    int lamp = b.material("Lampara", {1.0f, 0.98f, 0.9f}, 0.0f, 1.0f, 1.0f);

    const Vec2 middle{0.0f, 0.0f};
    for (const Wall& w : walls) {
        if (w.ballOnly) continue;
        bool goalPart = std::fabs(w.a.y) > L + 0.01f || std::fabs(w.b.y) > L + 0.01f;
        float zMid = (w.a.y + w.b.y) * 0.5f;
        if (goalPart) { // paredes de la porteria: red del color del equipo
            Vec2 inside{0.0f, zMid > 0 ? L + GOAL_D * 0.5f : -L - GOAL_D * 0.5f};
            wallBox(b, w.a, w.b, 0.0f, GOAL_H, 0.3f, inside, zMid > 0 ? orangeNet : blueNet);
            continue;
        }
        int glow = zMid > 0.0f ? orangeGlow : blueGlow;
        wallBox(b, w.a, w.b, 0.0f, WALL_H, 1.2f, middle, wallMat);
        wallBox(b, w.a, w.b, WALL_H, WALL_H + 0.35f, 1.2f, middle, glow); // borde de arriba que brilla
        wallBox(b, w.a, w.b, 0.0f, 0.25f, 0.05f, Vec2{w.a.x * 2.0f, w.a.y * 2.0f}, glow); // franja al pie (por dentro)
    }
    for (int s = -1; s <= 1; s += 2) {
        float z = s * L;
        int glow = s > 0 ? orangeGlow : blueGlow;
        // Pared encima de la boca de la porteria y encima de las puertas.
        wallBox(b, Vec2{-G, z}, Vec2{G, z}, GOAL_H, WALL_H, 1.2f, middle, wallMat);
        wallBox(b, Vec2{-G, z}, Vec2{G, z}, WALL_H, WALL_H + 0.35f, 1.2f, middle, glow);
        for (int side = -1; side <= 1; side += 2) {
            float x = side * W;
            wallBox(b, Vec2{x, -E}, Vec2{x, E}, 4.0f, WALL_H, 1.2f, middle, wallMat);
            wallBox(b, Vec2{x, -E}, Vec2{x, 0.0f}, WALL_H, WALL_H + 0.35f, 1.2f, middle, blueGlow);
            wallBox(b, Vec2{x, 0.0f}, Vec2{x, E}, WALL_H, WALL_H + 0.35f, 1.2f, middle, orangeGlow);
            // Postes de la porteria.
            b.frustum(8, 0.3f, 0.3f, 0.0f, GOAL_H, post, translate(Vec3{side * G, 0.0f, z}));
        }
        // Travesano y techo de la porteria.
        b.frustum(8, 0.3f, 0.3f, -G, G, post, translate(Vec3{0.0f, GOAL_H, z}) * rotateZ(PI * 0.5f));
        float zBack = z + s * GOAL_D;
        b.slab({{-G, z}, {G, z}, {G, zBack}, {-G, zBack}}, GOAL_H, GOAL_H + 0.3f, s > 0 ? orangeNet : blueNet);
    }

    // Gradas: escalones a los lados (con un tunel frente a cada puerta) y
    // detras de las porterias. Tambien son muros para el carro.
    auto stand = [&](float x0, float x1, float z0, float z1, bool alongX, float dirSign, int mat) {
        // Escalones que suben alejandose de la cancha.
        const int steps = 5;
        for (int i = 0; i < steps; ++i) {
            float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
            float h = 1.5f + i * 1.6f;
            Vec3 lo, hi;
            if (alongX) { // se alejan en X
                float a = dirSign > 0 ? x0 + (x1 - x0) * t0 : x1 - (x1 - x0) * t1;
                float c = dirSign > 0 ? x0 + (x1 - x0) * t1 : x1 - (x1 - x0) * t0;
                lo = {a, 0.0f, z0};
                hi = {c, h, z1};
            } else {
                float a = dirSign > 0 ? z0 + (z1 - z0) * t0 : z1 - (z1 - z0) * t1;
                float c = dirSign > 0 ? z0 + (z1 - z0) * t1 : z1 - (z1 - z0) * t0;
                lo = {x0, 0.0f, a};
                hi = {x1, h, c};
            }
            box(b, lo, hi, i % 2 ? seats : mat);
        }
        addBlock(x0, x1, z0, z1);
    };
    for (int side = -1; side <= 1; side += 2) {
        float xa = side > 0 ? W + 1.5f : -W - 14.0f, xb = side > 0 ? W + 14.0f : -W - 1.5f;
        stand(xa, xb, E + 1.0f, 38.0f, true, (float)side, seatsOrange);
        stand(xa, xb, -38.0f, -E - 1.0f, true, (float)side, seatsBlue);
        float za = side > 0 ? L + GOAL_D + 1.5f : -L - GOAL_D - 13.0f, zb = side > 0 ? L + GOAL_D + 13.0f : -L - GOAL_D - 1.5f;
        stand(-28.0f, 28.0f, za, zb, false, (float)side, side > 0 ? seatsOrange : seatsBlue);
    }

    // Torres de luz en las cuatro esquinas.
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            Vec3 base{sx * (W + 16.0f), 0.0f, sz * (L + 16.0f)};
            b.frustum(6, 0.7f, 0.45f, 0.0f, 26.0f, metal, translate(base));
            box(b, base + Vec3{-2.5f, 26.0f, -0.5f}, base + Vec3{2.5f, 29.0f, 0.5f}, metal);
            b.blob(1, base + Vec3{0.0f, 27.5f, -sz * 0.7f}, {2.2f, 1.2f, 0.4f}, 0.0f, 1, lamp);
            walls.push_back(Wall{{base.x - 0.7f, base.z}, {base.x + 0.7f, base.z}});
        }
    stadium = b.mesh;

    // --- Arboles y rocas afuera del estadio (semilla fija).
    const int GRID = 6;
    const float SPAN = 500.0f / GRID; // los objetos estan entre -250 y 250
    scenery.assign(GRID * GRID, Mesh{});
    sceneryCenters.clear();
    for (int j = 0; j < GRID; ++j)
        for (int i = 0; i < GRID; ++i) sceneryCenters.push_back(Vec3{-250.0f + (i + 0.5f) * SPAN, 10.0f, -250.0f + (j + 0.5f) * SPAN});
    Mesh tree = makeAlienTree();
    std::vector<Mesh> rocks;
    for (unsigned i = 0; i < 3; ++i) rocks.push_back(makeRock(100 + i));
    // Todos los trozos comparten la misma lista de materiales.
    std::vector<Material> materials(tree.materials);
    int treeBase = 0;
    std::vector<int> rockBase;
    for (const Mesh& r : rocks) {
        rockBase.push_back((int)materials.size());
        materials.insert(materials.end(), r.materials.begin(), r.materials.end());
    }
    for (Mesh& m : scenery) m.materials = materials;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    int placed = 0;
    for (int tries = 0; tries < 3000 && placed < 320; ++tries) {
        float x = (unit(rng) * 2.0f - 1.0f) * 250.0f, z = (unit(rng) * 2.0f - 1.0f) * 250.0f;
        if (std::fabs(x) < FIELD_W + 32.0f && std::fabs(z) < FIELD_L + 38.0f) continue; // lejos del estadio
        if (std::fabs(z) < 8.0f && x > 0.0f && x < 110.0f) continue; // camino a la puerta
        // Grupos: el ruido decide donde hay bosque.
        float forest = noise.perlin(Vec3{x * 0.02f, 7.0f, z * 0.02f});
        bool isTree = forest > -0.05f;
        if (!isTree && unit(rng) > 0.35f) continue;
        float h = groundHeight(x, z);
        if (h > 34.0f) continue; // no en la nieve
        float scale = isTree ? 1.6f + 1.4f * unit(rng) : 1.5f + 3.0f * unit(rng);
        Mat4 xf = translate(Vec3{x, h - 0.1f, z}) * rotateY(unit(rng) * 2.0f * PI) * ::scale(scale);
        int ci = std::min(GRID - 1, (int)((x + 250.0f) / SPAN)), cj = std::min(GRID - 1, (int)((z + 250.0f) / SPAN));
        Mesh& chunk = scenery[cj * GRID + ci];
        if (isTree) appendModel(chunk, tree, treeBase, xf);
        else {
            int k = (int)(unit(rng) * 3.0f) % 3;
            appendModel(chunk, rocks[k], rockBase[k], xf);
        }
        placed++;
    }

    // --- Balon, sombra y pads de boost.
    ballMesh = makeIcosphere(3, Material{"Balon", {1, 1, 1}});
    bakeRocketBall(ballTextures, 256);
    shadowDisc = Mesh{};
    shadowDisc.materials.push_back(Material{"Sombra"});
    addRing(shadowDisc, 0.0f, 1.0f, 32, 0);
    {
        MeshBuilder p;
        int baseMat = p.material("Base", {0.2f, 0.2f, 0.22f}, 0.3f, 16.0f);
        int ring = p.material("Anillo", {1.0f, 0.6f, 0.15f}, 0.3f, 16.0f, 1.0f);
        int orb = p.material("Esfera", {1.0f, 0.75f, 0.3f}, 0.5f, 32.0f, 1.0f);
        p.frustum(12, 1.3f, 1.15f, 0.0f, 0.12f, baseMat);
        p.frustum(12, 0.95f, 0.95f, 0.12f, 0.16f, ring);
        Mesh empty = p.mesh;
        p.blob(1, Vec3{0.0f, 1.0f, 0.0f}, Vec3{0.45f, 0.45f, 0.45f}, 0.0f, 1, orb);
        padActive = p.mesh;
        empty.materials[ring].emissive = 0.1f;
        empty.materials[ring].color = Vec3{0.35f, 0.25f, 0.15f};
        padEmpty = empty;
    }
    pads.clear();
    for (int sx = -1; sx <= 1; sx += 2) {
        pads.push_back(Pad{Vec3{sx * 34.0f, 0.0f, 0.0f}});
        for (int sz = -1; sz <= 1; sz += 2) pads.push_back(Pad{Vec3{sx * 30.0f, 0.0f, sz * 40.0f}});
    }

    size_t sceneryTris = 0;
    for (const Mesh& m : scenery) sceneryTris += m.triangles.size();
    std::printf("Mundo abierto de Arena: %zu triangulos de terreno, %zu del estadio, %d arboles y rocas (%zu)\n",
                terrain.size() * (size_t)perChunk * perChunk * 2, stadium.triangles.size(), placed, sceneryTris);
    enter();
    restartMatch();
}

bool OpenWorld::playerInStadium() const { return inField(car.position.x, car.position.z); }

void OpenWorld::enter() {
    car = GroundCar{};
    car.position = SPAWN;
    car.position.y = groundHeight(SPAWN.x, SPAWN.z);
    car.yaw = -PI * 0.5f; // mirando a -X, hacia la puerta
    accumulator = 0.0f;
    snapCamera();
}

void OpenWorld::respawn() { enter(); }

void OpenWorld::restartMatch() {
    blue = orange = 0;
    clock = 180.0f;
    overtime = false;
    touches = 0;
    rival = GroundCar{};
    rival.maxDrive = 15.0f;
    rival.maxBoost = 24.0f;
    rival.position = -KICKOFF_BLUE;
    rival.yaw = std::atan2(KICKOFF_BLUE.x, KICKOFF_BLUE.z);
    ballPos = Vec3{0.0f, BALL_R, 0.0f};
    ballVel = Vec3{0, 0, 0};
    for (Pad& p : pads) p.cooldown = 0.0f;
    // Si el carro ya esta adentro se hace un saque; si no, se espera a que entre.
    if (playerInStadium()) kickoff();
    else phase = Phase::Waiting;
}

void OpenWorld::placeAtKickoff() {
    kickoff();
    snapCamera();
}

void OpenWorld::kickoff() {
    ballPos = Vec3{0.0f, BALL_R, 0.0f};
    ballVel = Vec3{0, 0, 0};
    // Girado para que una placa con lente (esquina del cubo) mire al carro
    // azul en el saque: rodeada de un panel gris y dos blancos.
    Vec3 corner = normalize(Vec3{-1.0f, 1.0f, -1.0f});
    Vec3 toBlue = normalize(Vec3{KICKOFF_BLUE.x, 6.0f, KICKOFF_BLUE.z});
    Mat4 turn = rotateAxis(normalize(cross(corner, toBlue)), std::acos(dot(corner, toBlue)));
    ballX = normalize(turn.transformDir(Vec3{1, 0, 0}));
    ballY = normalize(turn.transformDir(Vec3{0, 1, 0}));
    float boostLeft = car.boost;
    car = GroundCar{};
    car.position = KICKOFF_BLUE;
    car.yaw = std::atan2(-KICKOFF_BLUE.x, -KICKOFF_BLUE.z);
    car.boost = std::max(33.0f, boostLeft);
    float rivalBoost = rival.boost;
    GroundCar fresh;
    fresh.maxDrive = rival.maxDrive;
    fresh.maxBoost = rival.maxBoost;
    rival = fresh;
    rival.position = -KICKOFF_BLUE;
    rival.yaw = std::atan2(KICKOFF_BLUE.x, KICKOFF_BLUE.z);
    rival.boost = std::max(33.0f, rivalBoost);
    phase = Phase::Countdown;
    phaseTimer = 3.0f;
}

// Paredes vistas desde arriba: circulo (del carro o del balon) contra cada
// segmento. Si se metio, se saca y se quita (y rebota) la parte de la
// velocidad que iba hacia la pared.
void OpenWorld::collideWalls(Vec3& p, float radius, Vec3& v, bool isBall, float bounce) const {
    for (const Wall& w : walls) {
        if (w.ballOnly && !isBall) continue;
        if (p.y + radius < w.minY) continue;
        float abx = w.b.x - w.a.x, abz = w.b.y - w.a.y;
        float t = clampf(((p.x - w.a.x) * abx + (p.z - w.a.y) * abz) / (abx * abx + abz * abz), 0.0f, 1.0f);
        float qx = w.a.x + abx * t, qz = w.a.y + abz * t;
        float dx = p.x - qx, dz = p.z - qz;
        float d2 = dx * dx + dz * dz;
        if (d2 >= radius * radius) continue;
        float d = std::sqrt(d2);
        float nx, nz;
        if (d > 1e-5f) {
            nx = dx / d;
            nz = dz / d;
        } else { // justo encima de la linea: se saca hacia la normal del segmento
            float len = std::sqrt(abx * abx + abz * abz);
            nx = -abz / len;
            nz = abx / len;
        }
        p.x = qx + nx * radius;
        p.z = qz + nz * radius;
        float vn = v.x * nx + v.z * nz;
        if (vn < 0.0f) {
            v.x -= nx * vn * (1.0f + bounce);
            v.z -= nz * vn * (1.0f + bounce);
        }
    }
}

void OpenWorld::driveCar(GroundCar& c, const ShipInput& in, float h) {
    bool jumpPress = in.brake && !c.jumpHeld;
    c.jumpHeld = in.brake;
    c.boosting = in.boost && c.boost > 0.0f;

    if (c.onGround) {
        // La vertical del terreno, suavizada (las colinas inclinan el carro).
        float x = c.position.x, z = c.position.z;
        Vec3 n = normalize(Vec3{groundHeight(x - 0.5f, z) - groundHeight(x + 0.5f, z), 1.0f,
                                groundHeight(x, z - 0.5f) - groundHeight(x, z + 0.5f)});
        c.up = normalize(c.up + (n - c.up) * approach(10.0f, h));

        Vec3 f = c.flatForward();
        float along = dot(flat(c.velocity), f);
        if (c.boosting) {
            if (along < c.maxBoost) along += 30.0f * h;
        } else if (in.throttle > 0.0f) {
            // Acelera menos cerca de la velocidad maxima, como en Rocket League.
            if (along < c.maxDrive) along += 18.0f * in.throttle * h * (1.0f - 0.7f * std::max(0.0f, along) / c.maxDrive);
            else along = std::max(c.maxDrive, along - 8.0f * h);
        } else if (in.throttle < 0.0f) {
            if (along > 0.5f) along -= 35.0f * h;           // frenar
            else along = std::max(-10.0f, along - 14.0f * h); // reversa
        } else {
            along -= std::min(std::fabs(along), 6.0f * h) * (along > 0.0f ? 1.0f : -1.0f); // rodando sin acelerar
            if (along > c.maxDrive) along -= 8.0f * h;
        }
        along = std::min(along, c.maxBoost);

        // Giro: depende de la velocidad (parado no gira; rapido gira mas abierto).
        float speedFactor = std::min(1.0f, std::fabs(along) / 3.0f) / (1.0f + std::fabs(along) * 0.035f);
        c.yaw += in.steer * 2.8f * speedFactor * (along >= 0.0f ? 1.0f : -1.0f) * h;

        // Agarre: la velocidad de lado se pierde rapido (las llantas no patinan).
        f = c.flatForward();
        Vec3 hv = flat(c.velocity);
        Vec3 lateral = hv - f * dot(hv, f);
        hv = f * along + lateral * std::exp(-12.0f * h);
        c.velocity = hv;
        c.nose = 0.0f;

        if (jumpPress) {
            c.velocity += c.up * 5.8f;
            c.onGround = false;
            c.canFlip = true;
            c.airTime = 0.0f;
        }
    } else {
        c.airTime += h;
        c.yaw += in.steer * 2.4f * h;
        // La nariz se controla con las flechas; sin tocarlas vuelve sola despacio.
        if (in.pitch != 0.0f) c.nose = clampf(c.nose + in.pitch * 2.4f * h, -0.9f, 1.45f);
        else if (!c.boosting) c.nose -= c.nose * approach(1.0f, h);
        if (c.boosting) {
            // Boost en el aire: empuja hacia donde apunta la nariz (con ~45
            // grados ya vence a la gravedad: asi se vuela).
            Vec3 dir = c.flatForward() * std::cos(c.nose) + WORLD_UP * std::sin(c.nose);
            c.velocity += dir * (17.0f * h);
        }
        c.velocity.y -= GRAVITY * h;
        if (jumpPress && c.canFlip && c.airTime < 1.4f) {
            // Segundo salto: flip hacia donde se apuntan las teclas (adelante si no).
            Vec3 f = c.flatForward();
            Vec3 right = cross(f, WORLD_UP);
            Vec3 dir = f * (in.throttle < 0.0f ? -1.0f : (in.throttle > 0.0f || in.steer == 0.0f ? 1.0f : 0.0f)) -
                       right * in.steer;
            c.flipDir = normalize(dir);
            c.velocity += c.flipDir * 6.0f;
            c.velocity.y = std::max(c.velocity.y, 1.0f);
            c.canFlip = false;
            c.flipTime = 0.0f;
        }
    }
    if (c.flipTime >= 0.0f) {
        c.flipTime += h;
        if (c.flipTime > FLIP_TIME) c.flipTime = -1.0f;
    }
    if (c.boosting) c.boost = std::max(0.0f, c.boost - 33.3f * h);
    float speed = length(c.velocity);
    if (speed > 32.0f) c.velocity = c.velocity * (32.0f / speed);

    c.position += c.velocity * h;
    float ground = groundHeight(c.position.x, c.position.z);
    if (c.onGround) {
        c.position.y = ground; // pegado al terreno
    } else if (c.position.y <= ground) { // aterrizo
        c.position.y = ground;
        c.velocity.y = 0.0f;
        c.onGround = true;
        c.nose = 0.0f;
        c.flipTime = -1.0f;
        c.canFlip = false;
    }
    collideWalls(c.position, CAR_WALL_R, c.velocity, false, 0.1f);
    // Borde del mundo.
    if (std::fabs(c.position.x) > WORLD_LIMIT) {
        c.position.x = clampf(c.position.x, -WORLD_LIMIT, WORLD_LIMIT);
        c.velocity.x = 0.0f;
    }
    if (std::fabs(c.position.z) > WORLD_LIMIT) {
        c.position.z = clampf(c.position.z, -WORLD_LIMIT, WORLD_LIMIT);
        c.velocity.z = 0.0f;
    }
}

// Rival naranja: ataca la porteria azul (-Z). Se pone detras del balon en la
// linea hacia esa porteria y acelera; si quedo del lado equivocado, rodea el
// balon para volver a su lado. Salta si el balon esta alto y cerca.
ShipInput OpenWorld::rivalInput() const {
    ShipInput in;
    bool active = phase == Phase::Play && playerInStadium();
    Vec3 target;
    if (!active) {
        target = Vec3{0.0f, 0.0f, FIELD_L - 6.0f}; // espera frente a su porteria
    } else {
        Vec3 ball = ballPos + flat(ballVel) * 0.25f; // un poco adelante de donde va
        Vec3 toGoal = normalize(flat(Vec3{0.0f, 0.0f, -FIELD_L - 2.0f} - ball));
        target = ball - toGoal * 2.0f;
        if (rival.position.z < ball.z - 1.0f) {
            float side = rival.position.x < ball.x ? -1.0f : 1.0f;
            target = Vec3{ball.x + side * 5.0f, 0.0f, ball.z + 7.0f};
        }
    }
    Vec3 f = rival.flatForward();
    Vec3 right = cross(f, WORLD_UP);
    Vec3 d = flat(target - rival.position);
    float dist = length(d);
    float angle = std::atan2(-dot(d, right), dot(d, f)); // positivo = a la izquierda
    in.steer = clampf(angle * 2.5f, -1.0f, 1.0f);
    in.throttle = (std::fabs(angle) > 2.2f && dist < 8.0f) ? -1.0f : 1.0f;
    if (!active && dist < 2.5f) in.throttle = 0.0f;
    in.boost = active && std::fabs(angle) < 0.25f && dist > 12.0f && rival.boost > 10.0f;
    Vec3 toBall = flat(ballPos - rival.position);
    in.brake = active && rival.onGround && length(toBall) < 3.0f && ballPos.y > 2.3f;
    return in;
}

// Golpe del carro al balon: esfera contra esfera con la velocidad relativa.
// El carro pesa mucho mas, asi que solo cambia el balon. Un flip le pega mas fuerte.
void OpenWorld::hitBall(const GroundCar& c) {
    Vec3 f, u, r;
    c.axes(f, u, r);
    Vec3 center = c.position + u * CAR_HIT_UP;
    Vec3 d = ballPos - center;
    float dist = length(d);
    float minDist = BALL_R + CAR_HIT_R;
    if (dist >= minDist || dist < 1e-4f) return;
    Vec3 m = d * (1.0f / dist);
    ballPos = center + m * minDist;
    float rel = dot(ballVel - c.velocity, m);
    if (rel < 0.0f) {
        ballVel -= m * (rel * 1.6f);
        ballVel += m * 1.5f;
        if (c.flipTime >= 0.0f && c.flipTime < 0.4f) ballVel += c.flipDir * 7.0f + WORLD_UP * 1.5f;
        touches++;
    }
}

void OpenWorld::stepBall(float h) {
    ballVel.y -= GRAVITY * h;
    ballVel = ballVel * std::exp(-0.03f * h);
    ballPos += ballVel * h;

    if (ballPos.y < BALL_R) { // suelo
        ballPos.y = BALL_R;
        if (ballVel.y < 0.0f) ballVel.y = -ballVel.y * 0.6f;
        if (ballVel.y < 0.6f) ballVel.y = 0.0f;
        Vec3 hv = flat(ballVel) * std::exp(-0.25f * h); // friccion al rodar
        ballVel = Vec3{hv.x, ballVel.y, hv.z};
    }
    if (ballPos.y > CEILING - BALL_R) {
        ballPos.y = CEILING - BALL_R;
        if (ballVel.y > 0.0f) ballVel.y = -ballVel.y * 0.6f;
    }
    // Techo de la porteria.
    if (std::fabs(ballPos.z) > FIELD_L && std::fabs(ballPos.x) < GOAL_W && ballPos.y + BALL_R > GOAL_H) {
        ballPos.y = GOAL_H - BALL_R;
        if (ballVel.y > 0.0f) ballVel.y = -ballVel.y * 0.5f;
    }
    collideWalls(ballPos, BALL_R, ballVel, true, 0.6f);
    float speed = length(ballVel);
    if (speed > 60.0f) ballVel = ballVel * (60.0f / speed);

    // Rueda: gira lo que avanzo entre su radio (en el suelo, alrededor del
    // eje horizontal perpendicular al movimiento).
    Vec3 hv = flat(ballVel);
    float rolled = length(hv) * h / BALL_R;
    if (rolled > 1e-6f) {
        Vec3 axis = normalize(cross(WORLD_UP, hv));
        ballX = normalize(rotateAround(ballX, axis, rolled));
        ballY = rotateAround(ballY, axis, rolled);
        ballY = normalize(ballY - ballX * dot(ballX, ballY));
    }
}

// Choque entre los dos carros: se separan y se reparten la velocidad (misma masa).
void OpenWorld::collideCars() {
    Vec3 d = (car.position + car.up * CAR_HIT_UP) - (rival.position + rival.up * CAR_HIT_UP);
    float dist = length(d);
    const float minDist = 1.5f;
    if (dist >= minDist || dist < 1e-4f) return;
    Vec3 n = d * (1.0f / dist);
    if (car.onGround && rival.onGround) n = normalize(flat(n));
    float push = (minDist - dist) * 0.5f;
    car.position += flat(n * push);
    rival.position -= flat(n * push);
    float rel = dot(car.velocity - rival.velocity, n);
    if (rel < 0.0f) {
        float j = -1.3f * rel * 0.5f;
        car.velocity += n * j;
        rival.velocity -= n * j;
    }
}

void OpenWorld::takePads(GroundCar& c) {
    for (Pad& p : pads) {
        if (p.cooldown > 0.0f || c.position.y > 1.5f) continue;
        float dx = c.position.x - p.position.x, dz = c.position.z - p.position.z;
        if (dx * dx + dz * dz < 1.6f * 1.6f && c.boost < 100.0f) {
            c.boost = 100.0f;
            p.cooldown = 10.0f;
        }
    }
}

void OpenWorld::scoreGoal(bool blueScored, Particles& particles) {
    if (blueScored) blue++;
    else orange++;
    lastGoalBlue = blueScored;
    // Explosion de gol, como en Rocket League.
    particles.burst(ballPos, Vec3{0, 0, 0}, 40, 0.0f, 3.0f, 14.0f, 0.3f, 0.6f);
    particles.burst(ballPos, Vec3{0, 0, 0}, 320, 4.0f, 20.0f, 4.0f, 0.8f, 2.0f);
    phase = Phase::Goal;
    phaseTimer = 3.0f;
    std::printf("%s  azul %d - %d naranja\n", blueScored ? "Gol azul" : "Gol naranja", blue, orange);
}

void OpenWorld::step(const ShipInput& input, float h, Particles& particles) {
    bool frozen = phase == Phase::Countdown;
    if (frozen) { // en la cuenta regresiva nadie se mueve (pero se registra el salto)
        car.jumpHeld = input.brake;
    } else {
        driveCar(car, input, h);
        driveCar(rival, rivalInput(), h);
        collideCars();
    }
    takePads(car);
    takePads(rival);
    for (Pad& p : pads) p.cooldown = std::max(0.0f, p.cooldown - h);

    if (phase == Phase::Goal || frozen) return; // el balon no esta (exploto) o espera el saque
    stepBall(h);
    hitBall(car);
    hitBall(rival);

    // Gol: el balon cruzo completo la linea de gol entre los postes.
    if (phase == Phase::Play && std::fabs(ballPos.x) < GOAL_W && std::fabs(ballPos.z) > FIELD_L + BALL_R) {
        scoreGoal(ballPos.z > 0.0f, particles); // la porteria naranja (+Z) es la que ataca el azul
    }
}

void OpenWorld::update(const ShipInput& input, float dt, Particles& particles) {
    if (dt <= 0.0f) return;
    accumulator += dt;
    while (accumulator >= STEP) {
        step(input, STEP, particles);
        accumulator -= STEP;
    }

    switch (phase) {
        case Phase::Waiting:
            if (playerInStadium()) phase = Phase::Play;
            break;
        case Phase::Play:
            if (playerInStadium() && !overtime) {
                clock -= dt;
                if (clock <= 0.0f) {
                    clock = 0.0f;
                    if (blue != orange) phase = Phase::Finished;
                    else overtime = true; // empate: el siguiente gol gana
                }
            }
            break;
        case Phase::Goal:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) {
                if (overtime || clock <= 0.0f) phase = Phase::Finished;
                else kickoff();
            }
            break;
        case Phase::Countdown:
            phaseTimer -= dt;
            if (phaseTimer <= 0.0f) phase = Phase::Play;
            break;
        case Phase::Finished:
            break;
    }

    // Estela del boost de los dos carros.
    GroundCar* cars[2] = {&car, &rival};
    for (int k = 0; k < 2; ++k) {
        const GroundCar& c = *cars[k];
        if (c.boosting) {
            Mat4 m = c.modelMatrix();
            Vec3 f, u, r;
            c.axes(f, u, r);
            const Vec3 exhausts[2] = {EXHAUST_LEFT, EXHAUST_RIGHT};
            for (int i = 0; i < 2; ++i) {
                Vec3 now = m.transformPoint(exhausts[i]);
                particles.emitStream(wasBoosting[k] ? exhaustLast[k][i] : now, now, f * -4.0f, dt);
                exhaustLast[k][i] = now;
            }
        }
        wasBoosting[k] = c.boosting;
    }
    updateCamera(dt);
}

void OpenWorld::updateCamera(float dt) {
    // Camara de Rocket League: detras del carro (o detras de la linea carro
    // -> balon con la camara al balon), un poco arriba.
    Vec3 dir = car.flatForward();
    if (ballCamera && phase != Phase::Goal) {
        Vec3 d = flat(ballPos - car.position);
        if (length(d) > 0.5f) dir = normalize(d);
    }
    Vec3 wanted = dir * -4.6f + WORLD_UP * 1.7f;
    float k = dt > 0.0f ? approach(8.0f, dt) : 1.0f; // dt = 0: sin suavizar (snapCamera)
    cameraOffset += (wanted - cameraOffset) * k;
    Vec3 pivot = car.position + WORLD_UP * 0.6f;
    eye = pivot + cameraOffset;
    eye.y = std::max(eye.y, groundHeight(eye.x, eye.z) + 0.5f);
    if (playerInStadium()) { // dentro del estadio la camara no se sale de la cancha
        Vec3 none;
        collideWalls(eye, 0.4f, none, false, 0.0f);
    }
    if (ballCamera && phase != Phase::Goal) target = pivot + normalize(ballPos - pivot) * 3.0f;
    else target = pivot + dir * 2.5f;
}

void OpenWorld::snapCamera() {
    cameraOffset = car.flatForward() * -4.6f + WORLD_UP * 1.7f;
    updateCamera(0.0f);
}

// ---------------------------------------------------------------- dibujo

// Cielo de dia: degradado del horizonte al cenit, Albor (la estrella blanca,
// enorme vista desde aqui) con su halo, y Cobalto como una luna en el cielo.
static void drawSky(Framebuffer& fb, const Uniforms& u, const Vec3& sun) {
    const int W = fb.width(), H = fb.height();
    Vec3 right{u.view.m[0][0], u.view.m[0][1], u.view.m[0][2]};
    Vec3 up{u.view.m[1][0], u.view.m[1][1], u.view.m[1][2]};
    Vec3 back{u.view.m[2][0], u.view.m[2][1], u.view.m[2][2]};
    float sx = 1.0f / u.projection.m[0][0], sy = 1.0f / u.projection.m[1][1];
    const Vec3 moon = normalize(Vec3{-0.55f, 0.32f, -0.77f});
    const float moonRadius = 0.06f, sunRadius = 0.11f;
    const float moonSin = std::sin(moonRadius), moonCos = std::cos(moonRadius), sunCos = std::cos(sunRadius);
    const int bandHeight = 16;
    threadPool().parallelFor((H + bandHeight - 1) / bandHeight, [&](int band) {
        int y0 = band * bandHeight, y1 = std::min(H, y0 + bandHeight);
        for (int y = y0; y < y1; ++y) {
            float ny = (1.0f - (y + 0.5f) / H * 2.0f) * sy;
            for (int x = 0; x < W; ++x) {
                float nx = ((x + 0.5f) / W * 2.0f - 1.0f) * sx;
                Vec3 d = normalize(right * nx + up * ny - back);
                Vec3 c = d.y > 0.0f ? lerp(SKY_HORIZON, SKY_TOP, std::sqrt(d.y)) : SKY_HORIZON;
                float s = dot(d, sun);
                if (s > 0.0f) { // halo de Albor
                    float s2 = s * s, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
                    c += Vec3{1.0f, 0.98f, 0.94f} * (s16 * s16 * 0.6f + s8 * 0.12f);
                }
                if (s > sunCos) c = lerp(Vec3{1.0f, 1.0f, 1.0f}, Vec3{0.85f, 0.92f, 1.0f}, (1.0f - s) / (1.0f - sunCos) * 0.6f);
                float m = dot(d, moon);
                if (m > moonCos) { // Cobalto: disco iluminado desde Albor
                    Vec3 o = (d - moon * m) * (1.0f / moonSin);
                    float r2 = std::min(1.0f, dot(o, o));
                    Vec3 n = o - moon * std::sqrt(1.0f - r2);
                    float lit = std::max(0.0f, dot(n, sun));
                    c = lerp(c, Vec3{0.25f, 0.45f, 0.85f} * (0.2f + 0.9f * lit), 0.85f);
                }
                uint32_t r = (uint32_t)(clampf(c.x, 0.0f, 1.0f) * 255.0f);
                uint32_t g = (uint32_t)(clampf(c.y, 0.0f, 1.0f) * 255.0f);
                uint32_t b = (uint32_t)(clampf(c.z, 0.0f, 1.0f) * 255.0f);
                fb.setPixel(x, y, 0xFF000000u | (r << 16) | (g << 8) | b);
            }
        }
    });
}

void OpenWorld::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CarAssets& assets, float time) const {
    u.lightPos = sunDirection * 100000.0f;
    sunDir = sunDirection;
    drawSky(fb, u, sunDirection);

    u.model = Mat4::identity();
    for (size_t i = 0; i < terrain.size(); ++i) {
        if (!sphereInFrustum(u, chunkCenters[i], chunkRadii[i])) {
            stats.objectsCulled++;
            continue;
        }
        stats.objectsDrawn++;
        drawMesh(fb, terrain[i], u, groundShader, stats);
    }
    drawMesh(fb, stadium, u, worldShader, stats);
    stats.objectsDrawn++;
    const float sceneryRadius = 70.0f; // medio trozo en diagonal mas lo que sobresalen
    for (size_t i = 0; i < scenery.size(); ++i) {
        if (scenery[i].triangles.empty()) continue;
        float dist = length(flat(sceneryCenters[i] - u.cameraPos));
        if (dist - sceneryRadius > 140.0f || !sphereInFrustum(u, sceneryCenters[i], sceneryRadius)) {
            stats.objectsCulled++;
            continue;
        }
        stats.objectsDrawn++;
        drawMesh(fb, scenery[i], u, worldShader, stats);
    }

    for (const Pad& p : pads) {
        if (!sphereInFrustum(u, p.position, 2.0f)) continue;
        float bob = 0.15f * std::sin(time * 3.0f + p.position.x);
        u.model = translate(p.position) * rotateY(time * 1.5f) * translate(Vec3{0.0f, bob, 0.0f});
        drawMesh(fb, p.cooldown > 0.0f ? padEmpty : padActive, u, worldShader, stats);
    }

    // Sombras (discos que oscurecen el suelo, sin escribir profundidad).
    bool cull = u.cullBackFaces;
    u.cullBackFaces = false;
    u.additive = true;
    u.depthWrite = false;
    if (phase != Phase::Goal) {
        float k = 1.0f + ballPos.y * 0.04f;
        u.model = translate(Vec3{ballPos.x, 0.03f, ballPos.z}) * scale3({BALL_R * k, 1.0f, BALL_R * k});
        drawMesh(fb, shadowDisc, u, shadowShader, stats);
    }
    for (const GroundCar* c : {&car, &rival}) {
        float g = groundHeight(c->position.x, c->position.z) + 0.04f;
        u.model = translate(Vec3{c->position.x, g, c->position.z}) * rotateY(c->yaw) * scale3({0.5f, 1.0f, 0.85f});
        drawMesh(fb, shadowDisc, u, shadowShader, stats);
    }
    u.additive = false;
    u.depthWrite = true;
    u.cullBackFaces = cull;

    if (phase != Phase::Goal) {
        u.model = fromBasis(ballX, ballY, cross(ballX, ballY), ballPos) * scale(BALL_R);
        u.textures = &ballTextures;
        drawMesh(fb, ballMesh, u, ballShader, stats);
        u.textures = nullptr;
    }

    // Carros: el modelo reducido si se ven pequenos.
    const GroundCar* cars[2] = {&car, &rival};
    for (int k = 0; k < 2; ++k) {
        const GroundCar& c = *cars[k];
        Vec3 center = c.position + c.up * WHEEL_OFFSET;
        if (!sphereInFrustum(u, center, 0.9f)) {
            stats.objectsCulled++;
            continue;
        }
        stats.objectsDrawn++;
        // Aqui la camara va mas cerca que en el espacio: el modelo reducido
        // basta hasta que el carro ocupa ~100 pixeles de radio.
        bool lod = projectedRadius(u, center, 0.8f, fb.height()) < 100.0f;
        const Mesh* const* meshes = k == 0 ? assets.blue : assets.orange;
        u.model = c.modelMatrix();
        drawMesh(fb, *meshes[lod ? 1 : 0], u, carShader, stats);
        if (c.boosting) {
            float length = 0.75f + 0.2f * std::sin(time * 31.0f + k);
            for (const Vec3& exhaust : {EXHAUST_LEFT, EXHAUST_RIGHT}) {
                u.model = c.modelMatrix() * translate(exhaust) * scale3({EXHAUST_RADIUS, EXHAUST_RADIUS, length});
                drawMesh(fb, *assets.flame, u, flameShader, stats);
            }
        }
    }
}

static void centered(Framebuffer& fb, int y, const char* text, uint32_t color, int scale = 1) {
    drawText(fb, (fb.width() - textWidth(text, scale)) / 2, y, text, color, scale);
}

void OpenWorld::drawHud(Framebuffer& fb, float fps, float frameMs, const RenderStats& stats, bool showHelp) const {
    const uint32_t WHITE = 0xFFE8ECF4, GREY = 0xFFB0B8C8, BLUE = 0xFF5AA8FF, ORANGE = 0xFFFFA040, GOLD = 0xFFFFD860;
    const int W = fb.width(), H = fb.height();
    char line[128];

    // Marcador: AZUL n   m:ss   n NARANJA
    darkenRect(fb, W / 2 - 150, 6, 300, 40, 0.55f);
    char blueText[16], orangeText[16], clockText[16];
    std::snprintf(blueText, sizeof(blueText), "AZUL %d", blue);
    std::snprintf(orangeText, sizeof(orangeText), "%d NARANJA", orange);
    int secs = (int)std::ceil(clock);
    std::snprintf(clockText, sizeof(clockText), "%d:%02d", secs / 60, secs % 60);
    drawText(fb, W / 2 - 140, 12, blueText, BLUE, 2);
    drawText(fb, W / 2 + 140 - textWidth(orangeText, 2), 12, orangeText, ORANGE, 2);
    centered(fb, 12, overtime ? "+" : clockText, WHITE, 2);
    if (overtime) centered(fb, 32, "TIEMPO EXTRA: EL SIGUIENTE GOL GANA", GOLD);

    // Mensajes del partido.
    if (phase == Phase::Waiting) {
        float dx = car.position.x - FIELD_W, dz = car.position.z;
        std::snprintf(line, sizeof(line), "ENTRA AL ESTADIO POR LAS PUERTAS LATERALES  (%.0f)", std::sqrt(dx * dx + dz * dz));
        darkenRect(fb, W / 2 - 170, 52, 340, 16, 0.5f);
        centered(fb, 56, line, WHITE);
    } else if (phase == Phase::Countdown) {
        std::snprintf(line, sizeof(line), "%d", (int)std::ceil(phaseTimer));
        centered(fb, H / 2 - 70, line, WHITE, 7);
    } else if (phase == Phase::Goal) {
        centered(fb, H / 2 - 70, lastGoalBlue ? "GOL!" : "GOL NARANJA", lastGoalBlue ? BLUE : ORANGE, 6);
    } else if (phase == Phase::Finished) {
        const char* msg = blue > orange ? "GANASTE!" : "PERDISTE";
        centered(fb, H / 2 - 70, msg, blue > orange ? GOLD : ORANGE, 6);
        centered(fb, H / 2 - 20, "ENTER: OTRO PARTIDO", WHITE, 2);
    }

    // Boost (abajo a la derecha, como en Rocket League) y velocidad.
    std::snprintf(line, sizeof(line), "%.0f", car.boost);
    darkenRect(fb, W - 112, H - 62, 104, 54, 0.55f);
    drawText(fb, W - 104, H - 56, "BOOST", GREY);
    drawText(fb, W - 104, H - 44, line, car.boosting ? GOLD : ORANGE, 4);
    drawText(fb, 16, H - 36, "VEL", GREY);
    drawBar(fb, 56, H - 35, 160, 7, length(flat(car.velocity)) / car.maxBoost, 0xFF3CC8FF);

    // Lugar, rendimiento y ayuda.
    darkenRect(fb, 8, 6, 210, 30, 0.55f);
    drawText(fb, 14, 11, "ARENA - MUNDO ABIERTO", WHITE);
    std::snprintf(line, sizeof(line), "CAMARA AL BALON %s", ballCamera ? "SI" : "NO");
    drawText(fb, 14, 23, line, ballCamera ? GOLD : GREY);
    std::snprintf(line, sizeof(line), "FPS %.0f  %.1f MS  TRIS %d", fps, frameMs, stats.trianglesIn);
    drawText(fb, W - textWidth(line) - 12, 10, line, fps >= 30.0f ? 0xFF60E880 : ORANGE);

    if (showHelp) {
        static const char* help[] = {
            "CONTROLES               (H OCULTA)",
            "W / S      ACELERAR / FRENAR Y REVERSA",
            "A / D      GIRAR",
            "SHIFT      BOOST (LOS PADS LO LLENAN)",
            "ESPACIO    SALTAR (OTRA VEZ: FLIP)",
            "^ / ~      NARIZ EN EL AIRE (+BOOST: VOLAR)",
            "C          CAMARA AL BALON",
            "ENTER      OTRO PARTIDO",
            "R          VOLVER A LA ENTRADA",
            "U          DESPEGAR AL ESPACIO",
        };
        const int lines = (int)(sizeof(help) / sizeof(help[0]));
        const int hx = 8, hy = 44, hw = 270, hh = lines * 11 + 10;
        darkenRect(fb, hx, hy, hw, hh, 0.6f);
        for (int i = 0; i < lines; ++i) drawText(fb, hx + 8, hy + 6 + i * 11, help[i], i == 0 ? ORANGE : WHITE);
    } else {
        drawText(fb, W - textWidth("H: AYUDA   U: DESPEGAR") - 12, H - 76, "H: AYUDA   U: DESPEGAR", GREY);
    }
}
