#pragma once
#include <random>
#include <vector>
#include "mesh.h"
#include "particles.h"
#include "renderer.h"
#include "ship.h"

// Cinturon de asteroides entre las orbitas de Glacia y Vergel. Cada roca
// orbita al sol (tambien con la ley de Kepler) y gira sobre si misma. El carro
// las puede chocar: la roca sale disparada, vuela libre (la gravedad de los
// cuerpos la curva) y si llega a un planeta o al sol explota. Despues de unos
// segundos reaparece en el cinturon.
class AsteroidBelt {
public:
    // Reparte "count" rocas con una semilla fija (siempre el mismo cinturon).
    // rockModels son las variantes de roca que se usan como modelo.
    void build(unsigned seed, const std::vector<Mesh>& rockModels, int count);

    // Las del cinturon siguen su orbita (calculada con el tiempo); las
    // golpeadas vuelan libres con la gravedad de los cuerpos. Si chocan con
    // uno, explotan (rafaga de particulas en el punto de impacto).
    void update(float time, float dt, const std::vector<SphereCollider>& bodies, Particles& particles);

    // Choque esfera contra esfera con el carro: empuja la roca segun la
    // velocidad con que la golpeo y frena un poco al carro. Devuelve cuantas golpeo.
    int collideShip(Ship& ship);

    // Junta todas las rocas visibles en una sola malla y la dibuja de una vez.
    // Las que no se ven se descartan y las muy lejanas se dibujan como un punto.
    void draw(Framebuffer& fb, Uniforms& u, RenderStats& stats, const Optimizations& opt);

    // Solo para pruebas (--test-impact): lanza la primera roca desde "from"
    // con esa velocidad, como si el carro la hubiera golpeado.
    void launchForTest(const Vec3& from, const Vec3& velocity);

    int total() const { return (int)asteroids.size(); }
    int flying() const;
    int explosions() const { return explosionCount; }

private:
    enum class State { Belt, Flying, Gone };
    struct Asteroid {
        int model;
        float size;
        // Orbita en el cinturon
        float orbitRadius, phase, omega, height;
        // Giro sobre si misma
        Vec3 spinAxis;
        float spinRate, spinAngle;
        // Estado actual
        State state;
        Vec3 position, velocity;
        float timer; // tiempo volando o tiempo para reaparecer
    };

    std::vector<Asteroid> asteroids;
    const std::vector<Mesh>* models = nullptr;
    std::mt19937 rng;
    Mesh batch; // todas las rocas visibles del cuadro, ya en coordenadas del mundo
    int explosionCount = 0;

    void placeInBelt(Asteroid& a);
    Vec3 beltPosition(const Asteroid& a, float time) const;
    Vec3 beltVelocity(const Asteroid& a, float time) const;
    void explode(Asteroid& a, const SphereCollider& body, Particles& particles);
};
