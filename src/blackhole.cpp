#include "blackhole.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "geometry.h"
#include "parallel.h"
#include "scene.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Horizonte de eventos: no sale luz, asi que es negro. Solo el borde brilla
// un poco: es el anillo de fotones, luz que da vueltas muy cerca del horizonte.
static Vec3 horizonShader(const Fragment& f, const Uniforms& u) {
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float edge = 1.0f - std::fabs(dot(f.normal, toCamera));
    float e2 = edge * edge;
    return f.material->color * (e2 * e2 * e2 * 1.6f);
}

// Disco de acrecion: gas que gira alrededor del agujero. Mas cerca gira mas
// rapido (como en la ley de Kepler) y esta mas caliente: blanco en el borde
// interior y del color del material hacia afuera. Las vetas son senos que
// avanzan con el tiempo. Efecto Doppler: el lado que gira hacia la camara se
// ve mas brillante que el que se aleja.
static Vec3 diskShader(const Fragment& f, const Uniforms& u) {
    // Pasada 1: la mitad de atras (mas lejos de la camara que el centro del
    // agujero); pasada 2: la de adelante. Cerca del corte las dos se mezclan
    // poco a poco (suman 1), para que no se note la linea donde se parte.
    Vec3 toCamera = normalize(u.cameraPos - u.objectCenter);
    float side = dot(f.worldPos - u.objectCenter, toCamera) / u.objectRadius;
    float frontWeight = smoothstep(-1.5f, 1.5f, side);
    float weight = u.pass == 2 ? frontWeight : 1.0f - frontWeight;
    if (weight <= 0.0f) return Vec3{0, 0, 0};
    float lx = f.localPos.x, lz = f.localPos.z;
    float r = std::sqrt(lx * lx + lz * lz);
    float t = clampf((r - DISK_INNER) / (DISK_OUTER - DISK_INNER), 0.0f, 1.0f);
    float angle = std::atan2(lz, lx);

    float omega = 2.5f / (r * std::sqrt(r)); // radianes por segundo
    float spin = angle - u.time * omega;
    float swirl = 0.6f + 0.4f * std::sin(spin * 3.0f + r * 6.0f) * std::sin(spin * 5.0f - r * 11.0f + 1.7f);
    swirl *= 0.85f + 0.15f * std::sin(r * 18.0f); // anillos finos

    // Se apaga suave en los dos bordes, sobre todo hacia afuera.
    float fade = smoothstep(0.0f, 0.06f, t) * (1.0f - smoothstep(0.35f, 1.0f, t));

    float heat = std::pow(1.0f - t, 1.6f);
    const Vec3 hot{1.0f, 0.96f, 0.88f};
    const Vec3& mid = f.material->color;
    Vec3 color = heat > 0.5f ? lerp(mid, hot, (heat - 0.5f) * 2.0f) : lerp(mid * 0.3f, mid, heat * 2.0f);

    // Direccion en que se mueve el gas en este punto (tangente al circulo).
    Vec3 radial = normalize(u.model.transformDir(Vec3{lx, 0.0f, lz}));
    Vec3 axis = normalize(u.model.transformDir(Vec3{0.0f, 1.0f, 0.0f}));
    Vec3 orbit = cross(radial, axis);
    float doppler = 1.0f + 0.65f * dot(orbit, normalize(u.cameraPos - f.worldPos));

    return color * (swirl * fade * doppler * 1.25f * weight);
}

CelestialBody makeBlackHole(const std::string& name, const Vec3& position, float radius, const Vec3& diskColor) {
    CelestialBody hole;
    hole.name = name;
    hole.blackHole = true;
    hole.fixedPosition = position;
    hole.radius = radius;
    hole.buildLods(Material{name, diskColor}); // el horizonte usa el color para su borde
    hole.shader = horizonShader;
    hole.spinPeriod = 0.0f;
    hole.axialTilt = radians(18.0f);
    // Atrae fuerte de cerca, pero su alcance es corto: hay que ir a buscarlo.
    hole.surfaceGravity = 22.0f;
    hole.ring.materials = {Material{"Disco de acrecion", diskColor}};
    addRing(hole.ring, DISK_INNER, DISK_OUTER, 160, 0);
    hole.ringOuter = DISK_OUTER;
    return hole;
}

