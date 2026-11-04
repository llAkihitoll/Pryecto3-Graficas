#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "mesh.h"
#include "renderer.h"

// Estilo de cubos (voxeles) del universo alterno: planetas, la estrella
// Albor, estrellas de decoracion y galaxias hechas de cubos.

// Lo que lleva el cubo de la superficie en una direccion del planeta.
struct VoxelCell {
    Vec3 color;
    int layers = 0;        // capas de cubos extra encima de la esfera (relieve: 0, 1 o 2)
    float emissive = 0.0f; // 1 = brilla con luz propia
};

// Planeta de cubos alrededor de una esfera de radio 1 (se escala con la
// matriz del cuerpo). "across" = cuantos cubos caben de lado a lado. Una
// celda de la rejilla es solida si su centro queda dentro de la esfera mas
// sus capas de relieve; solo se generan las caras que dan a una celda vacia
// (las de adentro nunca se ven). Los colores se agrupan en materiales.
// carve (opcional) quita las celdas donde devuelve true, a cualquier
// profundidad (la boca de Pac-Man); los cubos de adentro que quedan a la vista
// toman el color de la superficie en su direccion.
Mesh makeVoxelPlanet(int across, const std::function<VoxelCell(const Vec3&)>& cell,
                     const std::function<bool(const Vec3&)>& carve = nullptr);

// Cubos con cualquier forma: una celda (centro c, en la misma escala que la
// esfera unitaria) es solida si solidAt(c); cellAt(c) da el color de las que
// tienen alguna cara a la vista. La rejilla llega hasta "extent" en cada eje.
// Lo usa el planeta con forma de nave.
Mesh makeVoxelSolid(int across, float extent, const std::function<bool(const Vec3&)>& solidAt,
                    const std::function<VoxelCell(const Vec3&)>& cellAt);

// Sprite de pixel art convertido en cubos: cada caracter de "rows" con color
// en la paleta es un cubo de "pixel" de lado y "layers" pisos de alto, sobre
// el plano XZ (la primera fila hacia -Z; flip la pone hacia +Z). Centrado en
// el origen. Los personajes de Pac-Man y Galaga.
Mesh makeVoxelSprite(const std::vector<std::string>& rows, const std::map<char, Vec3>& palette, float pixel,
                     int layers, bool flip = false);

// Estrellas de decoracion: cubos brillantes repartidos en un cascaron lejano
// alrededor del sistema, con colores de fantasia (lila, rosa, violeta, blanco).
Mesh makeVoxelStars(unsigned seed, int count, float minDistance, float maxDistance);

// Galaxia espiral de cubos en el plano XZ: un nucleo brillante y dos brazos
// en espiral logaritmica. Los cubos del centro son mas grandes y claros.
Mesh makeVoxelGalaxy(unsigned seed, int count, float radius, const Vec3& coreColor, const Vec3& armColor);

// Cubos iluminados: Lambert con luz ambiente lila, borde violeta, brillo propio segun el
// material y orillas oscuras en cada cubo (se calculan con la posicion local
// y el tamano del cubo del material).
Vec3 voxelShader(const Fragment& f, const Uniforms& u);

// Cubos que brillan solos (Albor, estrellas y galaxias): el color del
// material con un latido distinto por cubo, y las orillas mas oscuras.
Vec3 voxelGlowShader(const Fragment& f, const Uniforms& u);
