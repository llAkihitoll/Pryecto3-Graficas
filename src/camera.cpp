#include "camera.h"
#include <cmath>

// "Arriba" fijo de la camara en vuelo libre (como el suelo de una cancha).
static const Vec3 WORLD_UP{0, 1, 0};

static float approach(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

// Lleva un angulo al rango (-pi, pi], para girar siempre por el lado corto.
static float wrapAngle(float a) {
    while (a > PI) a -= 2.0f * PI;
    while (a <= -PI) a += 2.0f * PI;
    return a;
}

// Rumbo horizontal del carro: su frente proyectado sobre el plano horizontal.
// Si el carro apunta casi vertical la proyeccion es muy corta y no dice nada
// confiable, asi que se conserva el rumbo anterior. k (0..1) suaviza el giro.
void ChaseCamera::updateHeading(const Ship& ship, float k) {
    Vec3 flat = ship.forward - WORLD_UP * dot(ship.forward, WORLD_UP);
    if (length(flat) < 0.25f) return;
    float wanted = std::atan2(flat.x, flat.z);
    headingYaw += wrapAngle(wanted - headingYaw) * k;
}

// Donde deberia estar el ojo respecto al carro, girado por la vista libre del
// raton. Se mezclan dos posiciones segun surfaceBlend:
//   - vuelo libre: detras del rumbo horizontal, con el arriba del mundo;
//   - superficie: detras del carro en sus propios ejes (arriba = planeta).
Vec3 ChaseCamera::desiredOffset(const Ship& ship) const {
    float dist = distance + speedZoom * std::fabs(ship.forwardSpeed());
    float cy = std::cos(lookYaw), sy = std::sin(lookYaw);
    float cp = std::cos(lookPitch), sp = std::sin(lookPitch);

    Vec3 heading{std::sin(headingYaw), 0.0f, std::cos(headingYaw)};
    Vec3 headingRight = normalize(cross(heading, WORLD_UP));
    Vec3 freeDir = (-heading * cy + headingRight * sy) * cp + WORLD_UP * sp;
    Vec3 freeOffset = freeDir * dist + WORLD_UP * height;

    Vec3 surfaceDir = (-ship.forward * cy + ship.right * sy) * cp + ship.up * sp;
    Vec3 surfaceOffset = surfaceDir * dist + ship.up * surfaceHeight;

    return lerp(freeOffset, surfaceOffset, surfaceBlend);
}

// Punto al que mira la camara: un poco arriba del carro en vuelo libre y por
// debajo de el en modo superficie (para que se vea el planeta).
Vec3 ChaseCamera::lookTarget(const Ship& ship) const {
    Vec3 freeTarget = ship.position + WORLD_UP * 0.5f;
    Vec3 surfaceTarget = ship.position - ship.up * surfaceLookDown;
    return lerp(freeTarget, surfaceTarget, surfaceBlend);
}

void ChaseCamera::snapTo(const Ship& ship) {
    surfaceBlend = ship.surfaceMode ? 1.0f : 0.0f;
    updateHeading(ship, 1.0f);
    offset = desiredOffset(ship);
    up = normalize(lerp(WORLD_UP, ship.up, surfaceBlend));
    eye = ship.position + offset;
    target = lookTarget(ship);
}

void ChaseCamera::keepOutside(const std::vector<SphereCollider>& bodies, float margin) {
    for (const SphereCollider& b : bodies) {
        Vec3 d = eye - b.center;
        float dist = length(d);
        float minDist = b.radius + margin;
        if (dist < minDist && dist > 1e-4f) eye = b.center + d * (minDist / dist);
    }
}

void ChaseCamera::update(const Ship& ship, float dt, bool looking, float mouseDx, float mouseDy) {
    if (looking) {
        lookYaw -= mouseDx * lookSensitivity;
        lookPitch = clampf(lookPitch + mouseDy * lookSensitivity, -1.2f, 1.2f);
    } else {
        // Al soltar el boton la camara regresa sola detras del carro.
        float k = approach(3.0f, dt);
        lookYaw -= lookYaw * k;
        lookPitch -= lookPitch * k;
    }

    float wanted = ship.surfaceMode ? 1.0f : 0.0f;
    surfaceBlend += (wanted - surfaceBlend) * approach(2.0f, dt);
    updateHeading(ship, approach(headingRate, dt));

    // Se suaviza el offset relativo y no la posicion absoluta: asi la
    // camara no se queda atras cuando el carro va muy rapido, pero los
    // giros se sienten suaves.
    float k = approach(followRate, dt);
    offset += (desiredOffset(ship) - offset) * k;
    // El arriba de la camara: el del mundo en vuelo libre (no rueda con el
    // carro) y el del carro en modo superficie.
    Vec3 wantedUp = normalize(lerp(WORLD_UP, ship.up, surfaceBlend));
    up = normalize(up + (wantedUp - up) * k);

    eye = ship.position + offset;
    target = lookTarget(ship);
}
