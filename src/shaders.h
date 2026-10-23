#pragma once
#include "renderer.h"

// Shaders de fragmentos escritos para el pipeline de software. Cada uno
// recibe un pixel ya interpolado y devuelve su color (0..1).

// Blinn-Phong con el color del material, mas un borde azulado (Fresnel)
// para que la silueta del carro se distinga contra el espacio oscuro.
Vec3 carShader(const Fragment& f, const Uniforms& u);
