#include "renderer.h"
#include <algorithm>
#include <cmath>
#include "parallel.h"

// Vertice ya transformado: posicion en espacio de recorte mas los datos
// que se van a interpolar (posicion en el mundo y en el modelo, y normal).
struct ClipVertex {
    Vec4 clip;
    Vec3 world;
    Vec3 local;
    Vec3 normal;
};

static ClipVertex lerpVertex(const ClipVertex& a, const ClipVertex& b, float t) {
    return ClipVertex{Vec4(a.clip.x + (b.clip.x - a.clip.x) * t, a.clip.y + (b.clip.y - a.clip.y) * t,
                           a.clip.z + (b.clip.z - a.clip.z) * t, a.clip.w + (b.clip.w - a.clip.w) * t),
                      lerp(a.world, b.world, t), lerp(a.local, b.local, t), lerp(a.normal, b.normal, t)};
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

// Triangulo listo para rasterizar: ya recortado, proyectado, con sus
// funciones de arista calculadas y los atributos divididos entre w.
struct SetupTriangle {
    ScreenVertex s[3];
    float A[3], B[3], C[3]; // funciones de arista normalizadas (dan las baricentricas)
    Vec3 world[3], local[3], normal[3];
    const Material* material;
    int minX, maxX, minY, maxY;
};

// Prepara un triangulo (ya recortado) para el raster. Devuelve false si no
// hay nada que dibujar: area nula, cara de atras o fuera de la pantalla.
static bool setupTriangle(const ClipVertex& v0, const ClipVertex& v1, const ClipVertex& v2,
                          const Material* material, int W, int H, bool cull, SetupTriangle& t) {
    ScreenVertex s0 = toScreen(v0.clip, W, H);
    ScreenVertex s1 = toScreen(v1.clip, W, H);
    ScreenVertex s2 = toScreen(v2.clip, W, H);

    // Area con signo (x2). Las caras de enfrente estan en sentido antihorario
    // en NDC; al invertir la y de la pantalla quedan con area negativa.
    float area = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
    if (std::fabs(area) < 1e-8f) return false;
    if (cull && area > 0) return false;

    t.minX = std::max(0, (int)std::floor(std::min({s0.x, s1.x, s2.x})));
    t.maxX = std::min(W - 1, (int)std::ceil(std::max({s0.x, s1.x, s2.x})));
    t.minY = std::max(0, (int)std::floor(std::min({s0.y, s1.y, s2.y})));
    t.maxY = std::min(H - 1, (int)std::ceil(std::max({s0.y, s1.y, s2.y})));
    if (t.minX > t.maxX || t.minY > t.maxY) return false;

    // Funciones de arista de Lab4, en flotante: e(x, y) = A*x + B*y + C es
    // lineal, asi que se avanza sumando A por pixel. Divididas entre el
    // area dan directamente las coordenadas baricentricas (suman 1).
    const ScreenVertex* s[3] = {&s0, &s1, &s2};
    float k = -1.0f / area;
    for (int i = 0; i < 3; ++i) {
        const ScreenVertex& a = *s[(i + 1) % 3];
        const ScreenVertex& b = *s[(i + 2) % 3];
        t.A[i] = (b.y - a.y) * k;
        t.B[i] = -(b.x - a.x) * k;
        t.C[i] = ((b.x - a.x) * a.y - (b.y - a.y) * a.x) * k;
    }

    // Atributos divididos entre w: se interpolan linealmente en pantalla y
    // al final se dividen entre 1/w interpolado (correccion de perspectiva).
    const ClipVertex* v[3] = {&v0, &v1, &v2};
    for (int i = 0; i < 3; ++i) {
        t.s[i] = *s[i];
        t.world[i] = v[i]->world * s[i]->invW;
        t.local[i] = v[i]->local * s[i]->invW;
        t.normal[i] = v[i]->normal * s[i]->invW;
    }
    t.material = material;
    return true;
}

// Rasteriza las filas [y0, y1] de un triangulo. Cada hilo llama esto solo
// para las filas de su franja, asi dos hilos nunca escriben el mismo pixel.
static long long rasterRows(Framebuffer& fb, const SetupTriangle& t, int y0, int y1, const Uniforms& u,
                            FragmentShader shader) {
    long long shaded = 0;
    Fragment frag;
    frag.material = t.material;

    for (int y = std::max(y0, t.minY); y <= std::min(y1, t.maxY); ++y) {
        float py = y + 0.5f;
        float px = t.minX + 0.5f;
        float l0 = t.A[0] * px + t.B[0] * py + t.C[0];
        float l1 = t.A[1] * px + t.B[1] * py + t.C[1];
        float l2 = t.A[2] * px + t.B[2] * py + t.C[2];

        for (int x = t.minX; x <= t.maxX; ++x, l0 += t.A[0], l1 += t.A[1], l2 += t.A[2]) {
            if (l0 < 0 || l1 < 0 || l2 < 0) continue;

            // Prueba de profundidad: 1/w es mayor mientras mas cerca este.
            float invW = l0 * t.s[0].invW + l1 * t.s[1].invW + l2 * t.s[2].invW;
            float& depth = fb.depthAt(x, y);
            if (invW <= depth) continue;
            depth = invW;

            float w = 1.0f / invW;
            frag.worldPos = (t.world[0] * l0 + t.world[1] * l1 + t.world[2] * l2) * w;
            frag.localPos = (t.local[0] * l0 + t.local[1] * l1 + t.local[2] * l2) * w;
            frag.normal = normalize((t.normal[0] * l0 + t.normal[1] * l1 + t.normal[2] * l2) * w);
            frag.x = x;
            frag.y = y;
            fb.setPixel(x, y, toARGB(shader(frag, u)));
            shaded++;
        }
    }
    return shaded;
}

// Recorta un triangulo contra el volumen de vision y, si queda algo, lo
// agrega ya preparado a "out".
static void clipAndSetup(const ClipVertex& a, const ClipVertex& b, const ClipVertex& c, const Material* material,
                         int W, int H, bool cull, std::vector<SetupTriangle>& out) {
    const ClipVertex* in[3] = {&a, &b, &c};

    // Si los tres vertices quedan del lado de afuera de un mismo plano
    // del volumen de vision, el triangulo no se ve y se descarta.
    for (int axis = 0; axis < 3; ++axis) {
        int over = 0, under = 0;
        for (const ClipVertex* v : in) {
            float coord = axis == 0 ? v->clip.x : (axis == 1 ? v->clip.y : v->clip.z);
            if (coord > v->clip.w) over++;
            if (coord < -v->clip.w) under++;
        }
        if (over == 3 || under == 3) return;
    }

    SetupTriangle t;
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
        if (setupTriangle(a, b, c, material, W, H, cull, t)) out.push_back(t);
        return;
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
        if (setupTriangle(poly[0], poly[k], poly[k + 1], material, W, H, cull, t)) out.push_back(t);
    }
}

