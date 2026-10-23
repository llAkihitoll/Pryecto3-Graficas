#include "geometry.h"
#include <algorithm>
#include <cstdint>
#include <unordered_map>

// Devuelve el indice del punto medio de la arista (a, b), creandolo si es
// la primera vez. Dos triangulos vecinos comparten la arista, asi que el
// cache evita vertices repetidos y grietas en la malla.
static int midpoint(int a, int b, Mesh& mesh, std::unordered_map<uint64_t, int>& cache) {
    uint64_t key = a < b ? ((uint64_t)a << 32) | (uint32_t)b : ((uint64_t)b << 32) | (uint32_t)a;
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    Vec3 p = normalize((mesh.vertices[a].position + mesh.vertices[b].position) * 0.5f);
    mesh.vertices.push_back(Vertex{p, p}); // en una esfera unitaria la normal es la posicion
    int index = (int)mesh.vertices.size() - 1;
    cache[key] = index;
    return index;
}

Mesh makeIcosphere(int subdivisions, const Material& material) {
    Mesh mesh;
    mesh.materials.push_back(material);

    // Los 12 vertices del icosaedro estan en 3 rectangulos aureos perpendiculares.
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const Vec3 base[12] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
                           {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
                           {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (const Vec3& v : base) {
        Vec3 p = normalize(v);
        mesh.vertices.push_back(Vertex{p, p});
    }

    const int faces[20][3] = {{0, 11, 5}, {0, 5, 1},  {0, 1, 7},   {0, 7, 10}, {0, 10, 11},
                              {1, 5, 9},  {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
                              {3, 9, 4},  {3, 4, 2},  {3, 2, 6},   {3, 6, 8},  {3, 8, 9},
                              {4, 9, 5},  {2, 4, 11}, {6, 2, 10},  {8, 6, 7},  {9, 8, 1}};
    for (const auto& f : faces) mesh.triangles.push_back(Triangle{f[0], f[1], f[2], 0});

    for (int level = 0; level < subdivisions; ++level) {
        std::unordered_map<uint64_t, int> cache;
        std::vector<Triangle> next;
        next.reserve(mesh.triangles.size() * 4);
        for (const Triangle& tri : mesh.triangles) {
            int ab = midpoint(tri.a, tri.b, mesh, cache);
            int bc = midpoint(tri.b, tri.c, mesh, cache);
            int ca = midpoint(tri.c, tri.a, mesh, cache);
            next.push_back(Triangle{tri.a, ab, ca, 0});
            next.push_back(Triangle{tri.b, bc, ab, 0});
            next.push_back(Triangle{tri.c, ca, bc, 0});
            next.push_back(Triangle{ab, bc, ca, 0});
        }
        mesh.triangles.swap(next);
    }

    // Las caras de enfrente deben ir en sentido antihorario vistas desde
    // afuera (si no, el backface culling se comeria la esfera).
    for (Triangle& tri : mesh.triangles) {
        const Vec3& a = mesh.vertices[tri.a].position;
        const Vec3& b = mesh.vertices[tri.b].position;
        const Vec3& c = mesh.vertices[tri.c].position;
        if (dot(cross(b - a, c - a), a + b + c) < 0) std::swap(tri.b, tri.c);
    }
    return mesh;
}
