#include "voxels.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include "mesh_builder.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Numero pseudoaleatorio 0..1 a partir de las coordenadas de una celda.
static float hash3(int x, int y, int z) {
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)z * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

// Que tan cerca de una orilla del cubo esta el punto (1 = sobre la orilla).
// En una cara, la coordenada del eje de la cara cae justo en la rejilla; la
// segunda mas cercana a la rejilla dice que tan cerca esta de una arista.
static float voxelEdge(const Vec3& local, float size) {
    if (size <= 0.0f) return 0.0f;
    float d[3];
    const float q[3] = {local.x / size, local.y / size, local.z / size};
    for (int i = 0; i < 3; ++i) d[i] = std::fabs(q[i] - std::round(q[i]));
    std::sort(d, d + 3);
    return 1.0f - smoothstep(0.04f, 0.1f, d[1]);
}

Vec3 voxelShader(const Fragment& f, const Uniforms& u) {
    const Material& m = *f.material;
    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float diffuse = std::max(0.0f, dot(f.normal, toLight));
    Vec3 half = normalize(toLight + toCamera);
    float spec = m.specular * std::pow(std::max(0.0f, dot(f.normal, half)), m.shininess);
    // Luz ambiente lila (el cielo morado ilumina el lado de noche) y un borde
    // violeta en la silueta, como de fantasia.
    const Vec3 ambient{0.2f, 0.11f, 0.3f};
    float edge = 1.0f - std::max(0.0f, dot(f.normal, toCamera));
    Vec3 lit = m.color * (ambient + Vec3{0.84f, 0.84f, 0.84f} * diffuse) + Vec3{spec, spec, spec} +
               Vec3{0.55f, 0.25f, 0.9f} * (edge * edge * 0.35f);
    Vec3 color = lerp(lit, m.color, m.emissive);
    return color * (1.0f - 0.4f * voxelEdge(f.localPos, m.voxelSize));
}

Vec3 voxelGlowShader(const Fragment& f, const Uniforms& u) {
    const Material& m = *f.material;
    // Cada cubo late a su ritmo (fase segun la celda en la que esta).
    float cell = m.voxelSize > 0.0f ? m.voxelSize : 40.0f;
    float phase = hash3((int)std::floor(f.localPos.x / cell), (int)std::floor(f.localPos.y / cell),
                        (int)std::floor(f.localPos.z / cell));
    float pulse = 0.82f + 0.18f * std::sin(u.time * (1.5f + phase * 2.0f) + phase * 6.283f);
    return m.color * pulse * (1.0f - 0.35f * voxelEdge(f.localPos, m.voxelSize));
}

// Materiales agrupados por color: cada color (cuantizado) y nivel de brillo
// propio es un material, asi la malla no necesita colores por vertice.
struct MaterialTable {
    MeshBuilder& builder;
    float voxelSize;
    std::map<int, int> index;

    int get(const Vec3& c, float emissive) {
        int r = (int)(clampf(c.x, 0, 1) * 15.0f + 0.5f), g = (int)(clampf(c.y, 0, 1) * 15.0f + 0.5f);
        int b = (int)(clampf(c.z, 0, 1) * 15.0f + 0.5f), e = (int)(clampf(emissive, 0, 1) * 3.0f + 0.5f);
        int key = ((r * 16 + g) * 16 + b) * 4 + e;
        auto it = index.find(key);
        if (it != index.end()) return it->second;
        int id = builder.material("Cubo", Vec3{r / 15.0f, g / 15.0f, b / 15.0f}, 0.25f, 24.0f, e / 3.0f);
        builder.mesh.materials[id].voxelSize = voxelSize;
        index[key] = id;
        return id;
    }
};

