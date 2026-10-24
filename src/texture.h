#pragma once
#include <memory>
#include <string>
#include <vector>
#include "math3d.h"

// Mapa de normales en espacio tangente: cada texel guarda una direccion
// relativa a la superficie (x = tangente, y = bitangente, z = normal).
struct NormalTexture {
    int width = 0, height = 0;
    std::vector<Vec3> texels; // ya convertidos a vectores (-1..1)

    // Lectura bilineal con repeticion (las UV pueden salirse de 0..1).
    // v = 0 es la fila de abajo de la imagen (convencion OBJ).
    Vec3 sample(float u, float v) const;
};

// Carga un PNG de 8 bits (RGB o RGBA, sin entrelazar) como mapa de normales.
// Detecta dos formatos:
//   - normal: x = R, y = G, z = B;
//   - empaquetado (texturas de Unreal): x = alfa, y = verde y z se
//     reconstruye con sqrt(1 - x^2 - y^2). Se reconoce porque el alfa no es
//     casi todo 255.
// flipGreen invierte y (las texturas de Unreal usan la convencion de DirectX).
// Las texturas cargadas se guardan en cache por ruta.
std::shared_ptr<NormalTexture> loadNormalTexture(const std::string& path, bool flipGreen);
