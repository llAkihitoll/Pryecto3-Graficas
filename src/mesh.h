#pragma once
#include <string>
#include <vector>
#include "math3d.h"

struct Vertex {
    Vec3 position;
    Vec3 normal;
};

// Indices (base 0) de los tres vertices y el material que le toca.
struct Triangle {
    int a, b, c;
    int material;
};

struct Material {
    std::string name;
    Vec3 color{0.8f, 0.8f, 0.8f}; // Kd del .mtl, en 0..1
    float specular = 0.3f;         // intensidad del brillo
    float shininess = 32.0f;       // que tan concentrado es el brillo
    float emissive = 0.0f;         // 1 = brilla con luz propia (faros)
};

// Malla de triangulos lista para el pipeline. La usan tanto los modelos
// cargados de OBJ como las esferas generadas en codigo.
struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    std::vector<Material> materials;

    // Radio de la esfera (centrada en el origen del modelo) que lo contiene.
    float boundingRadius() const;
};
