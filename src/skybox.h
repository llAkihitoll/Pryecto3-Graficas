#pragma once
#include "cubemap.h"
#include "framebuffer.h"
#include "renderer.h"

// Fondo del universo: un cubemap horneado una sola vez al arrancar con la Via
// Lactea (franja con nubes de estrellas y polvo oscuro), nebulosas de colores,
// galaxias espirales lejanas y polvo de estrellas tenue. Las estrellas
// nitidas se dibujan aparte como puntos (Scene::drawStars).
struct Skybox {
    CubeMap<Vec3> sky;

    void bake(unsigned seed, int size);

    // Pinta todo el framebuffer con el cielo: a cada pixel le calcula la
    // direccion en la que mira la camara (deshaciendo la proyeccion y la
    // rotacion de la vista) y lee el cubemap en esa direccion. Solo depende
    // de hacia donde mira la camara, no de donde esta: el fondo esta
    // "infinitamente" lejos. Se reparte en franjas entre los hilos.
    void draw(Framebuffer& fb, const Uniforms& u) const;
};
