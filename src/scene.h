#pragma once
#include <string>
#include <vector>
#include "planets.h"
#include "props.h"
#include "renderer.h"

// Un sol o un planeta. Su posicion y rotacion se calculan directamente a
// partir del tiempo transcurrido (no se suma un angulo fijo por cuadro),
// asi se mueven igual sin importar los FPS.
struct CelestialBody {
    std::string name;
    // Niveles de detalle: la misma esfera unitaria con mas o menos
    // subdivisiones. lods[0] es la mas detallada.
    std::vector<Mesh> lods;
    std::vector<Mesh> sphereLods; // si esta deformado: las mismas esferas sin deformar (tecla G)
    mutable int currentLod = 0; // el que se uso en el ultimo cuadro (-1 = descartado)
    // Forma del planeta: distancia al centro (en radios) segun la direccion.
    // Vacia = esfera perfecta. Duna la usa para hundir los crateres.
    std::function<float(const Vec3&)> shape;
    FragmentShader shader;
    PlanetTextures textures; // superficie horneada al arrancar
    float radius = 1.0f;
    float orbitRadius = 0.0f; // 0 = se queda en el centro (el sol)
    float orbitPeriod = 0.0f; // segundos para dar una vuelta al sol
    float orbitPhase = 0.0f;  // angulo inicial en la orbita (radianes)
    float spinPeriod = 0.0f;  // segundos para dar una vuelta sobre su eje
    float axialTilt = 0.0f;   // inclinacion del eje de rotacion (radianes)
    float cloudPeriod = 0.0f; // segundos para que las nubes den una vuelta (0 = sin nubes)
    float surfaceGravity = 0.0f; // atraccion en la superficie (unidades/s^2)

    // Objetos sobre la superficie, ya unidos en una sola malla en el espacio
    // local del planeta (sin escala). Solo se dibujan cuando el planeta esta cerca.
    Mesh props;
    int propCount = 0;
    float propHeight = 0.0f; // lo mas alto que sobresalen, para el descarte

    // Crea los niveles de detalle (de 20480 a 320 triangulos) y, si hay
    // shape, los deforma.
    void buildLods(const Material& material);
    // Elige el nivel segun cuantos pixeles de radio ocupa en pantalla.
    int chooseLod(float screenRadius) const;

    Vec3 position(float time) const;
    Vec3 velocity(float time) const; // derivada de position(): para la colision
    // Posicion y giro del planeta (sin escala): lo usan los objetos de
    // superficie para girar junto con el.
    Mat4 frameMatrix(float time) const;
    float spinAngle(float time) const;
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

    // Modelos de superficie (se construyen una vez y se copian en cada planeta).
    std::vector<Mesh> rockModels;
    Mesh treeModel, crystalModel, beaconModel;
    unsigned propSeed = 0;
    bool deformTerrain = true; // tecla G: ver Duna deformada o como esfera

    // Crea el sol, los planetas, las estrellas y los objetos de superficie.
    void build(unsigned propSeed);

    // Vuelve a repartir los objetos de superficie con otra semilla (tecla N).
    void buildProps(unsigned seed);

    // El sol es el primer cuerpo y tambien la fuente de luz.
    Vec3 sunPosition(float time) const { return bodies[0].position(time); }

    // Las estrellas se dibujan primero, como fondo: solo se aplica la
    // rotacion de la vista (sin traslacion) y la proyeccion.
    void drawStars(Framebuffer& fb, const Uniforms& u) const;
    void drawBodies(Framebuffer& fb, Uniforms& u, RenderStats& stats, const Optimizations& opt) const;
};
