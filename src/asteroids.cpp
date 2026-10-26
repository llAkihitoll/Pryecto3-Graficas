#include "asteroids.h"
#include <algorithm>
#include <cmath>
#include "scene.h"
#include "shaders.h"

// Medidas del cinturon (entre la orbita de Glacia, 230, y la de Vergel, 330).
const float BELT_INNER = 262.0f, BELT_OUTER = 302.0f;
const float BELT_THICKNESS = 5.0f;   // cuanto se separan del plano de las orbitas
const float RESPAWN_TIME = 8.0f;     // segundos hasta que una roca reaparece
const float MAX_FLIGHT_TIME = 40.0f; // una roca que no choca con nada se pierde
const float ROCK_CENTER_Y = 0.32f;   // el modelo de roca tiene la base en y = 0

static float unitRand(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }

void AsteroidBelt::placeInBelt(Asteroid& a) {
    a.orbitRadius = BELT_INNER + (BELT_OUTER - BELT_INNER) * unitRand(rng);
    a.phase = unitRand(rng) * 2.0f * PI;
    a.omega = 2.0f * PI / keplerPeriod(a.orbitRadius); // las de adentro van mas rapido
    // Altura: casi todas cerca del plano y pocas mas alejadas.
    float h = unitRand(rng) * 2.0f - 1.0f;
    a.height = h * std::fabs(h) * BELT_THICKNESS;
    a.state = State::Belt;
    a.timer = 0.0f;
}

void AsteroidBelt::build(unsigned seed, const std::vector<Mesh>& rockModels, int count) {
    models = &rockModels;
    rng.seed(seed);
    asteroids.clear();
    for (int i = 0; i < count; ++i) {
        Asteroid a{};
        a.model = (int)(unitRand(rng) * rockModels.size()) % (int)rockModels.size();
        float s = unitRand(rng);
        a.size = 0.5f + 2.5f * s * s * s; // muchas chicas y pocas grandes
        Vec3 axis{unitRand(rng) - 0.5f, unitRand(rng) - 0.5f, unitRand(rng) - 0.5f};
        a.spinAxis = normalize(axis + Vec3{0.0f, 0.01f, 0.0f});
        a.spinRate = 0.2f + 1.0f * unitRand(rng);
        a.spinAngle = unitRand(rng) * 2.0f * PI;
        placeInBelt(a);
        asteroids.push_back(a);
    }
}

// Igual que los planetas: angulo = fase + omega * t en el plano XZ.
Vec3 AsteroidBelt::beltPosition(const Asteroid& a, float time) const {
    float ang = a.phase + a.omega * time;
    return Vec3{a.orbitRadius * std::sin(ang), a.height, a.orbitRadius * std::cos(ang)};
}

Vec3 AsteroidBelt::beltVelocity(const Asteroid& a, float time) const {
    float ang = a.phase + a.omega * time;
    return Vec3{a.orbitRadius * a.omega * std::cos(ang), 0.0f, -a.orbitRadius * a.omega * std::sin(ang)};
}

void AsteroidBelt::launchForTest(const Vec3& from, const Vec3& velocity) {
    if (asteroids.empty()) return;
    Asteroid& a = asteroids[0];
    a.position = from;
    a.velocity = velocity;
    a.state = State::Flying;
    a.timer = 0.0f;
}

int AsteroidBelt::flying() const {
    int n = 0;
    for (const Asteroid& a : asteroids) n += a.state == State::Flying;
    return n;
}

void AsteroidBelt::explode(Asteroid& a, const SphereCollider& body, Particles& particles) {
    // Punto de impacto: sobre la superficie, en la direccion de la roca.
    Vec3 n = normalize(a.position - body.center);
    Vec3 impact = body.center + n * body.surfaceRadius(a.position);
    // Fogonazo corto y grande, luego fuego y humo que se abren. Las
    // particulas heredan la velocidad del planeta para no quedarse atras.
    particles.burst(impact, body.velocity, 14, 0.0f, 1.5f, 16.0f * a.size, 0.12f, 0.3f);
    particles.burst(impact, body.velocity + n * 2.0f, 110, 2.0f, 9.0f, 5.0f * a.size, 0.8f, 1.6f);
    a.state = State::Gone;
    a.timer = RESPAWN_TIME;
    explosionCount++;
}