Mesh makeVoxelPlanet(int across, const std::function<VoxelCell(const Vec3&)>& cellAt) {
    const float v = 2.0f / across; // tamano de cada cubo (la esfera mide 2)
    const int m = (int)std::ceil((1.0f + 2.5f * v) / v);
    const int n = 2 * m;
    // Estado de cada celda: 0 = sin calcular, 1 = vacia, 2 = solida.
    std::vector<char> state((size_t)n * n * n, 0);
    std::vector<VoxelCell> cells((size_t)n * n * n);
    auto center = [&](int i, int j, int k) { return Vec3{(i - m + 0.5f) * v, (j - m + 0.5f) * v, (k - m + 0.5f) * v}; };
    auto solid = [&](int i, int j, int k) -> bool {
        if (i < 0 || j < 0 || k < 0 || i >= n || j >= n || k >= n) return false;
        size_t id = ((size_t)i * n + j) * n + k;
        if (state[id] == 0) {
            Vec3 c = center(i, j, k);
            float r = length(c);
            if (r < 1.0f - 2.0f * v) state[id] = 2;      // muy adentro: solida, nunca se ve
            else if (r > 1.0f + 2.5f * v) state[id] = 1; // demasiado lejos
            else {
                VoxelCell cell = cellAt(c * (1.0f / r));
                cells[id] = cell;
                state[id] = r <= 1.0f + cell.layers * v ? 2 : 1;
            }
        }
        return state[id] == 2;
    };

    MeshBuilder b;
    MaterialTable table{b, v, {}};
    const int dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                if (!solid(i, j, k)) continue;
                Vec3 c = center(i, j, k);
                if (length(c) < 1.0f - 2.0f * v) continue; // interior
                size_t id = ((size_t)i * n + j) * n + k;
                // Los de abajo de una capa de relieve toman el color de la
                // superficie en su direccion.
                const VoxelCell& cell = cells[id];
                int mat = table.get(cell.color, cell.emissive);
                for (const auto& d : dirs) {
                    if (solid(i + d[0], j + d[1], k + d[2])) continue;
                    Vec3 nrm{(float)d[0], (float)d[1], (float)d[2]};
                    // Dos ejes de la cara (perpendiculares a la normal).
                    Vec3 a = d[0] != 0 ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
                    Vec3 bAxis = cross(nrm, a);
                    float h = v * 0.5f;
                    Vec3 fc = c + nrm * h;
                    Vec3 p0 = fc - a * h - bAxis * h, p1 = fc + a * h - bAxis * h;
                    Vec3 p2 = fc + a * h + bAxis * h, p3 = fc - a * h + bAxis * h;
                    b.triangle(p0, p1, p2, mat, c);
                    b.triangle(p0, p2, p3, mat, c);
                }
            }
    return b.mesh;
}

Mesh makeVoxelStars(unsigned seed, int count, float minDistance, float maxDistance) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    MeshBuilder b;
    const Vec3 colors[4] = {{0.85f, 0.7f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.65f, 0.9f}, {0.6f, 0.5f, 1.0f}};
    int mats[4];
    for (int i = 0; i < 4; ++i) mats[i] = b.material("Estrella", colors[i], 0.0f, 1.0f, 1.0f);
    for (int s = 0; s < count; ++s) {
        float z = unit(rng) * 2.0f - 1.0f, a = unit(rng) * 2.0f * PI, r = std::sqrt(1.0f - z * z);
        Vec3 dir{r * std::cos(a), r * std::sin(a), z};
        float dist = minDistance + (maxDistance - minDistance) * unit(rng);
        // Muchas pequenas y pocas grandes.
        float size = 14.0f + 34.0f * std::pow(unit(rng), 3.0f);
        Vec3 axis = normalize(Vec3{unit(rng) - 0.5f, unit(rng) - 0.5f, unit(rng) - 0.5f});
        Mat4 xf = translate(dir * dist) * rotateAxis(axis, unit(rng) * PI);
        // frustum de 4 lados = un prisma cuadrado: con radio r mide r*sqrt(2) de lado.
        float half = size * 0.5f;
        b.frustum(4, half * 1.4142f, half * 1.4142f, -half, half, mats[(int)(unit(rng) * 4.0f) % 4], xf);
    }
    return b.mesh;
}

Mesh makeVoxelGalaxy(unsigned seed, int count, float radius, const Vec3& coreColor, const Vec3& armColor) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    MeshBuilder b;
    // Rampa de colores del centro a la orilla (cuatro materiales).
    int mats[4];
    for (int i = 0; i < 4; ++i) mats[i] = b.material("Galaxia", lerp(coreColor, armColor, i / 3.0f), 0.0f, 1.0f, 1.0f);
    for (int s = 0; s < count; ++s) {
        Vec3 p;
        float t;
        if (s < count / 5) { // nucleo
            p = Vec3{gauss(rng), gauss(rng) * 0.5f, gauss(rng)} * (radius * 0.09f);
            t = 0.0f;
        } else { // brazos: espiral logaritmica con un poco de dispersion
            t = std::pow(unit(rng), 0.8f);
            float r = radius * (0.12f + 0.88f * t);
            float arm = (s % 2) * PI;
            float angle = arm + std::log(r / (radius * 0.12f)) * 2.2f + gauss(rng) * 0.18f;
            p = Vec3{r * std::cos(angle), gauss(rng) * radius * 0.02f, r * std::sin(angle)};
            p += Vec3{gauss(rng), 0.0f, gauss(rng)} * (radius * 0.04f);
        }
        float size = (radius * 0.035f) * (1.0f - 0.55f * t) * (0.6f + 0.6f * unit(rng));
        int mat = mats[std::min(3, (int)(t * 4.0f))];
        float h = size * 0.5f;
        // Cubos alineados al plano de la galaxia: se ve como un mosaico.
        b.frustum(4, h * 1.4142f, h * 1.4142f, -h, h, mat, translate(p) * rotateY(PI * 0.25f));
    }
    return b.mesh;
}
