#pragma once
#include "mesh.h"

// Esfera de radio 1 generada a partir de un icosaedro (20 triangulos).
// Cada subdivision parte cada triangulo en 4 y empuja los vertices nuevos
// a la superficie de la esfera: 20 * 4^n triangulos.
Mesh makeIcosphere(int subdivisions, const Material& material);
