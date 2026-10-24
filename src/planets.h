#pragma once
#include "cubemap.h"
#include "noise.h"

// Texturas de un cuerpo celeste, calculadas una sola vez al arrancar. Los
// shaders solo las leen: evaluar varias octavas de ruido por pixel en cada
// cuadro seria demasiado lento para el renderizador por software.
struct PlanetTextures {
    CubeMap<Vec3> color;   // color de la superficie (en el sol: granulacion, fase, manchas)
    CubeMap<Vec3> normal;  // normal con relieve, en el espacio del modelo (el sol no la usa)
    CubeMap<float> mask;   // Glacia: brillo de las grietas. Vergel: 1 = agua
    CubeMap<float> clouds; // Vergel: densidad de nubes (0..1)
};

// Planeta desertico: tierras bajas rojizas, mesetas claras, crateres y dunas.
void bakeDune(PlanetTextures& tex, unsigned seed, int size);

// Glacia: placas de hielo separadas por grietas brillantes y escarcha polar.
void bakeGlacia(PlanetTextures& tex, unsigned seed, int size);

// Vergel: continentes verdes, oceanos, montanas nevadas y una capa de nubes.
void bakeVergel(PlanetTextures& tex, unsigned seed, int size);

// Consultas del terreno para decidir donde colocar objetos encima. Se les
// pasa un Noise creado con la misma semilla que se uso para hornear, asi
// coinciden con lo que se ve en la textura.
float duneBaseRelief(const Noise& terrain, const Vec3& p); // negativo = tierras bajas (dunas)
float duneCrater(const Noise& terrain, const Vec3& p);     // negativo = dentro de un crater grande
float glaciaCrack(const Noise& terrain, const Vec3& p);    // 1 = sobre una grieta grande
float vergelElevation(const Noise& terrain, const Vec3& p);
extern const float VERGEL_SEA_LEVEL;

// Sol: granulacion (celdas de conveccion), una fase por zona para animar
// el "hervor" y manchas oscuras.
void bakeSun(PlanetTextures& tex, unsigned seed, int size);
