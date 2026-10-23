#include "shaders.h"
#include <algorithm>
#include <cmath>

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
    float rim = std::pow(1.0f - std::max(0.0f, dot(n, toCamera)), 3.0f) * 0.35f;
    Vec3 rimColor{0.35f, 0.5f, 1.0f};

    Vec3 color = m.color * (ambient + diffuse) + Vec3{1, 1, 1} * spec + rimColor * rim;
    return lerp(color, m.color, m.emissive);
}

Vec3 sunShader(const Fragment& f, const Uniforms& u) {
    Vec3 toCamera = normalize(u.cameraPos - f.worldPos);
    float facing = std::max(0.0f, dot(f.normal, toCamera)); // 1 en el centro, 0 en el borde
    Vec3 edge{1.0f, 0.35f, 0.05f};
    Vec3 center{1.0f, 0.95f, 0.7f};
    return lerp(edge, center, std::pow(facing, 0.6f));
}

Vec3 planetShader(const Fragment& f, const Uniforms& u) {
    Vec3 toLight = normalize(u.lightPos - f.worldPos);
    float diffuse = std::max(0.0f, dot(f.normal, toLight));
    return f.material->color * (0.06f + diffuse);
}
