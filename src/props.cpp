#include "props.h"
#include <algorithm>
#include <map>
#include <random>
#include "geometry.h"

// ---------------------------------------------------------------- construccion de modelos

// Ayudante para armar modelos con caras planas: cada triangulo tiene sus
// propios vertices con la normal de la cara.
struct MeshBuilder {
    Mesh mesh;

    int material(const char* name, Vec3 color, float specular, float shininess, float emissive = 0.0f) {
        Material m{name, color};
        m.specular = specular;
        m.shininess = shininess;
        m.emissive = emissive;
        mesh.materials.push_back(m);
        return (int)mesh.materials.size() - 1;
    }

    // Agrega el triangulo mirando hacia afuera: si su normal apunta hacia el
    // punto "inside" (el centro de la pieza), se invierte el orden.
    void triangle(Vec3 a, Vec3 b, Vec3 c, int mat, const Vec3& inside) {
        Vec3 n = cross(b - a, c - a);
        if (dot(n, (a + b + c) * (1.0f / 3.0f) - inside) < 0) {
            std::swap(b, c);
            n = -n;
        }
        n = normalize(n);
        int base = (int)mesh.vertices.size();
        mesh.vertices.push_back(Vertex{a, n});
        mesh.vertices.push_back(Vertex{b, n});
        mesh.vertices.push_back(Vertex{c, n});
        mesh.triangles.push_back(Triangle{base, base + 1, base + 2, mat});
    }

    // Prisma o tronco de cono de "sides" lados, de y0 (radio r0) a y1 (radio
    // r1), con tapas. xf permite inclinarlo o moverlo.
    void frustum(int sides, float r0, float r1, float y0, float y1, int mat, const Mat4& xf = Mat4::identity()) {
        Vec3 inside = xf.transformPoint({0, (y0 + y1) * 0.5f, 0});
        Vec3 bottomCenter = xf.transformPoint({0, y0, 0}), topCenter = xf.transformPoint({0, y1, 0});
        for (int i = 0; i < sides; ++i) {
            float a0 = 2.0f * PI * i / sides, a1 = 2.0f * PI * (i + 1) / sides;
            Vec3 b0 = xf.transformPoint({r0 * std::cos(a0), y0, r0 * std::sin(a0)});
            Vec3 b1 = xf.transformPoint({r0 * std::cos(a1), y0, r0 * std::sin(a1)});
            Vec3 t0 = xf.transformPoint({r1 * std::cos(a0), y1, r1 * std::sin(a0)});
            Vec3 t1 = xf.transformPoint({r1 * std::cos(a1), y1, r1 * std::sin(a1)});
            triangle(b0, b1, t1, mat, inside);
            if (r1 > 0) triangle(b0, t1, t0, mat, inside);
            triangle(bottomCenter, b0, b1, mat, inside);
            if (r1 > 0) triangle(topCenter, t0, t1, mat, inside);
        }
    }

    // Esfera irregular: icosfera estirada a "size" y deformada con ruido.
    void blob(int subdivisions, Vec3 center, Vec3 size, float roughness, unsigned seed, int mat) {
        Mesh ico = makeIcosphere(subdivisions, Material{});
        Noise noise(seed);
        std::vector<Vec3> p;
        for (const Vertex& v : ico.vertices) {
            float bump = 1.0f + roughness * noise.perlin(v.position * 1.8f);
            p.push_back(center + v.position * size * bump);
        }
        for (const Triangle& t : ico.triangles) triangle(p[t.a], p[t.b], p[t.c], mat, center);
    }

    // Bipiramide: un anillo de "sides" puntos con una punta arriba y otra abajo.
    void bipyramid(int sides, float r, float yMid, float yTop, float yBottom, int mat, const Mat4& xf) {
        Vec3 inside = xf.transformPoint({0, yMid, 0});
        Vec3 top = xf.transformPoint({0, yTop, 0}), bottom = xf.transformPoint({0, yBottom, 0});
        for (int i = 0; i < sides; ++i) {
            float a0 = 2.0f * PI * i / sides, a1 = 2.0f * PI * (i + 1) / sides;
            Vec3 m0 = xf.transformPoint({r * std::cos(a0), yMid, r * std::sin(a0)});
            Vec3 m1 = xf.transformPoint({r * std::cos(a1), yMid, r * std::sin(a1)});
            triangle(m0, m1, top, mat, inside);
            triangle(m0, m1, bottom, mat, inside);
        }
    }
};

Mesh makeRock(unsigned seed) {
    MeshBuilder b;
    int stone = b.material("Roca", {0.46f, 0.38f, 0.31f}, 0.08f, 12.0f);
    b.blob(1, {0, 0.32f, 0}, {1.0f, 0.65f, 0.85f}, 0.4f, seed, stone);
    return b.mesh;
}

Mesh makeAlienTree() {
    MeshBuilder b;
    int bark = b.material("Corteza", {0.33f, 0.21f, 0.14f}, 0.05f, 8.0f);
    int leaf1 = b.material("Copa baja", {0.72f, 0.24f, 0.58f}, 0.15f, 16.0f);
    int leaf2 = b.material("Copa media", {0.85f, 0.38f, 0.70f}, 0.15f, 16.0f);
    int leaf3 = b.material("Copa alta", {0.95f, 0.58f, 0.84f}, 0.15f, 16.0f);
    b.frustum(6, 0.22f, 0.11f, -0.3f, 2.5f, bark);
    b.blob(1, {0, 2.3f, 0}, {1.3f, 0.5f, 1.3f}, 0.15f, 11, leaf1);
    b.blob(1, {0, 2.85f, 0}, {0.95f, 0.42f, 0.95f}, 0.15f, 12, leaf2);
    b.blob(1, {0, 3.3f, 0}, {0.55f, 0.35f, 0.55f}, 0.15f, 13, leaf3);
    return b.mesh;
}

