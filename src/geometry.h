#pragma once
#include <functional>
#include "mesh.h"

// Esfera de radio 1 generada a partir de un icosaedro (20 triangulos).
// Cada subdivision parte cada triangulo en 4 y empuja los vertices nuevos
// a la superficie de la esfera: 20 * 4^n triangulos.
Mesh makeIcosphere(int subdivisions, const Material& material);

// Deforma una esfera unitaria: cada vertice se mueve sobre su direccion hasta
// la distancia shape(direccion) del centro (1 = sin cambio). Luego recalcula
// las normales promediando las caras que tocan cada vertice.
void displaceSphere(Mesh& mesh, const std::function<float(const Vec3&)>& shape);

// Agrega un anillo plano (en el plano XZ, normal +Y) entre los radios inner
// y outer, con "segments" divisiones alrededor. Se dibuja sin descarte de
// caras traseras para verlo desde arriba y desde abajo.
void addRing(Mesh& mesh, float inner, float outer, int segments, int material);
