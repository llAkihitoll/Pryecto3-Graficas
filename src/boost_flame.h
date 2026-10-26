#pragma once
#include "mesh.h"

// Boost como en Rocket League: una llama de cohete sale de cada uno de los dos
// escapes que ya tiene el modelo del Fennec (la estela usa Particles).

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