void AsteroidBelt::update(float time, float dt, const std::vector<SphereCollider>& bodies, Particles& particles) {
    for (Asteroid& a : asteroids) {
        a.spinAngle += a.spinRate * dt;
        if (a.state == State::Belt) {
            a.position = beltPosition(a, time);
            a.velocity = beltVelocity(a, time);
        } else if (a.state == State::Flying) {
            // Vuelo libre: la misma gravedad que siente el carro (con alcance limitado).
            for (const SphereCollider& b : bodies) {
                Vec3 d = b.center - a.position;
                float r2 = std::max(dot(d, d), b.radius * b.radius);
                float r = std::sqrt(r2);
                float g = b.surfaceGravity * b.radius * b.radius / r2;
                if (b.gravityRange > 0.0f) {
                    float t = clampf((r - b.gravityRange * 0.66f) / (b.gravityRange * 0.34f), 0.0f, 1.0f);
                    g *= 1.0f - t * t * (3.0f - 2.0f * t);
                }
                a.velocity += d * (g / r * dt);
            }
            a.position += a.velocity * dt;
            a.timer += dt;

            // Choque con un planeta o con el sol: explota.
            for (const SphereCollider& b : bodies) {
                if (length(a.position - b.center) < b.surfaceRadius(a.position) + a.size * 0.8f) {
                    explode(a, b, particles);
                    break;
                }
            }
            if (a.state == State::Flying && a.timer > MAX_FLIGHT_TIME) {
                a.state = State::Gone;
                a.timer = RESPAWN_TIME;
            }
        } else { // Gone: esperando para reaparecer en otro lugar del cinturon
            a.timer -= dt;
            if (a.timer <= 0.0f) {
                placeInBelt(a);
                a.position = beltPosition(a, time);
            }
        }
    }
}

int AsteroidBelt::collideShip(Ship& ship) {
    int hits = 0;
    for (Asteroid& a : asteroids) {
        if (a.state == State::Gone) continue;
        Vec3 d = a.position - ship.position;
        float dist = length(d);
        float minDist = a.size + ship.tuning.collisionRadius;
        if (dist >= minDist || dist < 1e-4f) continue;

        Vec3 n = d * (1.0f / dist); // del carro hacia la roca
        Vec3 shipVel = ship.velocity + ship.gravityVelocity;
        Vec3 rockVel = a.velocity; // en el cinturon es su velocidad orbital
        // Rapidez con que se acercan a lo largo de la linea entre centros.
        float closing = dot(shipVel - rockVel, n);
        if (closing > 0.0f) {
            // La roca es mas liviana que el carro: sale mas rapido de lo que
            // iba el carro, y el carro pierde parte de su velocidad.
            a.velocity = rockVel + n * (closing * 1.6f + 2.0f);
            a.spinRate *= 4.0f;
            a.state = State::Flying;
            a.timer = 0.0f;
            ship.velocity -= n * (closing * 0.5f);
            hits++;
        }
        ship.position = a.position - n * minDist; // separarlos para que no se encimen
    }
    return hits;
}

void AsteroidBelt::draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const Optimizations& opt) {
    batch.vertices.clear();
    batch.triangles.clear();
    if (batch.materials.empty()) {
        Material rock{"Asteroide", {0.44f, 0.40f, 0.37f}};
        batch.materials.push_back(rock);
    }

    for (const Asteroid& a : asteroids) {
        if (a.state == State::Gone) continue;
        float bound = a.size * 1.4f; // el modelo de roca mide ~1.4 de radio a escala 1
        if (opt.cullObjects && !sphereInFrustum(u, a.position, bound)) continue;
        // Muy lejos (menos de ~1 pixel): basta con un punto.
        if (projectedRadius(u, a.position, bound, fb.height()) < 1.2f) {
            drawPoint3D(fb, u, a.position, 0xFF8A8580, 2);
            continue;
        }
        stats.asteroidsDrawn++;
        const Mesh& model = (*models)[a.model];
        Mat4 m = translate(a.position) * rotateAxis(a.spinAxis, a.spinAngle) * scale(a.size) *
                 translate({0.0f, -ROCK_CENTER_Y, 0.0f});
        int base = (int)batch.vertices.size();
        for (const Vertex& v : model.vertices) {
            batch.vertices.push_back(Vertex{m.transformPoint(v.position), normalize(m.transformDir(v.normal))});
        }
        for (const Triangle& t : model.triangles) batch.triangles.push_back(Triangle{t.a + base, t.b + base, t.c + base, 0});
    }

    if (batch.triangles.empty()) return;
    u.model = Mat4::identity(); // la malla ya esta en coordenadas del mundo
    drawMesh(fb, batch, u, asteroidShader, stats);
}
