#include "camera.h"
#include <cmath>

static float approach(float rate, float dt) { return 1.0f - std::exp(-rate * dt); }

// Donde deberia estar el ojo respecto al carro: atras y arriba en los ejes
// del carro, girado por la vista libre del raton.
Vec3 ChaseCamera::desiredOffset(const Ship& ship) const {
    float dist = distance + speedZoom * std::fabs(ship.forwardSpeed());
    float cy = std::cos(lookYaw), sy = std::sin(lookYaw);
    float cp = std::cos(lookPitch), sp = std::sin(lookPitch);

    // Direccion "hacia atras" girada: yaw alrededor del eje arriba del carro
    // y pitch hacia arriba/abajo.
    Vec3 back = -ship.forward;
    Vec3 dir = (back * cy + ship.right * sy) * cp + ship.up * sp;
    return dir * dist + ship.up * height;
}

void ChaseCamera::snapTo(const Ship& ship) {
    offset = desiredOffset(ship);
    up = ship.up;
    eye = ship.position + offset;
    target = ship.position + ship.up * 0.5f;
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

    // Se suaviza el offset relativo y no la posicion absoluta: asi la
    // camara no se queda atras cuando el carro va muy rapido, pero los
    // giros se sienten suaves.
    float k = approach(followRate, dt);
    offset += (desiredOffset(ship) - offset) * k;
    up = normalize(up + (ship.up - up) * k);

    eye = ship.position + offset;
    target = ship.position + ship.up * 0.5f;
}
