#pragma once
#include "mesh.h"

// Kit espacial del Fennec, modelado en codigo por el equipo: dos propulsores
// traseros con tobera brillante, dos alerones en flecha y una aleta dorsal.
// Esta en las mismas coordenadas que el carro (frente hacia -Z, +Y arriba),
// asi que se dibuja con la misma matriz de modelo.
Mesh makeSpaceKit();

// Cono de la llama del propulsor: base de radio 1 en z = 0 y punta en z = 1.
// Se escala segun el empuje; tint es el color de la llama.
Mesh makeFlame(Vec3 tint);

// Donde estan las toberas de los propulsores (para colocar las llamas).
const Vec3 NOZZLE_LEFT{-0.30f, 0.03f, 0.805f};
const Vec3 NOZZLE_RIGHT{0.30f, 0.03f, 0.805f};
const float NOZZLE_RADIUS = 0.075f;