void applyLensing(Framebuffer& fb, const Uniforms& u, const Vec3& center, float radius) {
    // Con la camara casi encima del horizonte la lente cubriria todo de forma
    // extrana; en ese momento ya empieza la transicion al otro universo.
    if (length(center - u.cameraPos) < radius * 1.3f) return;
    Vec4 c = u.projection * u.view * Vec4(center, 1.0f);
    if (c.w <= 0.0f) return; // detras de la camara

    const int W = fb.width(), H = fb.height();
    float cx = (c.x / c.w * 0.5f + 0.5f) * W;
    float cy = (1.0f - (c.y / c.w * 0.5f + 0.5f)) * H;
    float rh = projectedRadius(u, center, radius, H); // radio del horizonte en pantalla
    float einstein = rh * 1.7f;
    float reach = rh * 7.0f; // mas lejos la luz casi no se dobla
    float holeDepth = 1.0f / c.w;

    int x0 = std::max(0, (int)(cx - reach)), x1 = std::min(W - 1, (int)(cx + reach));
    int y0 = std::max(0, (int)(cy - reach)), y1 = std::min(H - 1, (int)(cy + reach));
    if (x0 > x1 || y0 > y1) return;

    // Se lee de una copia: si no, un pixel ya deformado se volveria a usar.
    static std::vector<uint32_t> source;
    source = fb.colors();
    const float e2 = einstein * einstein;
    const int rows = y1 - y0 + 1;
    const int bandHeight = 8;
    threadPool().parallelFor((rows + bandHeight - 1) / bandHeight, [&](int band) {
        int ya = y0 + band * bandHeight, yb = std::min(y1, ya + bandHeight - 1);
        for (int y = ya; y <= yb; ++y) {
            float dy = y + 0.5f - cy;
            for (int x = x0; x <= x1; ++x) {
                float dx = x + 0.5f - cx;
                float r2 = dx * dx + dy * dy;
                if (r2 > reach * reach || r2 < 1e-4f) continue;
                if (fb.depthAt(x, y) > holeDepth) continue; // esta delante del agujero
                float r = std::sqrt(r2);
                // La desviacion se desvanece hacia el borde del alcance para
                // que no se note donde termina la lente.
                float fade = 1.0f - smoothstep(reach * 0.4f, reach, r);
                float k = (r - e2 / r * fade) / r;
                int sx = std::clamp((int)(cx + dx * k), 0, W - 1);
                int sy = std::clamp((int)(cy + dy * k), 0, H - 1);
                fb.setPixel(x, y, source[(size_t)sy * W + sx]);
            }
        }
    });
}

void drawBlackHole(Framebuffer& fb, Uniforms& u, RenderStats& stats, const CelestialBody& hole, bool cullObjects) {
    Vec3 center = hole.position(u.time);
    // La lente alcanza mas que el disco: se prueba con esa esfera.
    if (cullObjects && !sphereInFrustum(u, center, hole.radius * 7.0f)) {
        stats.objectsCulled++;
        hole.currentLod = -1;
        return;
    }
    stats.objectsDrawn++;
    hole.currentLod = hole.chooseLod(projectedRadius(u, center, hole.radius, fb.height()));

    u.model = hole.modelMatrix(u.time);
    u.objectCenter = center;
    u.objectRadius = hole.radius;
    bool cull = u.cullBackFaces;
    u.cullBackFaces = false; // el disco se ve por arriba y por abajo
    u.additive = true;

    // La mitad de atras del disco se dibuja antes de la lente y sin guardar
    // su profundidad: asi la lente la dobla junto con el fondo y se ve como
    // un arco por encima y por debajo del horizonte.
    u.pass = 1;
    u.depthWrite = false;
    drawMesh(fb, hole.ring, u, diskShader, stats);
    u.depthWrite = true;

    applyLensing(fb, u, center, hole.radius);
    u.additive = false;
    drawMesh(fb, hole.lods[hole.currentLod], u, hole.shader, stats);
    u.additive = true;

    // La mitad de adelante pasa entre la camara y el agujero: no se dobla.
    u.pass = 2;
    drawMesh(fb, hole.ring, u, diskShader, stats);
    u.pass = 0;
    u.additive = false;
    u.cullBackFaces = cull;
}
