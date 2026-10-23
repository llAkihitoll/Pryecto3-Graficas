#include "renderer.h"
#include <algorithm>
#include <cmath>

// Vertice ya transformado: posicion en espacio de recorte mas los datos
// que se van a interpolar (posicion y normal en el mundo).
struct ClipVertex {
    Vec4 clip;
    Vec3 world;
    Vec3 normal;
};

static ClipVertex lerpVertex(const ClipVertex& a, const ClipVertex& b, float t) {
    return ClipVertex{Vec4(a.clip.x + (b.clip.x - a.clip.x) * t, a.clip.y + (b.clip.y - a.clip.y) * t,
                           a.clip.z + (b.clip.z - a.clip.z) * t, a.clip.w + (b.clip.w - a.clip.w) * t),
                      lerp(a.world, b.world, t), lerp(a.normal, b.normal, t)};
}

static uint32_t toARGB(const Vec3& c) {
    uint32_t r = (uint32_t)(clampf(c.x, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(clampf(c.y, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(clampf(c.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// Vertice en pantalla: x, y en pixeles y 1/w para la profundidad y para
// interpolar con correccion de perspectiva.
struct ScreenVertex {
    float x, y, invW;
};

static ScreenVertex toScreen(const Vec4& c, int width, int height) {
    float invW = 1.0f / c.w;
    // NDC (-1..1) -> pixeles. En pantalla la y crece hacia abajo.
    float x = (c.x * invW * 0.5f + 0.5f) * width;
    float y = (1.0f - (c.y * invW * 0.5f + 0.5f)) * height;
    return ScreenVertex{x, y, invW};
}

// Rellena un triangulo ya recortado. Usa las funciones de arista de Lab4,
// pero en flotante, para sacar coordenadas baricentricas de cada pixel.
static void rasterTriangle(Framebuffer& fb, const ClipVertex& v0, const ClipVertex& v1, const ClipVertex& v2,
                           const Material* material, const Uniforms& u, FragmentShader shader,
                           RenderStats& stats) {
    const int W = fb.width(), H = fb.height();
    ScreenVertex s0 = toScreen(v0.clip, W, H);
    ScreenVertex s1 = toScreen(v1.clip, W, H);
    ScreenVertex s2 = toScreen(v2.clip, W, H);

    // Area con signo (x2). Las caras de enfrente estan en sentido antihorario
    // en NDC; al invertir la y de la pantalla quedan con area negativa.
    float area = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
    if (std::fabs(area) < 1e-8f) return;
    if (u.cullBackFaces && area > 0) return;

    int minX = std::max(0, (int)std::floor(std::min({s0.x, s1.x, s2.x})));
    int maxX = std::min(W - 1, (int)std::ceil(std::max({s0.x, s1.x, s2.x})));
    int minY = std::max(0, (int)std::floor(std::min({s0.y, s1.y, s2.y})));
    int maxY = std::min(H - 1, (int)std::ceil(std::max({s0.y, s1.y, s2.y})));
    if (minX > maxX || minY > maxY) return;

    stats.trianglesDrawn++;
    float invArea = 1.0f / area;

    // Cada funcion de arista es lineal: e(x, y) = A*x + B*y + C. Asi se
    // avanza sumando A por pixel en vez de recalcular todo.
    float A0 = (s2.y - s1.y), B0 = -(s2.x - s1.x), C0 = (s2.x - s1.x) * s1.y - (s2.y - s1.y) * s1.x;
    float A1 = (s0.y - s2.y), B1 = -(s0.x - s2.x), C1 = (s0.x - s2.x) * s2.y - (s0.y - s2.y) * s2.x;
    float A2 = (s1.y - s0.y), B2 = -(s1.x - s0.x), C2 = (s1.x - s0.x) * s0.y - (s1.y - s0.y) * s0.x;
    // Normalizados por el area dan directamente las baricentricas (suman 1).
    A0 *= -invArea; B0 *= -invArea; C0 *= -invArea;
    A1 *= -invArea; B1 *= -invArea; C1 *= -invArea;
    A2 *= -invArea; B2 *= -invArea; C2 *= -invArea;

    // Atributos divididos entre w: se interpolan linealmente en pantalla y
    // al final se dividen entre 1/w interpolado (correccion de perspectiva).
    Vec3 wp0 = v0.world * s0.invW, wp1 = v1.world * s1.invW, wp2 = v2.world * s2.invW;
    Vec3 n0 = v0.normal * s0.invW, n1 = v1.normal * s1.invW, n2 = v2.normal * s2.invW;

    Fragment frag;
    frag.material = material;

    for (int y = minY; y <= maxY; ++y) {
        float py = y + 0.5f;
        float px = minX + 0.5f;
        float l0 = A0 * px + B0 * py + C0;
        float l1 = A1 * px + B1 * py + C1;
        float l2 = A2 * px + B2 * py + C2;

        for (int x = minX; x <= maxX; ++x, l0 += A0, l1 += A1, l2 += A2) {
            if (l0 < 0 || l1 < 0 || l2 < 0) continue;

            // Prueba de profundidad: 1/w es mayor mientras mas cerca este.
            float invW = l0 * s0.invW + l1 * s1.invW + l2 * s2.invW;
            float& depth = fb.depthAt(x, y);
            if (invW <= depth) continue;
            depth = invW;

            float w = 1.0f / invW;
            frag.worldPos = (wp0 * l0 + wp1 * l1 + wp2 * l2) * w;
            frag.normal = normalize((n0 * l0 + n1 * l1 + n2 * l2) * w);
            frag.x = x;
            frag.y = y;
            fb.setPixel(x, y, toARGB(shader(frag, u)));
            stats.pixelsShaded++;
        }
    }
}

void drawMesh(Framebuffer& fb, const Mesh& mesh, const Uniforms& u, FragmentShader shader, RenderStats& stats) {
    // Etapa de vertices: cada vertice se transforma una sola vez.
    static std::vector<ClipVertex> transformed;
    transformed.resize(mesh.vertices.size());

    Mat4 mvp = u.projection * u.view * u.model;
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vertex& v = mesh.vertices[i];
        transformed[i].clip = mvp * Vec4(v.position, 1.0f);
        transformed[i].world = u.model.transformPoint(v.position);
        // Solo usamos escalas uniformes, asi que basta con rotar la normal.
        transformed[i].normal = normalize(u.model.transformDir(v.normal));
    }

    for (const Triangle& t : mesh.triangles) {
        stats.trianglesIn++;
        const ClipVertex* in[3] = {&transformed[t.a], &transformed[t.b], &transformed[t.c]};

        // Si los tres vertices quedan del lado de afuera de un mismo plano
        // del volumen de vision, el triangulo no se ve y se descarta.
        bool outside = false;
        for (int axis = 0; axis < 3 && !outside; ++axis) {
            int over = 0, under = 0;
            for (const ClipVertex* v : in) {
                float c = axis == 0 ? v->clip.x : (axis == 1 ? v->clip.y : v->clip.z);
                if (c > v->clip.w) over++;
                if (c < -v->clip.w) under++;
            }
            outside = over == 3 || under == 3;
        }
        if (outside) continue;

        const Material* material = &mesh.materials[t.material];

        // Recorte contra el plano cercano (z >= -w). Sin esto, un vertice
        // detras de la camara tendria w <= 0 y la division lo mandaria al
        // otro lado de la pantalla. Sutherland-Hodgman con un solo plano:
        // un triangulo puede quedar como triangulo o como cuadrilatero.
        float d[3];
        int insideCount = 0;
        for (int i = 0; i < 3; ++i) {
            d[i] = in[i]->clip.z + in[i]->clip.w;
            if (d[i] >= 0) insideCount++;
        }

        if (insideCount == 3) {
            rasterTriangle(fb, *in[0], *in[1], *in[2], material, u, shader, stats);
            continue;
        }

        ClipVertex poly[4];
        int count = 0;
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            if (d[i] >= 0) poly[count++] = *in[i];
            if ((d[i] >= 0) != (d[j] >= 0)) {
                float tcut = d[i] / (d[i] - d[j]);
                poly[count++] = lerpVertex(*in[i], *in[j], tcut);
            }
        }
        for (int k = 1; k + 1 < count; ++k) {
            rasterTriangle(fb, poly[0], poly[k], poly[k + 1], material, u, shader, stats);
        }
    }
}
