#pragma once
#include <random>
#include <vector>
#include "framebuffer.h"
#include "renderer.h"

// Particulas de fuego y humo: la estela del boost y las explosiones de los
// asteroides. Cada una es un disco brillante que pasa de fuego blanco
// amarillo a naranja, rojo y humo gris mientras crece y se apaga.
class Particles {
public:
    // Chorro continuo (estela del boost): suelta particulas durante dt
    // segundos mientras el emisor se movio de "from" a "to" (en el mundo).
    // Cada particula nace en el punto del camino que le toca segun su
    // instante, asi la estela sale continua aunque el emisor vaya rapido.
    void emitStream(const Vec3& from, const Vec3& to, const Vec3& velocity, float dt);

    // Rafaga (explosion): "count" particulas de golpe en direcciones al azar,
    // con rapidez entre speedMin y speedMax, sumada a baseVelocity (por
    // ejemplo, la del planeta que se mueve). sizeScale agranda los discos.
    void burst(const Vec3& center, const Vec3& baseVelocity, int count, float speedMin, float speedMax,
               float sizeScale, float lifeMin, float lifeMax);

    // Envejece y mueve las particulas; quita las que ya se apagaron.
    void update(float dt);

    // Dibuja cada particula como un disco brillante con mezcla aditiva.
    void draw(Framebuffer& fb, const Uniforms& u) const;

    int count() const { return (int)particles.size(); }

private:
    struct Particle {
        Vec3 position, velocity;
        float age, life;
        float sizeScale;
    };
    std::vector<Particle> particles;
    std::mt19937 rng{7};
    float emitCarry = 0.0f; // fraccion de particula que quedo pendiente del cuadro anterior
};
