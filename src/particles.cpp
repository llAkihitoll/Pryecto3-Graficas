#include "particles.h"
#include <algorithm>
#include <cmath>

const float STREAM_PER_SECOND = 400.0f; // particulas por segundo del chorro (por emisor)
const float STREAM_LIFE = 0.6f;         // segundos

void Particles::emitStream(const Vec3& from, const Vec3& to, const Vec3& velocity, float dt) {
    std::uniform_real_distribution<float> spread(-1.0f, 1.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    // Se acumula la fraccion para que la cantidad dependa del tiempo y no
    // de los FPS (a 30 FPS salen mas por cuadro que a 144).
    emitCarry += STREAM_PER_SECOND * dt;
    int n = (int)emitCarry;
    emitCarry -= n;
    for (int i = 0; i < n; ++i) {
        Vec3 jitter{spread(rng), spread(rng), spread(rng)};
        // Instante de nacimiento repartido a lo largo del cuadro: una que nacio
        // hace "age" segundos salio de un punto anterior del camino del emisor.
        float f = unit(rng);
        float age = f * dt;
        Particle p{lerp(to, from, f), velocity + jitter * 1.2f, age, STREAM_LIFE * (0.7f + 0.6f * unit(rng)), 1.0f};
        p.position += p.velocity * age;
        particles.push_back(p);
    }
}

void Particles::burst(const Vec3& center, const Vec3& baseVelocity, int count, float speedMin, float speedMax,
                      float sizeScale, float lifeMin, float lifeMax) {
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    for (int i = 0; i < count; ++i) {
        // Direccion uniforme sobre la esfera (z uniforme y angulo uniforme).
        float z = unit(rng) * 2.0f - 1.0f;
        float a = unit(rng) * 2.0f * PI;
        float r = std::sqrt(1.0f - z * z);
        Vec3 dir{r * std::cos(a), r * std::sin(a), z};
        float speed = speedMin + (speedMax - speedMin) * unit(rng);
        float life = lifeMin + (lifeMax - lifeMin) * unit(rng);
        particles.push_back(Particle{center, baseVelocity + dir * speed, 0.0f, life, sizeScale * (0.6f + 0.8f * unit(rng))});
    }
}

void Particles::update(float dt) {
    float drag = std::exp(-2.0f * dt); // el humo se frena un poco
    for (Particle& p : particles) {
        p.age += dt;
        p.position += p.velocity * dt;
        p.velocity = p.velocity * drag;
    }
    particles.erase(std::remove_if(particles.begin(), particles.end(),
                                   [](const Particle& p) { return p.age >= p.life; }),
                    particles.end());
}

static float smooth01(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void Particles::draw(Framebuffer& fb, const Uniforms& u) const {
    const int W = fb.width(), H = fb.height();
    Mat4 viewProj = u.projection * u.view;
    for (const Particle& p : particles) {
        float a = p.age / p.life; // 0 recien salida, 1 apagada
        // Color segun la edad: fuego blanco amarillo -> naranja -> rojo -> humo gris.
        Vec3 color = lerp(Vec3{1.0f, 0.85f, 0.5f}, Vec3{1.0f, 0.4f, 0.08f}, smooth01(0.0f, 0.25f, a));
        color = lerp(color, Vec3{0.45f, 0.08f, 0.02f}, smooth01(0.25f, 0.6f, a));
        color = lerp(color, Vec3{0.16f, 0.16f, 0.18f}, smooth01(0.6f, 1.0f, a));
        float intensity = std::pow(1.0f - a, 1.5f);
        float size = (0.1f + 0.45f * a) * p.sizeScale; // el humo se expande

        Vec4 c = viewProj * Vec4(p.position, 1.0f);
        if (c.w < 0.05f) continue; // detras de la camara
        float invW = 1.0f / c.w;
        float sx = (c.x * invW * 0.5f + 0.5f) * W;
        float sy = (1.0f - (c.y * invW * 0.5f + 0.5f)) * H;
        float radius = std::min(60.0f, size * u.projection.m[1][1] * H * 0.5f * invW);
        if (radius < 0.5f) radius = 0.5f;

        int x0 = std::max(0, (int)(sx - radius)), x1 = std::min(W - 1, (int)(sx + radius));
        int y0 = std::max(0, (int)(sy - radius)), y1 = std::min(H - 1, (int)(sy + radius));
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                // Prueba de profundidad sin escribirla: lo de adelante la tapa.
                if (invW <= fb.depthAt(x, y)) continue;
                float dx = (x + 0.5f - sx) / radius, dy = (y + 0.5f - sy) / radius;
                float d2 = dx * dx + dy * dy;
                if (d2 >= 1.0f) continue;
                float falloff = (1.0f - d2) * (1.0f - d2); // mas brillante al centro
                float k = intensity * falloff * 0.5f;
                // Mezcla aditiva: se suma al color que ya hay (se ve translucida
                // y no importa el orden en que se dibujen las particulas).
                uint32_t px = fb.getPixel(x, y);
                int r = std::min(255, (int)((px >> 16) & 0xFF) + (int)(color.x * k * 255.0f));
                int g = std::min(255, (int)((px >> 8) & 0xFF) + (int)(color.y * k * 255.0f));
                int b = std::min(255, (int)(px & 0xFF) + (int)(color.z * k * 255.0f));
                fb.setPixel(x, y, 0xFF000000u | (r << 16) | (g << 8) | b);
            }
        }
    }
}