void drawMesh(Framebuffer& fb, const Mesh& mesh, const Uniforms& u, FragmentShader shader, RenderStats& stats) {
    ThreadPool& pool = threadPool();
    const int W = fb.width(), H = fb.height();
    const int chunks = pool.threadCount() * 4;

    // 1) Etapa de vertices: cada vertice se transforma una sola vez.
    static std::vector<ClipVertex> transformed;
    transformed.resize(mesh.vertices.size());
    Mat4 mvp = u.projection * u.view * u.model;
    int vertexCount = (int)mesh.vertices.size();
    pool.parallelFor(chunks, [&](int chunk) {
        int begin = (int)((long long)vertexCount * chunk / chunks);
        int end = (int)((long long)vertexCount * (chunk + 1) / chunks);
        for (int i = begin; i < end; ++i) {
            const Vertex& v = mesh.vertices[i];
            transformed[i].clip = mvp * Vec4(v.position, 1.0f);
            transformed[i].world = u.model.transformPoint(v.position);
            transformed[i].local = v.position;
            // Solo usamos escalas uniformes, asi que basta con rotar la normal.
            transformed[i].normal = normalize(u.model.transformDir(v.normal));
        }
    });

    // 2) Preparacion: cada grupo de triangulos se recorta y proyecta en
    // paralelo y queda en su propia lista (sin compartir memoria).
    static std::vector<std::vector<SetupTriangle>> lists;
    lists.resize(chunks);
    int triCount = (int)mesh.triangles.size();
    pool.parallelFor(chunks, [&](int chunk) {
        std::vector<SetupTriangle>& out = lists[chunk];
        out.clear();
        int begin = (int)((long long)triCount * chunk / chunks);
        int end = (int)((long long)triCount * (chunk + 1) / chunks);
        for (int i = begin; i < end; ++i) {
            const Triangle& t = mesh.triangles[i];
            clipAndSetup(transformed[t.a], transformed[t.b], transformed[t.c], &mesh.materials[t.material], W, H,
                         u.cullBackFaces, out);
        }
    });

    // 3) Raster por franjas horizontales: cada franja recorre todos los
    // triangulos preparados y dibuja solo las filas que le tocan. Hay mas
    // franjas que hilos para repartir bien las zonas con mucho trabajo.
    const int bandHeight = 16;
    const int bands = (H + bandHeight - 1) / bandHeight;
    static std::vector<long long> shadedPerBand;
    shadedPerBand.assign(bands, 0);
    pool.parallelFor(bands, [&](int band) {
        int y0 = band * bandHeight;
        int y1 = std::min(H - 1, y0 + bandHeight - 1);
        long long shaded = 0;
        for (const std::vector<SetupTriangle>& list : lists) {
            for (const SetupTriangle& t : list) {
                if (t.maxY < y0 || t.minY > y1) continue;
                shaded += rasterRows(fb, t, y0, y1, u, shader);
            }
        }
        shadedPerBand[band] = shaded;
    });

    stats.trianglesIn += triCount;
    for (const std::vector<SetupTriangle>& list : lists) stats.trianglesDrawn += (int)list.size();
    for (long long s : shadedPerBand) stats.pixelsShaded += s;
}
