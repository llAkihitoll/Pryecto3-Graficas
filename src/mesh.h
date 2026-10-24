#pragma once
#include <memory>
#include <string>
#include <vector>
#include "math3d.h"
#include "texture.h"

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;                  // coordenadas de textura (solo modelos con mapa de normales)
    Vec3 tangent;             // direccion en que crece u sobre la superficie
    float tangentSign = 1.0f; // -1 si las UV estan espejadas (bitangente invertida)
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
    std::shared_ptr<NormalTexture> normalMap; // mapa de normales en espacio tangente (o nada)
    float normalStrength = 1.0f;   // que tan marcado se ve el relieve del mapa
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
