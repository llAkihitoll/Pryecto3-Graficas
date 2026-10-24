#pragma once
#include <random>
#include <vector>
#include "framebuffer.h"
#include "mesh.h"
#include "renderer.h"

// Boost como en Rocket League: una llama de cohete sale de cada uno de los dos
// escapes que ya tiene el modelo del Fennec, y deja una estela de particulas.

// Penacho de la llama: superficie de revolucion alrededor del eje Z, de z = 0
// (el escape) a z = 1 (la punta). Se ensancha al salir y luego se afila.
// Radio maximo ~1; se escala segun el empuje. tint es el color de la llama.
Mesh makeFlame(Vec3 tint);

// Escapes del Fennec, en las coordenadas del carro (frente hacia -Z). Se
// encontraron buscando en el modelo los anillos de las paredes de los tubos
// en la parte trasera: centro, radio y donde termina cada tubo.
const Vec3 EXHAUST_LEFT{-0.135f, 0.021f, 0.69f};
const Vec3 EXHAUST_RIGHT{0.135f, 0.021f, 0.69f};
const float EXHAUST_RADIUS = 0.045f;

// Estela del boost: particulas que salen de los escapes, quedan en el mundo
// y en ~0.6 s pasan de fuego a humo mientras crecen y se apagan.
class BoostTrail {
public:
    // Suelta particulas durante dt segundos mientras el escape se movio de
    // "from" a "to" (en el mundo), con velocidad base "velocity" mas un poco
    // de dispersion. Cada particula nace en el punto del camino que le toca
    // segun su instante, asi la estela sale continua aunque el carro vaya rapido.
    void emit(const Vec3& from, const Vec3& to, const Vec3& velocity, float dt);

    // Envejece y mueve las particulas; quita las que ya se apagaron.
    void update(float dt);

    // Dibuja cada particula como un disco brillante con mezcla aditiva.
    void draw(Framebuffer& fb, const Uniforms& u) const;

    int count() const { return (int)particles.size(); }

private:
    struct Particle {
        Vec3 position, velocity;
        float age, life;
    };
    std::vector<Particle> particles;
    std::mt19937 rng{7};
    float emitCarry = 0.0f; // fraccion de particula que quedo pendiente del cuadro anterior
};
