#pragma once
#include <cmath>
#include <thread>
#include <vector>
#include "math3d.h"

// Textura guardada en las 6 caras de un cubo. Se consulta con una direccion
// (por ejemplo, el punto de la esfera unitaria): la cara se elige por el eje
// mas grande y las otras dos coordenadas dan la posicion dentro de la cara.
// A diferencia de una textura rectangular, no se estira en los polos.
//
// Caras: 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z, 5 = -Z. Dentro de cada cara
// s y t van de -1 a 1.
template <typename T>
struct CubeMap {
    int size = 0;
    std::vector<T> texels; // 6 * size * size, cara por cara, fila por fila

    // Direccion que corresponde a (s, t) en una cara (inverso de sample()).
    static Vec3 direction(int face, float s, float t) {
        switch (face) {
            case 0: return {1, t, -s};
            case 1: return {-1, t, s};
            case 2: return {s, 1, -t};
            case 3: return {s, -1, t};
            case 4: return {s, t, 1};
            default: return {-s, t, -1};
        }
    }

    // Llena cada texel con f(direccion unitaria). Cada cara se calcula en su
    // propio hilo porque f suele ser caro (varias octavas de ruido).
    template <typename F>
    void bake(int newSize, F f) {
        size = newSize;
        texels.assign((size_t)6 * size * size, T{});
        std::vector<std::thread> workers;
        for (int face = 0; face < 6; ++face) {
            workers.emplace_back([this, face, &f]() {
                for (int y = 0; y < size; ++y)
                    for (int x = 0; x < size; ++x) {
                        float s = (x + 0.5f) / size * 2.0f - 1.0f;
                        float t = (y + 0.5f) / size * 2.0f - 1.0f;
                        texels[((size_t)face * size + y) * size + x] = f(normalize(direction(face, s, t)));
                    }
            });
        }
        for (std::thread& w : workers) w.join();
    }

    // Lectura con interpolacion bilineal (promedio de los 4 texels vecinos),
    // para que de cerca no se vean los cuadros de la textura.
    T sample(const Vec3& d) const {
        float ax = std::fabs(d.x), ay = std::fabs(d.y), az = std::fabs(d.z);
        int face;
        float s, t;
        if (ax >= ay && ax >= az) {
            face = d.x > 0 ? 0 : 1;
            s = (d.x > 0 ? -d.z : d.z) / ax;
            t = d.y / ax;
        } else if (ay >= az) {
            face = d.y > 0 ? 2 : 3;
            s = d.x / ay;
            t = (d.y > 0 ? -d.z : d.z) / ay;
        } else {
            face = d.z > 0 ? 4 : 5;
            s = (d.z > 0 ? d.x : -d.x) / az;
            t = d.y / az;
        }

        float fx = clampf((s * 0.5f + 0.5f) * size - 0.5f, 0.0f, size - 1.001f);
        float fy = clampf((t * 0.5f + 0.5f) * size - 0.5f, 0.0f, size - 1.001f);
        int x0 = (int)fx, y0 = (int)fy;
        float tx = fx - x0, ty = fy - y0;
        const T* row0 = &texels[((size_t)face * size + y0) * size];
        const T* row1 = row0 + size;
        T top = row0[x0] * (1.0f - tx) + row0[x0 + 1] * tx;
        T bottom = row1[x0] * (1.0f - tx) + row1[x0 + 1] * tx;
        return top * (1.0f - ty) + bottom * ty;
    }
};
