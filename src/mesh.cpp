#include "mesh.h"

float Mesh::boundingRadius() const {
    float r2 = 0;
    for (const Vertex& v : vertices) {
        float d = dot(v.position, v.position);
        if (d > r2) r2 = d;
    }
    return std::sqrt(r2);
}
