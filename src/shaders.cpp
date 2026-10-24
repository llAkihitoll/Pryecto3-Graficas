#include "shaders.h"
#include <algorithm>
#include <cmath>
#include "planets.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

Vec3 carShader(const Fragment& f, const Uniforms& u) {
    const Material& m = *f.material;
    Vec3 n = f.normal;
    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);

    // Algunas caras del modelo se ven por detras (interior de las ruedas);
    // volteamos la normal para que no queden negras.
    if (dot(n, toCamera) < 0) n = -n;

    float ambient = 0.12f;
    float diffuse = std::max(0.0f, dot(n, toLight));
    Vec3 half = normalize(toLight + toCamera);
    float spec = m.specular * std::pow(std::max(0.0f, dot(n, half)), m.shininess);

    // Fresnel: mientras mas de canto se ve la superficie, mas brilla el borde.
    float edge = 1.0f - std::max(0.0f, dot(n, toCamera));
    float rim = edge * edge * edge * 0.35f;
    Vec3 rimColor{0.35f, 0.5f, 1.0f};

    Vec3 color = m.color * (ambient + diffuse) + Vec3{1, 1, 1} * spec + rimColor * rim;
    return lerp(color, m.color, m.emissive);
}

// Paleta del plasma: de rojo oscuro (frio) a blanco amarillento (caliente).
static Vec3 plasmaColor(float heat) {
    const Vec3 dark{0.45f, 0.04f, 0.0f}, orange{1.0f, 0.42f, 0.04f};
    const Vec3 yellow{1.0f, 0.82f, 0.30f}, white{1.0f, 0.97f, 0.85f};
    heat = clampf(heat, 0.0f, 1.0f);
    if (heat < 0.45f) return lerp(dark, orange, heat / 0.45f);
    if (heat < 0.8f) return lerp(orange, yellow, (heat - 0.45f) / 0.35f);
    return lerp(yellow, white, (heat - 0.8f) / 0.2f);
}

Vec3 sunShader(const Fragment& f, const Uniforms& u) {
    // x = granulo (0..1), y = fase de esa zona (0..1), z = manchas (-1..1).
    Vec3 s = u.textures->color.sample(normalize(f.localPos));

    // Animacion: cada zona late con un seno desfasado segun su fase, asi los
    // granulos se encienden y apagan a destiempo, como plasma hirviendo.
    float boil = 0.5f + 0.5f * std::sin(u.time * 2.2f + s.y * 4.0f * PI);
    float heat = 0.42f + 0.68f * s.x * (0.45f + 0.55f * boil);
    heat -= std::max(0.0f, s.z - 0.3f) * 1.4f; // manchas solares

    // Limbo: el borde del disco se ve mas frio y oscuro que el centro.
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float facing = std::max(0.0f, dot(f.normal, toCamera));
    heat *= 0.55f + 0.45f * std::sqrt(facing);

    Vec3 color = plasmaColor(heat);
    float edge = 1.0f - facing;
    float edgeGlow = edge * edge * edge;
    return color + Vec3{1.0f, 0.3f, 0.05f} * (edgeGlow * 0.5f);
}

Vec3 duneShader(const Fragment& f, const Uniforms& u) {
    Vec3 dir = normalize(f.localPos);
    Vec3 albedo = u.textures->color.sample(dir);
    // La normal del mapa esta en el espacio del modelo: se gira con el planeta.
    Vec3 n = normalize(u.model.transformDir(u.textures->normal.sample(dir)));
    Vec3 geo = f.normal; // normal de la esfera sin relieve

    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);

    // La linea dia/noche depende de la esfera, no del relieve; si no, las
    // laderas de los crateres se iluminarian en plena noche.
    float day = smoothstep(-0.12f, 0.25f, dot(geo, toLight));
    float diffuse = std::max(0.0f, dot(n, toLight)) * day;

    // Lado de noche: luz de relleno azulada desde el lado contrario al sol,
    // para que el relieve no desaparezca del todo.
    float fill = std::max(0.0f, dot(n, -toLight)) * 0.10f + 0.04f;
    Vec3 night = albedo * Vec3{0.55f, 0.65f, 1.0f} * fill;

    // Bruma de atmosfera: mas fuerte en el borde del disco y de dia.
    float edge = 1.0f - std::max(0.0f, dot(geo, toCamera));
    float rim = (edge * edge) * (edge * edge);
    Vec3 haze = Vec3{1.0f, 0.62f, 0.38f} * (rim * (0.1f + 0.9f * day) * 0.55f);

    return albedo * diffuse + night + haze;
}

Vec3 glaciaShader(const Fragment& f, const Uniforms& u) {
    Vec3 dir = normalize(f.localPos);
    Vec3 albedo = u.textures->color.sample(dir);
    float crack = u.textures->mask.sample(dir);
    Vec3 n = normalize(u.model.transformDir(u.textures->normal.sample(dir)));
    Vec3 geo = f.normal;

    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float day = smoothstep(-0.1f, 0.2f, dot(geo, toLight));

    float diffuse = std::max(0.0f, dot(n, toLight)) * day;
    // Brillo especular muy concentrado: el hielo es liso y refleja el sol.
    Vec3 half = normalize(toLight + toCamera);
    float spec = std::pow(std::max(0.0f, dot(n, half)), 70.0f) * 0.9f * day;

    // Fresnel: de canto el hielo refleja mas el "cielo" y se ve azulado.
    float edge = 1.0f - std::max(0.0f, dot(geo, toCamera));
    Vec3 fresnel = Vec3{0.45f, 0.75f, 1.0f} * (edge * edge * edge * 0.6f);

    // Las grietas brillan con luz propia, mas fuerte en el lado de noche.
    Vec3 glow = Vec3{0.15f, 0.75f, 1.0f} * (crack * (0.35f + 0.65f * (1.0f - day)));

    return albedo * (0.05f + 0.9f * diffuse) + Vec3{1, 1, 1} * spec + fresnel + glow;
}

Vec3 vergelShader(const Fragment& f, const Uniforms& u) {
    Vec3 dir = normalize(f.localPos);
    Vec3 albedo = u.textures->color.sample(dir);
    float water = u.textures->mask.sample(dir);
    Vec3 n = normalize(u.model.transformDir(u.textures->normal.sample(dir)));
    Vec3 geo = f.normal;

    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float day = smoothstep(-0.12f, 0.22f, dot(geo, toLight));

    float diffuse = std::max(0.0f, dot(n, toLight)) * day;
    // Reflejo del sol en el oceano (con la normal lisa de la esfera).
    Vec3 half = normalize(toLight + toCamera);
    float glint = water * std::pow(std::max(0.0f, dot(geo, half)), 45.0f) * 0.8f * day;
    Vec3 ground = albedo * (0.03f + diffuse) + Vec3{1.0f, 0.95f, 0.85f} * glint;

    // Nubes: la misma capa leida con un giro que cambia con el tiempo, asi
    // se desplazan sobre los continentes.
    float cloud = u.textures->clouds.sample(u.textureMotion.transformDir(dir));
    float cloudLight = 0.06f + 0.94f * std::max(0.0f, dot(geo, toLight)) * day;
    Vec3 color = lerp(ground, Vec3{cloudLight, cloudLight, cloudLight}, cloud * 0.85f);

    float edge = 1.0f - std::max(0.0f, dot(geo, toCamera));
    Vec3 atmosphere = Vec3{0.35f, 0.6f, 1.0f} * (edge * edge * edge * (0.12f + 0.88f * day) * 0.7f);
    return color + atmosphere;
}
