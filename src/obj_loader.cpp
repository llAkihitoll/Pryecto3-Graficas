#include "obj_loader.h"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// Las texturas del Fennec vienen de Unreal, que guarda la y de las normales
// con la convencion de DirectX (al reves que OpenGL/Blender).
const bool NORMAL_MAPS_FLIP_GREEN = true;

// Lee los colores Kd y los mapas de normales (norm o map_Bump) de cada
// material. Los nombres pueden tener espacios (por ejemplo "Fennec - Body"),
// por eso se toma el resto de la linea.
static void loadMTL(const std::string& path, std::vector<Material>& materials) {
    std::ifstream file(path);
    std::string dir = path.substr(0, path.find_last_of("/\\") + 1);
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.rfind("newmtl ", 0) == 0) {
            materials.push_back(Material{trim(line.substr(7))});
        } else if (line.rfind("Kd ", 0) == 0 && !materials.empty()) {
            std::istringstream iss(line.substr(3));
            Vec3& c = materials.back().color;
            iss >> c.x >> c.y >> c.z;
        } else if ((line.rfind("norm ", 0) == 0 || line.rfind("map_Bump ", 0) == 0) && !materials.empty()) {
            std::string texFile = trim(line.substr(line.find(' ') + 1));
            materials.back().normalMap = loadNormalTexture(dir + texFile, NORMAL_MAPS_FLIP_GREEN);
        }
    }
}

// Convierte un indice del OBJ (empieza en 1, negativo = desde el final) a base 0.
static int fixIndex(long idx, size_t count) {
    return idx > 0 ? (int)idx - 1 : (int)count + (int)idx;
}

void computeTangents(Mesh& mesh) {
    // Para cada triangulo se busca la direccion en que crecen u (tangente) y
    // v (bitangente) sobre su superficie: son las T y B que cumplen
    //   e1 = du1 * T + dv1 * B,   e2 = du2 * T + dv2 * B
    // (e1, e2 = aristas; du, dv = diferencias de UV). Se acumulan en los
    // vertices para suavizarlas entre triangulos vecinos.
    std::vector<Vec3> tan(mesh.vertices.size()), bitan(mesh.vertices.size());
    for (const Triangle& t : mesh.triangles) {
        const Vertex& a = mesh.vertices[t.a];
        const Vertex& b = mesh.vertices[t.b];
        const Vertex& c = mesh.vertices[t.c];
        Vec3 e1 = b.position - a.position, e2 = c.position - a.position;
        float du1 = b.uv.x - a.uv.x, dv1 = b.uv.y - a.uv.y;
        float du2 = c.uv.x - a.uv.x, dv2 = c.uv.y - a.uv.y;
        float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue; // UV degeneradas
        float r = 1.0f / det;
        Vec3 T = (e1 * dv2 - e2 * dv1) * r;
        Vec3 B = (e2 * du1 - e1 * du2) * r;
        for (int idx : {t.a, t.b, t.c}) {
            tan[idx] += T;
            bitan[idx] += B;
        }
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vertex& v = mesh.vertices[i];
        // Gram-Schmidt: se quita la parte de T que va en la direccion de la
        // normal, para que la base (T, B, N) quede perpendicular.
        v.tangent = normalize(tan[i] - v.normal * dot(v.normal, tan[i]));
        // Signo: si cross(N, T) apunta al reves que B, las UV estan espejadas.
        v.tangentSign = dot(cross(v.normal, v.tangent), bitan[i]) < 0.0f ? -1.0f : 1.0f;
    }
}

