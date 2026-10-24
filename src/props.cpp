#include "props.h"
#include <algorithm>
#include <map>
#include <random>
#include "mesh_builder.h"

// ---------------------------------------------------------------- modelos

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
    int metal = b.material("Metal", {0.58f, 0.59f, 0.64f}, 0.35f, 40.0f);
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

Mesh mergeProps(const std::vector<PropInstance>& instances, float planetRadius,
                const std::function<float(const Vec3&)>& shape) {
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
        float ground = planetRadius * (shape ? shape(up) : 1.0f);
        Vec3 position = up * (ground - 0.1f * inst.scale);
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
