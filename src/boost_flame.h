#pragma once
#include "mesh.h"

// Llama del boost, como en Rocket League: sale de los dos escapes que ya
// tiene el modelo del Fennec en la parte trasera.

// Cono de la llama: base de radio 1 en z = 0 y punta en z = 1. Se escala
// segun el empuje; tint es el color de la llama.
Mesh makeFlame(Vec3 tint);

// Escapes del Fennec, en las coordenadas del carro (frente hacia -Z). Se
// encontraron buscando en el modelo los anillos de las paredes de los tubos
// en la parte trasera: centro, radio y donde termina cada tubo.
const Vec3 EXHAUST_LEFT{-0.135f, 0.021f, 0.69f};
const Vec3 EXHAUST_RIGHT{0.135f, 0.021f, 0.69f};
const float EXHAUST_RADIUS = 0.045f;
