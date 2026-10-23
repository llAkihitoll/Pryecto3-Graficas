#include "ship.h"
#include <algorithm>
#include <cmath>

// La fisica avanza en pasos fijos de 1/120 s. Si el cuadro duro 1/30 s se
// hacen 4 pasos; si duro 1/144 s a veces se hace 0 y a veces 1. Asi el
// recorrido es el mismo en una computadora lenta y en una rapida.
const float FIXED_STEP = 1.0f / 120.0f;

// Factor para acercarse a un objetivo de forma exponencial: con la misma
// "rapidez" el resultado no depende del tamano del paso.
static float approach(float rate, float h) { return 1.0f - std::exp(-rate * h); }

void Ship::update(const ShipInput& input, float dt) {
    accumulator += dt;
    // La tolerancia evita perder un paso por redondeo (0.05 - 6 * 1/120
    // en float puede quedar apenas por debajo de cero).
    while (accumulator >= FIXED_STEP - 1e-6f) {
        step(input, FIXED_STEP);
        accumulator -= FIXED_STEP;
    }
}

// Formula de Rodrigues: rota el vector v un angulo alrededor de un eje unitario.
static Vec3 rotateAround(const Vec3& v, const Vec3& axis, float angle) {
    float c = std::cos(angle), s = std::sin(angle);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

// El eje va por valor: puede ser uno de los mismos ejes que se estan girando.
void Ship::rotate(Vec3 axis, float angle) {
    forward = rotateAround(forward, axis, angle);
    up = rotateAround(up, axis, angle);
}

void Ship::lookAt(const Vec3& target) {
    forward = normalize(target - position);
    right = normalize(cross(forward, Vec3{0, 1, 0}));
    up = cross(right, forward);
}

void Ship::step(const ShipInput& in, float h) {
    const ShipTuning& t = tuning;

    // --- Giro progresivo: la velocidad angular se acerca a la deseada ---
    Vec3 target{in.pitch * t.turnRate, in.steer * t.turnRate, in.roll * t.rollRate};
    if (in.brake) target = target * 0.5f;
    angularVelocity += (target - angularVelocity) * approach(t.turnResponse, h);

    rotate(right, angularVelocity.x * h);
    rotate(up, angularVelocity.y * h);
    rotate(forward, angularVelocity.z * h);

    // Reortonormalizar: con tantas rotaciones pequenas se acumula error y
    // los ejes dejarian de ser perpendiculares (el carro se deformaria).
    forward = normalize(forward);
    right = normalize(cross(forward, up));
    up = cross(right, forward);

    // --- Boost: se gasta mientras se mantiene Shift y se recarga solo ---
    boosting = in.boost && boostFuel > 0.0f && in.throttle >= 0.0f;
    if (boosting) boostFuel = std::max(0.0f, boostFuel - h);
    else boostFuel = std::min(t.boostCapacity, boostFuel + t.boostRecharge * h);

    // --- Aceleracion, freno y reversa ---
    float speed = forwardSpeed();
    if (boosting) {
        velocity += forward * (t.boostAcceleration * h);
    } else if (in.throttle > 0) {
        velocity += forward * (t.acceleration * in.throttle * h);
    } else if (in.throttle < 0) {
        if (speed > 0.5f) velocity -= forward * std::min(speed, t.brakeDeceleration * h); // frenar
        else velocity -= forward * (t.acceleration * 0.6f * h);                         // reversa
    }

    // Agarre: la velocidad lateral se va alineando con el frente, como si
    // el carro "doblara" en vez de patinar para siempre en el espacio.
    float along = dot(velocity, forward);
    velocity += (forward * along - velocity) * approach(t.grip, h);

    // Arrastre suave para que el carro termine deteniendose solo.
    float drag = in.brake ? t.stabilize : (in.throttle == 0 && !boosting ? t.drag : 0.0f);
    velocity = velocity * std::exp(-drag * h);
    if (in.brake) angularVelocity = angularVelocity * std::exp(-t.stabilize * h);

    // Limites de velocidad.
    float limit = boosting ? t.maxBoostSpeed : t.maxSpeed;
    along = dot(velocity, forward);
    if (along < -t.maxReverseSpeed) velocity -= forward * (along + t.maxReverseSpeed);
    float len = length(velocity);
    if (len > limit) {
        // Despues de un boost no se frena de golpe: baja al limite poco a poco.
        float target = boosting ? limit : std::max(limit, len - t.brakeDeceleration * 0.5f * h);
        velocity = velocity * (target / len);
    }

    position += velocity * h;
}
