#pragma once
#include <string>
#include "framebuffer.h"
#include "renderer.h"

struct CelestialBody;

// Radios del disco de acrecion, en radios del horizonte.
const float DISK_INNER = 1.6f;
const float DISK_OUTER = 5.0f;

// Crea un agujero negro quieto en "position": el horizonte de eventos es una
// esfera negra y el disco de acrecion un anillo plano inclinado. diskColor es
// el color medio del gas (el centro siempre se ve casi blanco de tan caliente).
CelestialBody makeBlackHole(const std::string& name, const Vec3& position, float radius, const Vec3& diskColor);

// Dibuja el agujero negro en tres pasos:
//   1) el disco de acrecion con mezcla aditiva (el gas brilla sobre el fondo);
//   2) la lente gravitacional: deforma lo que ya esta dibujado detras;
//   3) el horizonte de eventos (negro, con el anillo de fotones en el borde).
void drawBlackHole(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& hole, bool cullObjects);

// Lente gravitacional en espacio de pantalla. La luz que pasa cerca del
// agujero se dobla hacia el: un pixel a distancia r del centro (en pantalla)
// muestra lo que en realidad esta a r - E^2 / r, donde E es el radio de
// Einstein. Cerca de E se ve el fondo de justo detras estirado en un anillo,
// y mas adentro aparece la imagen del otro lado (invertida). Solo se deforman
// los pixeles que estan mas lejos que el agujero (usa la profundidad), asi lo
// que esta delante no se dobla.
void applyLensing(Framebuffer& fb, const Uniforms& u, const Vec3& center, float radius);

// El carro (una esfera de radio carRadius en "point") toca el centro negro
// del agujero (su horizonte de eventos): eso lo lleva al otro universo.
bool touchesBlackHole(const CelestialBody& hole, float time, const Vec3& point, float carRadius);
