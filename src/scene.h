#pragma once
#include <string>
#include <vector>
#include "planets.h"
#include "renderer.h"

// Un sol o un planeta. Su posicion y rotacion se calculan directamente a
// partir del tiempo transcurrido (no se suma un angulo fijo por cuadro),
// asi se mueven igual sin importar los FPS.
struct CelestialBody {
    std::string name;
    Mesh mesh;              // esfera unitaria, se escala con el radio
    FragmentShader shader;
    PlanetTextures textures; // superficie horneada al arrancar
    float radius = 1.0f;
    float orbitRadius = 0.0f; // 0 = se queda en el centro (el sol)
    float orbitPeriod = 0.0f; // segundos para dar una vuelta al sol
    float orbitPhase = 0.0f;  // angulo inicial en la orbita (radianes)
    float spinPeriod = 0.0f;  // segundos para dar una vuelta sobre su eje
    float axialTilt = 0.0f;   // inclinacion del eje de rotacion (radianes)

    Vec3 position(float time) const;
    Vec3 velocity(float time) const; // derivada de position(): para la colision
    Mat4 modelMatrix(float time) const;
};

// Estrella del fondo: solo una direccion, porque esta "infinitamente" lejos.
struct Star {
    Vec3 direction;
    float brightness;
};

struct Scene {
    std::vector<CelestialBody> bodies;
    std::vector<Star> stars;

    // Crea el sol, los planetas y las estrellas (con semilla fija).
    void build();

    // El sol es el primer cuerpo y tambien la fuente de luz.
    Vec3 sunPosition(float time) const { return bodies[0].position(time); }

    // Las estrellas se dibujan primero, como fondo: solo se aplica la
    // rotacion de la vista (sin traslacion) y la proyeccion.
    void drawStars(Framebuffer& fb, const Uniforms& u) const;
    void drawBodies(Framebuffer& fb, Uniforms& u, RenderStats& stats) const;
};
