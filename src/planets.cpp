#include "planets.h"
#include <algorithm>
#include <cmath>
#include "noise.h"

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Normal con relieve por diferencias finitas: cuanto cambia la altura al
// moverse un poco en dos direcciones tangentes a la esfera. bump controla
// que tan marcado se ve el relieve.
template <typename HeightFn>
static void bakeReliefNormals(CubeMap<Vec3>& normals, int size, float bump, HeightFn height) {
    const float e = 0.0015f;
    normals.bake(size, [&](const Vec3& p) {
        Vec3 helper = std::fabs(p.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
        Vec3 t1 = normalize(cross(p, helper));
        Vec3 t2 = cross(p, t1);
        float h0 = height(p);
        float h1 = height(normalize(p + t1 * e));
        float h2 = height(normalize(p + t2 * e));
        return normalize(p - t1 * ((h1 - h0) / e * bump) - t2 * ((h2 - h0) / e * bump));
    });
}

// ---------------------------------------------------------------- Venus

// Perfil de un crater segun la distancia d al centro de su celda: hundido
// adentro del radio y con un borde levantado justo afuera.
static float crater(float d, float radius) {
    if (d < radius) {
        float x = d / radius;
        return (x * x - 1.0f) + 0.35f * std::exp(-std::pow((x - 1.0f) / 0.25f, 2.0f));
    }
    return 0.35f * std::exp(-std::pow((d - radius) / (radius * 0.25f), 2.0f));
}

// Todo lo que describe un punto de la superficie de Venus.
struct DuneSample {
    float height;
    float base;   // relieve grande: negativo = tierras bajas
    float craterBig;
    float dune;
};

static DuneSample duneAt(const Noise& noise, const Vec3& p) {
    DuneSample s;
    s.base = noise.fbm(p * 1.8f, 5);
    s.craterBig = crater(noise.cellular(p * 2.6f + Vec3{3.1f, 7.7f, 1.3f}), 0.33f);
    float craterSmall = crater(noise.cellular(p * 7.0f + Vec3{11.0f, 2.0f, 5.0f}), 0.25f);

    // Dunas: franjas paralelas deformadas con ruido, solo en las tierras bajas.
    float lowland = smoothstep(0.05f, -0.2f, s.base);
    float wave = 0.5f + 0.5f * std::sin(dot(p, Vec3{0.6f, 0.25f, 0.76f}) * 90.0f + noise.fbm(p * 5.0f, 3) * 8.0f);
    s.dune = wave * wave * wave * lowland;

    s.height = s.base * 0.6f + s.craterBig * 0.5f + craterSmall * 0.2f + s.dune * 0.08f;
    return s;
}

float duneHeight(const Noise& terrain, const Vec3& p) { return duneAt(terrain, p).height; }
float duneBaseRelief(const Noise& terrain, const Vec3& p) { return duneAt(terrain, p).base; }
float duneCrater(const Noise& terrain, const Vec3& p) { return duneAt(terrain, p).craterBig; }

void bakeDune(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 rust{0.55f, 0.24f, 0.12f}, sand{0.86f, 0.54f, 0.30f}, pale{0.94f, 0.79f, 0.56f};

    tex.color.bake(size, [&](const Vec3& p) {
        DuneSample s = duneAt(noise, p);
        // Mezcla por altura: rojizo abajo, arena en medio, claro en las mesetas.
        Vec3 c = s.base < 0.0f ? lerp(sand, rust, smoothstep(0.0f, -0.45f, s.base))
                               : lerp(sand, pale, smoothstep(0.1f, 0.5f, s.base));
        if (s.craterBig < 0.0f) c = c * (1.0f + 0.25f * s.craterBig); // fondo del crater mas oscuro
        c = c * (1.0f + 0.12f * s.dune);                                // crestas de las dunas
        c = c * (1.0f + 0.08f * noise.fbm(p * 40.0f, 2));               // grano fino
        return c;
    });

    bakeReliefNormals(tex.normal, size, 0.06f, [&](const Vec3& p) { return duneAt(noise, p).height; });
}

// ---------------------------------------------------------------- Urano

// Que tan cerca esta p de una grieta (1 = justo encima). Las grietas son los
// bordes entre celdas del ruido celular: ahi f1 y f2 son casi iguales.
static float crackAt(const Noise& noise, const Vec3& p, float frequency, float width) {
    float f1, f2;
    noise.cellular2(p * frequency, f1, f2);
    return 1.0f - smoothstep(0.0f, width, f2 - f1);
}

static float glaciaHeight(const Noise& noise, const Vec3& p) {
    float bigCracks = crackAt(noise, p, 5.0f, 0.09f);
    float smallCracks = crackAt(noise, p + Vec3{4.0f, 1.0f, 7.0f}, 14.0f, 0.06f);
    // Las placas se inclinan un poco cada una y las grietas son surcos.
    return noise.fbm(p * 3.0f, 4) * 0.3f - bigCracks * 0.5f - smallCracks * 0.2f;
}

float glaciaCrack(const Noise& terrain, const Vec3& p) { return crackAt(terrain, p, 5.0f, 0.09f); }

void bakeGlacia(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 iceLight{0.80f, 0.91f, 0.98f}, iceDeep{0.52f, 0.70f, 0.90f};
    const Vec3 frost{0.95f, 0.97f, 1.0f}, crackColor{0.08f, 0.30f, 0.55f};

    tex.color.bake(size, [&](const Vec3& p) {
        float bigCracks = crackAt(noise, p, 5.0f, 0.09f);
        float smallCracks = crackAt(noise, p + Vec3{4.0f, 1.0f, 7.0f}, 14.0f, 0.06f);
        Vec3 c = lerp(iceLight, iceDeep, 0.5f + 0.5f * noise.fbm(p * 4.0f, 4));
        c = lerp(c, frost, smoothstep(0.65f, 0.85f, std::fabs(p.y))); // escarcha en los polos
        return lerp(c, crackColor, std::max(bigCracks, smallCracks * 0.7f));
    });
    // Brillo propio de las grietas (el shader lo usa como luz emitida).
    tex.mask.bake(size, [&](const Vec3& p) {
        return std::max(crackAt(noise, p, 5.0f, 0.06f), 0.5f * crackAt(noise, p + Vec3{4.0f, 1.0f, 7.0f}, 14.0f, 0.04f));
    });
    bakeReliefNormals(tex.normal, size, 0.05f, [&](const Vec3& p) { return glaciaHeight(noise, p); });
}

// ---------------------------------------------------------------- Tierra

const float VERGEL_SEA_LEVEL = 0.02f;
static const float SEA_LEVEL = VERGEL_SEA_LEVEL;

// Elevacion del terreno: positiva = tierra, menor que SEA_LEVEL = mar.
float vergelElevation(const Noise& noise, const Vec3& p) {
    return noise.fbm(p * 1.6f, 6) + 0.12f * noise.fbm(p * 7.0f, 3);
}

void bakeVergel(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 deep{0.02f, 0.09f, 0.28f}, shallow{0.05f, 0.33f, 0.52f}, beach{0.84f, 0.78f, 0.55f};
    const Vec3 grass{0.27f, 0.56f, 0.19f}, forest{0.09f, 0.33f, 0.12f};
    const Vec3 rock{0.46f, 0.42f, 0.38f}, snow{0.94f, 0.95f, 0.97f};

    tex.color.bake(size, [&](const Vec3& p) {
        float e = vergelElevation(noise, p);
        Vec3 c;
        if (e < SEA_LEVEL) {
            c = lerp(deep, shallow, smoothstep(-0.25f, SEA_LEVEL, e)); // mas claro cerca de la costa
        } else if (e < SEA_LEVEL + 0.025f) {
            c = beach;
        } else {
            float woods = smoothstep(-0.1f, 0.2f, noise.fbm(p * 5.0f + Vec3{9.0f, 3.0f, 1.0f}, 3));
            c = lerp(grass, forest, woods);
            c = lerp(c, rock, smoothstep(0.22f, 0.32f, e));
            c = lerp(c, snow, smoothstep(0.36f, 0.42f, e));
        }
        // Casquetes de hielo en los polos.
        return lerp(c, snow, smoothstep(0.82f, 0.9f, std::fabs(p.y) + 0.05f * noise.fbm(p * 8.0f, 2)));
    });
    tex.mask.bake(size, [&](const Vec3& p) { return vergelElevation(noise, p) < SEA_LEVEL ? 1.0f : 0.0f; });
    // El mar es plano: el relieve solo cuenta sobre el nivel del mar.
    bakeReliefNormals(tex.normal, size, 0.05f,
                      [&](const Vec3& p) { return std::max(SEA_LEVEL, vergelElevation(noise, p)); });
    // Nubes: ruido deformado con otro ruido para que se vean como remolinos.
    tex.clouds.bake(size / 2, [&](const Vec3& p) {
        Vec3 warp{noise.fbm(p * 2.0f, 3), noise.fbm(p * 2.0f + Vec3{5.2f, 1.3f, 2.8f}, 3), 0.0f};
        float c = noise.fbm(p * 3.0f + warp * 1.5f + Vec3{20.0f, 0.0f, 0.0f}, 5);
        return smoothstep(0.02f, 0.3f, c);
    });
}

// ---------------------------------------------------------------- Marte

static float marsCanyon(const Noise& noise, const Vec3& p) {
    // Una zanja larga y angosta junto al ecuador, solo en un lado del planeta.
    float line = std::fabs(p.y - 0.08f + 0.05f * noise.fbm(p * 3.0f, 3));
    return (1.0f - smoothstep(0.012f, 0.04f, line)) * smoothstep(0.15f, 0.55f, p.x);
}

static float marsHeight(const Noise& noise, const Vec3& p) {
    float big = crater(noise.cellular(p * 3.2f + Vec3{2.0f, 9.0f, 4.0f}), 0.3f);
    float small = crater(noise.cellular(p * 9.0f + Vec3{6.0f, 1.0f, 8.0f}), 0.22f);
    return noise.fbm(p * 2.0f, 5) * 0.5f + big * 0.4f + small * 0.15f - marsCanyon(noise, p) * 0.7f;
}

void bakeMars(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 rust{0.66f, 0.29f, 0.14f}, ochre{0.80f, 0.50f, 0.30f}, basalt{0.30f, 0.16f, 0.10f};
    const Vec3 ice{0.95f, 0.94f, 0.92f};

    tex.color.bake(size, [&](const Vec3& p) {
        Vec3 c = lerp(rust, ochre, smoothstep(-0.2f, 0.4f, noise.fbm(p * 2.0f, 5)));
        // Regiones oscuras grandes (como Syrtis Major).
        c = lerp(c, basalt, smoothstep(0.1f, 0.3f, noise.fbm(p * 1.3f + Vec3{8.0f, 3.0f, 5.0f}, 4)) * 0.8f);
        c = c * (1.0f - 0.35f * marsCanyon(noise, p)); // el fondo del canon queda en sombra
        c = c * (1.0f + 0.08f * noise.fbm(p * 35.0f, 2));
        // Casquetes de hielo en los polos.
        return lerp(c, ice, smoothstep(0.8f, 0.86f, std::fabs(p.y) + 0.04f * noise.fbm(p * 9.0f, 2)));
    });
    bakeReliefNormals(tex.normal, size, 0.06f, [&](const Vec3& p) { return marsHeight(noise, p); });
}

// ---------------------------------------------------------------- Gigantes gaseosos

// Paleta de un gigante gaseoso, de la banda mas oscura a la mas clara.
struct GasPalette {
    Vec3 darkest, dark, light, lightest;
};

// Bandas de nubes segun la latitud. Turbulencia: se deforma el punto con
// ruido antes de leer la latitud, asi los bordes de las bandas se ven
// ondulados y con remolinos. frequency = cuantas bandas hay; contrast = que
// tanto se separan las bandas claras de las oscuras.
static Vec3 gasBands(const Noise& noise, const Vec3& p, const GasPalette& pal, float frequency, float contrast) {
    Vec3 warp{noise.fbm(p * 3.0f, 4), noise.fbm(p * 3.0f + Vec3{7.0f, 2.0f, 5.0f}, 4), 0.0f};
    float lat = p.y + warp.y * 0.12f + warp.x * 0.04f;
    float band = 0.5f + 0.5f * contrast * std::sin(lat * frequency + noise.fbm(p * 1.5f, 3) * 2.0f);
    float fine = 0.5f + 0.5f * std::sin(lat * frequency * 3.1f + warp.x * 3.0f);
    Vec3 c = band < 0.5f ? lerp(pal.darkest, pal.dark, band * 2.0f) : lerp(pal.light, pal.lightest, (band - 0.5f) * 2.0f);
    return c * (0.9f + 0.1f * fine);
}

// Tormenta ovalada: un remolino de dos colores centrado en center.
static Vec3 gasStorm(const Noise& noise, const Vec3& p, const Vec3& c, const Vec3& center, float size,
                     const Vec3& inner, const Vec3& outer) {
    float d = length(p - center);
    float storm = 1.0f - smoothstep(0.08f * size, 0.22f * size, d + 0.04f * noise.fbm(p * 12.0f, 2));
    float swirl = 0.5f + 0.5f * std::sin(d * 60.0f / size - std::atan2(p.z - center.z, p.x - center.x) * 2.0f);
    return lerp(c, lerp(inner, outer, swirl), storm);
}

void bakeJupiter(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const GasPalette pal{{0.45f, 0.28f, 0.18f}, {0.70f, 0.40f, 0.24f}, {0.84f, 0.66f, 0.45f}, {0.95f, 0.88f, 0.72f}};
    const Vec3 spot = normalize(Vec3{0.7f, -0.35f, 0.6f}); // Gran Mancha Roja, en el hemisferio sur
    tex.color.bake(size, [&](const Vec3& p) {
        Vec3 c = gasBands(noise, p, pal, 15.0f, 1.0f);
        return gasStorm(noise, p, c, spot, 1.0f, Vec3{0.75f, 0.30f, 0.18f}, Vec3{0.95f, 0.65f, 0.45f});
    });
}

// ---------------------------------------------------------------- Saturno

void bakeAurelia(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const GasPalette pal{{0.70f, 0.58f, 0.40f}, {0.82f, 0.70f, 0.50f}, {0.90f, 0.80f, 0.60f}, {0.97f, 0.91f, 0.74f}};
    tex.color.bake(size, [&](const Vec3& p) { return gasBands(noise, p, pal, 22.0f, 0.7f); });
}

// ---------------------------------------------------------------- Neptuno

void bakeNeptune(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const GasPalette pal{{0.10f, 0.20f, 0.62f}, {0.16f, 0.32f, 0.78f}, {0.24f, 0.44f, 0.88f}, {0.42f, 0.62f, 0.96f}};
    const Vec3 spot = normalize(Vec3{-0.6f, -0.3f, 0.75f}); // Gran Mancha Oscura
    tex.color.bake(size, [&](const Vec3& p) {
        Vec3 c = gasBands(noise, p, pal, 11.0f, 0.5f);
        // Nubes altas blancas, sueltas y alargadas, como cirros.
        float cirrus = smoothstep(0.25f, 0.45f, noise.fbm(Vec3{p.x * 3.0f, p.y * 14.0f, p.z * 3.0f}, 4));
        c = lerp(c, Vec3{0.92f, 0.95f, 1.0f}, cirrus * 0.7f);
        return gasStorm(noise, p, c, spot, 0.8f, Vec3{0.04f, 0.08f, 0.30f}, Vec3{0.10f, 0.18f, 0.50f});
    });
}

// ---------------------------------------------------------------- Mercurio

static float ignisBase(const Noise& noise, const Vec3& p) { return noise.fbm(p * 2.5f, 5); }

float ignisLava(const Noise& noise, const Vec3& p) {
    // Rios: donde el ruido cruza cero (|fbm| chico) queda una linea fina,
    // como una red de grietas. Lagos: en las partes mas bajas.
    Vec3 warp{noise.fbm(p * 2.0f + Vec3{3.0f, 0.0f, 0.0f}, 3), 0.0f, noise.fbm(p * 2.0f + Vec3{0.0f, 0.0f, 9.0f}, 3)};
    float river = 1.0f - smoothstep(0.015f, 0.05f, std::fabs(noise.fbm(p * 3.0f + warp, 4)));
    float lake = smoothstep(-0.32f, -0.42f, ignisBase(noise, p));
    return std::max(river, lake);
}

void bakeIgnis(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    const Vec3 basalt{0.10f, 0.08f, 0.08f}, ash{0.30f, 0.26f, 0.24f}, crust{0.22f, 0.07f, 0.03f};

    tex.color.bake(size, [&](const Vec3& p) {
        float base = ignisBase(noise, p);
        Vec3 c = lerp(basalt, ash, smoothstep(0.0f, 0.45f, base)); // ceniza clara en las alturas
        c = c * (0.85f + 0.15f * noise.fbm(p * 30.0f, 2));
        return lerp(c, crust, ignisLava(noise, p)); // costra oscura donde hay lava
    });
    tex.mask.bake(size, [&](const Vec3& p) { return ignisLava(noise, p); });
    // La lava queda hundida respecto a la roca que la rodea.
    bakeReliefNormals(tex.normal, size, 0.05f, [&](const Vec3& p) {
        return ignisBase(noise, p) * 0.6f - ignisLava(noise, p) * 0.25f;
    });
}

// ---------------------------------------------------------------- Sol

void bakeSun(PlanetTextures& tex, unsigned seed, int size) {
    Noise noise(seed);
    tex.color.bake(size, [&](const Vec3& p) {
        // Granulos: brillantes al centro de cada celda y oscuros en los bordes.
        float cell = noise.cellular(p * 18.0f);
        float granule = clampf(1.0f - cell * 1.1f + 0.15f * noise.fbm(p * 40.0f, 2), 0.0f, 1.0f);
        float phase = 0.5f + 0.5f * noise.fbm(p * 6.0f, 3); // cada zona late a su propio ritmo
        float spots = noise.fbm(p * 3.0f + Vec3{5.0f, 1.0f, 9.0f}, 4);
        return Vec3{granule, phase, spots};
    });
    // El sol no usa normales con relieve, asi que tex.normal queda vacio.
}
