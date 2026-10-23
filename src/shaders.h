#pragma once
#include "renderer.h"

// Shaders de fragmentos escritos para el pipeline de software. Cada uno
// recibe un pixel ya interpolado y devuelve su color (0..1).

// Blinn-Phong con el color del material, mas un borde azulado (Fresnel)
// para que la silueta del carro se distinga contra el espacio oscuro.
Vec3 carShader(const Fragment& f, const Uniforms& u);

// Sol: no lo ilumina nadie, brilla solo. Mas claro al centro del disco y
// anaranjado en el borde (oscurecimiento del limbo).
Vec3 sunShader(const Fragment& f, const Uniforms& u);

// Planeta: difuso iluminado por el sol con una luz ambiente muy baja, asi
// se ve claramente el lado de dia y el de noche.
Vec3 planetShader(const Fragment& f, const Uniforms& u);
