#pragma once
#include "cubemap.h"
#include "framebuffer.h"
#include "renderer.h"

// Fondo del universo: un cubemap horneado una sola vez al arrancar con la Via
// Lactea (franja con nubes de estrellas y polvo oscuro), nebulosas de colores,
// galaxias espirales lejanas y polvo de estrellas tenue. Las estrellas
// nitidas se dibujan aparte como puntos (Scene::drawStars).
// Colores del cielo. Los valores por defecto son los del universo normal; el
// universo alterno usa otra paleta (mas calida, con nebulosas doradas y rojas).
struct SkyPalette {
    Vec3 background{0.012f, 0.014f, 0.035f}; // fondo casi negro, un poco azulado
    Vec3 band{0.32f, 0.3f, 0.36f};            // brillo de la Via Lactea
    Vec3 bandNormal{0.3f, 0.85f, -0.4f};      // orientacion de su plano
    Vec3 nebulae[3] = {{0.9f, 0.25f, 0.6f}, {0.15f, 0.75f, 0.7f}, {0.4f, 0.35f, 1.0f}}; // magenta, turquesa, violeta
    Vec3 armA{0.55f, 0.65f, 1.0f}, armB{0.9f, 0.7f, 1.0f}; // colores de los brazos de las galaxias
};

struct Skybox {
    CubeMap<Vec3> sky;

    void bake(unsigned seed, int size, const SkyPalette& palette = SkyPalette{});

    // Pinta todo el framebuffer con el cielo: a cada pixel le calcula la
    // direccion en la que mira la camara (deshaciendo la proyeccion y la
    // rotacion de la vista) y lee el cubemap en esa direccion. Solo depende
    // de hacia donde mira la camara, no de donde esta: el fondo esta
    // "infinitamente" lejos. Se reparte en franjas entre los hilos.
    void draw(Framebuffer& fb, const Uniforms& u) const;
};
