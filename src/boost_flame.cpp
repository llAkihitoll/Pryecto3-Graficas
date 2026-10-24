#include "boost_flame.h"
#include "mesh_builder.h"

Mesh makeFlame(Vec3 tint) {
    MeshBuilder b;
    int mat = b.material("Llama", tint, 0.0f, 1.0f, 1.0f);
    // rotateX(90 grados) lleva el eje Y del cono al eje Z del carro: la
    // llama sale hacia atras.
    b.frustum(12, 1.0f, 0.0f, 0.0f, 1.0f, mat, rotateX(PI / 2));
    return b.mesh;
}