Mesh makeCrystalCluster() {
    MeshBuilder b;
    int crystal = b.material("Cristal", {0.45f, 0.9f, 1.0f}, 0.9f, 80.0f, 0.35f);
    b.bipyramid(6, 0.35f, 1.6f, 2.5f, -0.3f, crystal, Mat4::identity());
    b.bipyramid(6, 0.25f, 1.1f, 1.8f, -0.2f, crystal, translate({0.4f, 0, 0.1f}) * rotateZ(-0.45f));
    b.bipyramid(6, 0.2f, 0.8f, 1.3f, -0.2f, crystal, translate({-0.25f, 0, -0.35f}) * rotateX(-0.5f));
    return b.mesh;
}

Mesh makeBeacon() {
    MeshBuilder b;
    int metal = b.material("Metal", {0.58f, 0.59f, 0.64f}, 0.6f, 40.0f);
    int lamp = b.material("Luz", {1.0f, 0.18f, 0.1f}, 0.0f, 1.0f, 1.0f);
    b.frustum(4, 0.9f, 0.55f, -0.2f, 0.5f, metal);  // base
    b.frustum(4, 0.09f, 0.06f, 0.5f, 4.2f, metal);  // mastil
    // Travesanos: prismas delgados acostados (girados 90 grados).
    b.frustum(4, 0.04f, 0.04f, -0.6f, 0.6f, metal, translate({0, 2.6f, 0}) * rotateZ(PI / 2));
    b.frustum(4, 0.04f, 0.04f, -0.4f, 0.4f, metal, translate({0, 3.4f, 0}) * rotateX(PI / 2));
    b.blob(1, {0, 4.35f, 0}, {0.22f, 0.22f, 0.22f}, 0.0f, 1, lamp);
    return b.mesh;
}

// ---------------------------------------------------------------- distribucion

void scatterProps(const ScatterRule& rule, unsigned seed, const Noise& density, std::vector<PropInstance>& out) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    float minDot = std::cos(rule.minSpacing);

    for (int i = 0; i < rule.candidates; ++i) {
        // Punto uniforme sobre la esfera: z uniforme en [-1, 1] y angulo uniforme.
        float z = unit(rng) * 2.0f - 1.0f;
        float a = unit(rng) * 2.0f * PI;
        float r = std::sqrt(1.0f - z * z);
        Vec3 dir{r * std::cos(a), z, r * std::sin(a)};
        float yaw = unit(rng) * 2.0f * PI;
        float jitter = unit(rng);
        int variant = (int)(unit(rng) * rule.models.size()) % (int)rule.models.size();

        // Densidad = que tan apto es el terreno * ruido de distribucion. El
        // ruido hace que los objetos se agrupen en zonas, como un bosque.
        float noiseDensity = 0.5f + 0.5f * density.fbm(dir * 5.0f, 3);
        float d = rule.suitability(dir) * noiseDensity;
        if (d < rule.threshold) continue;

        // Separacion minima con los que ya estan (de cualquier tipo).
        bool tooClose = false;
        for (const PropInstance& other : out) {
            if (dot(other.direction, dir) > minDot) {
                tooClose = true;
                break;
            }
        }
        if (tooClose) continue;

        float t = clampf((d - rule.threshold) / (1.0f - rule.threshold), 0.0f, 1.0f);
        float scale = (rule.minScale + (rule.maxScale - rule.minScale) * t) * (0.85f + 0.3f * jitter);
        out.push_back(PropInstance{rule.models[variant], dir, yaw, scale});
    }
}

Mesh mergeProps(const std::vector<PropInstance>& instances, float planetRadius) {
    Mesh merged;
    // Los materiales de cada modelo se copian una sola vez y todas sus
    // instancias los comparten.
    std::map<const Mesh*, int> materialOffsets;
    for (const PropInstance& inst : instances) {
        // Ejes locales del objeto: "arriba" es la vertical del planeta en ese
        // punto y los otros dos son tangentes a la superficie.
        Vec3 up = inst.direction;
        Vec3 helper = std::fabs(up.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 right = normalize(cross(helper, up));
        Vec3 back = cross(right, up);
        // Se hunde un poco para que la base no quede flotando en la curva.
        Vec3 position = up * (planetRadius - 0.1f * inst.scale);
        Mat4 local = fromBasis(right, up, back, position) * rotateY(inst.yaw) * scale(inst.scale);

        auto found = materialOffsets.find(inst.model);
        if (found == materialOffsets.end()) {
            found = materialOffsets.emplace(inst.model, (int)merged.materials.size()).first;
            merged.materials.insert(merged.materials.end(), inst.model->materials.begin(), inst.model->materials.end());
        }
        int materialOffset = found->second;
        int vertexOffset = (int)merged.vertices.size();
        for (const Vertex& v : inst.model->vertices) {
            merged.vertices.push_back(Vertex{local.transformPoint(v.position), normalize(local.transformDir(v.normal))});
        }
        for (const Triangle& t : inst.model->triangles) {
            merged.triangles.push_back(
                Triangle{t.a + vertexOffset, t.b + vertexOffset, t.c + vertexOffset, t.material + materialOffset});
        }
    }
    return merged;
}
