#pragma once
#include "math3d.h"

// Ruido procedural para generar superficies. Con la misma semilla siempre da
// el mismo resultado, asi los planetas se ven igual cada vez que se corre.
class Noise {
public:
    explicit Noise(unsigned seed);

    // Ruido de gradiente (Perlin) en 3D. Devuelve aprox. -1..1.
    float perlin(const Vec3& p) const;

    // Suma de varias octavas de Perlin (fractal): cada octava tiene el
    // doble de frecuencia y la mitad de amplitud. Devuelve aprox. -1..1.
    float fbm(const Vec3& p, int octaves) const;

    // Ruido celular (Worley): distancia al punto aleatorio mas cercano, con
    // un punto por celda de la cuadricula. Sirve para crateres y celdas.
    float cellular(const Vec3& p) const;

    // Igual, pero tambien devuelve la distancia al segundo punto mas cercano.
    // Donde f2 - f1 es casi 0 el punto esta en el borde entre dos celdas:
    // asi se dibujan grietas entre placas.
    void cellular2(const Vec3& p, float& f1, float& f2) const;

private:
    int perm[512]; // permutacion duplicada para no tener que hacer & 255 dos veces
    float grad(int hash, float x, float y, float z) const;
    Vec3 cellPoint(int x, int y, int z) const;
};
