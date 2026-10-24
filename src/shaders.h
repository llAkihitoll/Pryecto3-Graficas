#pragma once
#include "renderer.h"

// Shaders de fragmentos escritos para el pipeline de software. Cada uno
// recibe un pixel ya interpolado y devuelve su color (0..1).

// Blinn-Phong con el color del material, mas un borde azulado (Fresnel)
// para que la silueta del carro se distinga contra el espacio oscuro. Si el
// material tiene mapa de normales (chasis, rines, llantas) usa la normal del
// mapa en espacio tangente.
Vec3 carShader(const Fragment& f, const Uniforms& u);

// Sol (shader animado): granulos de plasma que se encienden y apagan con el
// tiempo, cada zona con su propio ritmo, manchas oscuras, borde mas oscuro
// (limbo) y una paleta de rojo a blanco segun la "temperatura".
Vec3 sunShader(const Fragment& f, const Uniforms& u);

// Duna: lee el color y la normal con relieve de su cubemap (giran con el
// planeta), ilumina con el sol, suaviza la linea dia/noche, agrega una luz
// de relleno azulada en el lado de noche y una bruma de atmosfera en el borde.
Vec3 duneShader(const Fragment& f, const Uniforms& u);

// Glacia: hielo con reflejo especular muy concentrado, borde azulado
// (Fresnel) y grietas que emiten luz propia, mas visibles de noche.
Vec3 glaciaShader(const Fragment& f, const Uniforms& u);

// Vergel: tierra con relieve, el sol se refleja solo en el agua, una capa de
// nubes que se mueve sobre la superficie y atmosfera azul en el borde.
Vec3 vergelShader(const Fragment& f, const Uniforms& u);

// Aurelia: gigante gaseoso. Rotacion diferencial: cada latitud gira a su
// propia velocidad (la textura se lee girada segun el tiempo), asi las
// bandas se deslizan entre si. Oscurecimiento del borde y bruma calida.
Vec3 aureliaShader(const Fragment& f, const Uniforms& u);

// Anillos de Aurelia: vetas finas segun la distancia al centro, iluminados
// por las dos caras y con la sombra del planeta (rayo hacia el sol contra la
// esfera del planeta).
Vec3 ringShader(const Fragment& f, const Uniforms& u);

// Ignis: roca volcanica con relieve y lava con luz propia; pulsos de calor
// que avanzan por la lava con el tiempo, y humo anaranjado en el borde.
Vec3 ignisShader(const Fragment& f, const Uniforms& u);

// Objetos de superficie: Lambert + brillo con el color del material, luz
// propia (cristales, la luz de la baliza) y sombra del planeta: si el sol
// esta debajo del horizonte del objeto, no lo ilumina.
Vec3 propShader(const Fragment& f, const Uniforms& u);