bool loadOBJ(const std::string& path, Mesh& mesh) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    std::string dir = path.substr(0, path.find_last_of("/\\") + 1);

    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    // Cada combinacion (posicion, uv, normal) distinta se vuelve un Vertex.
    std::unordered_map<uint64_t, int> vertexIndex;
    int currentMaterial = -1;
    bool missingNormals = false;

    const char* p = text.c_str();
    while (*p) {
        const char* lineEnd = std::strchr(p, '\n');
        if (!lineEnd) lineEnd = p + std::strlen(p);
        std::string line(p, lineEnd);
        p = *lineEnd ? lineEnd + 1 : lineEnd;

        const char* s = line.c_str();
        if (s[0] == 'v' && s[1] == ' ') {
            char* end;
            Vec3 v;
            v.x = std::strtof(s + 2, &end);
            v.y = std::strtof(end, &end);
            v.z = std::strtof(end, &end);
            positions.push_back(v);
        } else if (s[0] == 'v' && s[1] == 'n' && s[2] == ' ') {
            char* end;
            Vec3 n;
            n.x = std::strtof(s + 3, &end);
            n.y = std::strtof(end, &end);
            n.z = std::strtof(end, &end);
            normals.push_back(n);
        } else if (s[0] == 'v' && s[1] == 't' && s[2] == ' ') {
            char* end;
            Vec2 uv;
            uv.x = std::strtof(s + 3, &end);
            uv.y = std::strtof(end, &end);
            uvs.push_back(uv);
        } else if (s[0] == 'f' && s[1] == ' ') {
            std::vector<int> face;
            const char* t = s + 2;
            while (*t) {
                while (*t == ' ' || *t == '\t' || *t == '\r') ++t;
                if (!*t) break;
                char* end;
                long vi = std::strtol(t, &end, 10);
                long ti = 0, ni = 0;
                t = end;
                if (*t == '/') {
                    ++t;
                    if (*t != '/') {
                        ti = std::strtol(t, &end, 10);
                        t = end;
                    }
                    if (*t == '/') {
                        ++t;
                        ni = std::strtol(t, &end, 10);
                        t = end;
                    }
                }
                while (*t && *t != ' ' && *t != '\t') ++t;

                int pv = fixIndex(vi, positions.size());
                int pn = ni != 0 ? fixIndex(ni, normals.size()) : -1;
                int pt = ti != 0 ? fixIndex(ti, uvs.size()) : -1;
                if (pv < 0 || pv >= (int)positions.size()) continue;
                if (pn >= (int)normals.size()) pn = -1;
                if (pt >= (int)uvs.size()) pt = -1;
                if (pn < 0) missingNormals = true;

                // 21 bits por indice alcanzan para mas de 2 millones de cada uno.
                uint64_t key = ((uint64_t)(uint32_t)pv << 42) | ((uint64_t)(uint32_t)(pt + 1) << 21) |
                               (uint64_t)(uint32_t)(pn + 1);
                auto it = vertexIndex.find(key);
                if (it == vertexIndex.end()) {
                    Vertex vert{positions[pv], pn >= 0 ? normals[pn] : Vec3{0, 0, 0}};
                    if (pt >= 0) vert.uv = uvs[pt];
                    it = vertexIndex.emplace(key, (int)mesh.vertices.size()).first;
                    mesh.vertices.push_back(vert);
                }
                face.push_back(it->second);
            }
            // Abanico: (0,1,2), (0,2,3), ...
            for (size_t k = 1; k + 1 < face.size(); ++k) {
                mesh.triangles.push_back(Triangle{face[0], face[k], face[k + 1], currentMaterial});
            }
        } else if (line.rfind("usemtl ", 0) == 0) {
            std::string name = trim(line.substr(7));
            currentMaterial = -1;
            for (size_t i = 0; i < mesh.materials.size(); ++i) {
                if (mesh.materials[i].name == name) currentMaterial = (int)i;
            }
            if (currentMaterial < 0) {
                currentMaterial = (int)mesh.materials.size();
                mesh.materials.push_back(Material{name});
            }
        } else if (line.rfind("mtllib ", 0) == 0) {
            loadMTL(dir + trim(line.substr(7)), mesh.materials);
        }
    }

    // Material por defecto para caras sin usemtl.
    for (Triangle& t : mesh.triangles) {
        if (t.material < 0) {
            if (mesh.materials.empty() || mesh.materials.back().name != "Default") {
                mesh.materials.push_back(Material{"Default"});
            }
            t.material = (int)mesh.materials.size() - 1;
        }
    }

    if (missingNormals) {
        // Normal suave: suma de las normales de las caras que tocan al vertice.
        for (Vertex& v : mesh.vertices) v.normal = Vec3{0, 0, 0};
        for (const Triangle& t : mesh.triangles) {
            Vec3& a = mesh.vertices[t.a].position;
            Vec3& b = mesh.vertices[t.b].position;
            Vec3& c = mesh.vertices[t.c].position;
            Vec3 n = cross(b - a, c - a);
            mesh.vertices[t.a].normal += n;
            mesh.vertices[t.b].normal += n;
            mesh.vertices[t.c].normal += n;
        }
        for (Vertex& v : mesh.vertices) v.normal = normalize(v.normal);
    }
    if (!uvs.empty()) computeTangents(mesh);
    return !mesh.triangles.empty();
}
