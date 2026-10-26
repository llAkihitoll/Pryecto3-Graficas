#include "ship.h"
#include <algorithm>
#include <cmath>

// La fisica avanza en pasos fijos de 1/120 s. Si el cuadro duro 1/30 s se
// hacen 4 pasos; si duro 1/144 s a veces se hace 0 y a veces 1. Asi el
// recorrido es el mismo en una computadora lenta y en una rapida.
const float FIXED_STEP = 1.0f / 120.0f;

static float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// Factor para acercarse a un objetivo de forma exponencial: con la misma
// "rapidez" el resultado no depende del tamano del paso.
static float approach(float rate, float h) { return 1.0f - std::exp(-rate * h); }

void Ship::update(const ShipInput& input, float dt, const std::vector<SphereCollider>& bodies) {
    if (crashed) return; // destruido: ya no se mueve
    accumulator += dt;
    // La tolerancia evita perder un paso por redondeo (0.05 - 6 * 1/120
    // en float puede quedar apenas por debajo de cero).
    while (accumulator >= FIXED_STEP - 1e-6f) {
        step(input, FIXED_STEP, bodies);
        accumulator -= FIXED_STEP;
        if (crashed) break;
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

// Gira el carro una fraccion (amount, 0..1) del angulo que hay entre su eje
// "arriba" y la direccion n. Gira adelante y arriba juntos, asi el carro
// conserva hacia donde mira mientras se endereza.
void Ship::alignUpWith(const Vec3& n, float amount) {
    Vec3 axis = cross(up, n);
    float s = length(axis);
    float c = dot(up, n);
    if (s < 1e-5f) {
        if (c > 0) return;          // ya esta alineado
        axis = right;               // esta de cabeza: cualquier eje horizontal sirve
        s = 1.0f;
    }
    rotate(axis * (1.0f / s), std::atan2(s, c) * amount);
}

void Ship::lookAt(const Vec3& target) {
    forward = normalize(target - position);
    right = normalize(cross(forward, Vec3{0, 1, 0}));
    up = cross(right, forward);
}

void Ship::step(const ShipInput& in, float h, const std::vector<SphereCollider>& bodies) {
    const ShipTuning& t = tuning;

    // --- Cuerpo mas cercano y que tan adentro de su zona de acercamiento esta ---
    nearest = -1;
    altitude = 1e9f;
    for (size_t i = 0; i < bodies.size(); ++i) {
        float alt = length(position - bodies[i].center) - bodies[i].surfaceRadius(position);
        if (alt < altitude) {
            altitude = alt;
            nearest = (int)i;
        }
    }
    // closeness: 0 fuera de la zona de frenado, 1 tocando la superficie.
    float closeness = 0.0f;
    if (nearest >= 0 && bodies[nearest].slowZone > 0.0f) {
        closeness = clampf(1.0f - altitude / bodies[nearest].slowZone, 0.0f, 1.0f);
    }

    // Modo de superficie: solo ya cerca del suelo (si no se apago con F).
    // Su zona es mas chica que la de frenado: pasar cerca de un planeta no
    // atrapa al carro, solo lo frena un poco.
    // Si entra a la zona en picada (muy rapido hacia el planeta), el modo
    // superficie no lo salva: sigue en vuelo libre y puede estrellarse.
    bool wasSurface = surfaceMode;
    bool inZone = surfaceEnabled && nearest >= 0 && altitude < bodies[nearest].surfaceZone;
    if (inZone && !wasSurface && !diving) {
        Vec3 vertical = normalize(position - bodies[nearest].center);
        float dive = -dot(velocity + gravityVelocity, vertical);
        if (dive > t.captureSpeed) diving = true;
    }
    if (!inZone) diving = false;
    surfaceMode = inZone && !diving;
    Vec3 n; // vertical del planeta en la posicion del carro
    if (surfaceMode) {
        n = normalize(position - bodies[nearest].center);
        if (!wasSurface) holdAltitude = t.cruiseAltitude; // al entrar baja a la altitud de crucero
    }

    // --- Giro progresivo: la velocidad angular se acerca a la deseada ---
    Vec3 target{in.pitch * t.turnRate, in.steer * t.turnRate, in.roll * t.rollRate};
    // En modo superficie las flechas cambian la altitud en vez de inclinar
    // el carro, y no se rueda: el planeta decide donde es "arriba".
    if (surfaceMode) target.x = target.z = 0.0f;
    if (in.brake) target = target * 0.5f;
    angularVelocity += (target - angularVelocity) * approach(t.turnResponse, h);

    rotate(right, angularVelocity.x * h);
    rotate(up, angularVelocity.y * h);
    rotate(forward, angularVelocity.z * h);
    if (surfaceMode) alignUpWith(n, approach(t.alignRate, h));

    // Reortonormalizar: con tantas rotaciones pequenas se acumula error y
    // los ejes dejarian de ser perpendiculares (el carro se deformaria).
    forward = normalize(forward);
    right = normalize(cross(forward, up));
    up = cross(right, forward);

    // --- Boost: se gasta mientras se mantiene Shift y se recarga solo ---
    boosting = in.boost && boostFuel > 0.0f && in.throttle >= 0.0f;
    if (boosting) boostFuel = std::max(0.0f, boostFuel - h);
    else boostFuel = std::min(t.boostCapacity, boostFuel + t.boostRecharge * h);

    // Limite de velocidad actual. Cerca de un planeta baja para que se pueda
    // maniobrar y rodearlo sin pasarse.
    // El freno cerca del planeta solo actua en modo superficie: en vuelo libre
    // se puede ir a fondo hacia un planeta (y estrellarse).
    float brake = surfaceMode ? closeness : 0.0f;
    float limitFar = boosting ? t.maxBoostSpeed : t.maxSpeed;
    float limitNear = boosting ? t.nearBoostSpeed : t.nearSpeed;
    float limit = limitFar + (limitNear - limitFar) * brake;

    // --- Aceleracion, freno y reversa ---
    // El empuje solo acelera mientras no se haya llegado al limite.
    float speed = forwardSpeed();
    if (boosting) {
        if (speed < limit) velocity += forward * (t.boostAcceleration * h);
    } else if (in.throttle > 0) {
        if (speed < limit) velocity += forward * (t.acceleration * in.throttle * h);
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

    // Aplicar el limite.
    along = dot(velocity, forward);
    if (along < -t.maxReverseSpeed) velocity -= forward * (along + t.maxReverseSpeed);
    float len = length(velocity);
    if (len > limit) {
        // El boost acelera hasta su limite y ahi se queda. Si el limite bajo
        // (se acabo el boost o se entro a la zona del planeta) se frena poco
        // a poco en vez de golpe.
        bool boostCap = boosting && brake == 0.0f;
        float decel = brake > 0.0f ? t.approachDeceleration : t.limitDeceleration;
        float target = boostCap ? limit : std::max(limit, len - decel * h);
        velocity = velocity * (target / len);
    }

    // Modo superficie: la parte vertical de la velocidad la controlan las
    // flechas (subir / bajar) o, sin flechas, se busca mantener holdAltitude.
    if (surfaceMode) {
        float targetClimb;
        if (in.pitch != 0.0f) {
            targetClimb = in.pitch * (t.climbSpeed + t.climbPerAltitude * altitude);
            holdAltitude = std::max(t.minAltitude, altitude);
        } else {
            targetClimb = clampf((holdAltitude - altitude) * 1.5f, -t.climbSpeed, t.climbSpeed);
        }
        float climb = dot(velocity, n);
        climb += (targetClimb - climb) * approach(5.0f, h);
        velocity = velocity - n * dot(velocity, n) + n * climb;
    }

    // Marco local: cerca del planeta el carro tambien avanza con la orbita
    // del planeta. Asi el planeta no se aleja (ni lo atropella) mientras se
    // le da la vuelta, y "velocity" queda relativa a la superficie.
    Vec3 carried = nearest >= 0 ? bodies[nearest].velocity * closeness : Vec3{0, 0, 0};
    // Gravedad: cada cuerpo atrae con a = g * (R / r)^2 (inversa del
    // cuadrado de la distancia, igual a g en su superficie). En modo
    // superficie no se aplica: ahi el modo local ya controla la altura.
    if (gravityEnabled && !surfaceMode) {
        for (const SphereCollider& b : bodies) {
            Vec3 d = b.center - position;
            float r2 = std::max(dot(d, d), b.radius * b.radius);
            float a = b.surfaceGravity * b.radius * b.radius / r2;
            // Alcance limitado: la atraccion se desvanece en el ultimo tercio
            // del alcance y mas alla no existe, para que viajar entre
            // planetas no se sienta como ser jalado todo el tiempo.
            if (b.gravityRange > 0.0f) a *= 1.0f - smoothstep(b.gravityRange * 0.66f, b.gravityRange, std::sqrt(r2));
            gravityVelocity += d * (a / std::sqrt(r2) * h);
        }
        gravityVelocity = gravityVelocity * std::exp(-(in.brake ? t.stabilize : t.gravityDrag) * h);
    } else {
        gravityVelocity = gravityVelocity * std::exp(-t.gravitySurfaceDecay * h);
    }

    position += (velocity + gravityVelocity + carried) * h;

    // Al avanzar en linea recta sobre una esfera la vertical cambia un poco.
    // Se gira la velocidad horizontal para que siga pegada a la nueva
    // superficie (con la misma rapidez): asi la trayectoria se curva y
    // rodea el planeta en lugar de alejarse en linea recta.
    if (surfaceMode) {
        Vec3 n2 = normalize(position - bodies[nearest].center);
        float climb = dot(velocity, n);
        Vec3 flat = velocity - n * climb;
        float speedFlat = length(flat);
        Vec3 flat2 = flat - n2 * dot(flat, n2);
        float len2 = length(flat2);
        if (len2 > 1e-6f) flat2 = flat2 * (speedFlat / len2);
        velocity = flat2 + n2 * climb;

        // El carro tambien gira lo mismo que cambio la vertical; si solo se
        // enderezara poco a poco, quedaria siempre atrasado y con la nariz
        // un poco hacia arriba, y se iria alejando de la superficie.
        Vec3 axis = cross(n, n2);
        float s = length(axis);
        if (s > 1e-7f) {
            rotate(axis * (1.0f / s), std::atan2(s, dot(n, n2)));
            right = normalize(cross(forward, up));
            up = cross(right, forward);
        }
    }

    collide(bodies);
}

// Colision basica esfera contra esfera: si el carro quedo adentro de un
// cuerpo, se empuja a la superficie y se quita (y rebota un poco) la parte
// de la velocidad que iba hacia adentro.
void Ship::collide(const std::vector<SphereCollider>& bodies) {
    for (size_t i = 0; i < bodies.size(); ++i) {
        const SphereCollider& b = bodies[i];
        Vec3 d = position - b.center;
        float dist = length(d);
        float minDist = b.surfaceRadius(position) + tuning.collisionRadius;
        if (dist >= minDist) continue;

        Vec3 n = dist > 1e-4f ? d * (1.0f / dist) : Vec3{0, 1, 0};
        // Velocidad del impacto: la parte de la velocidad que va hacia el
        // cuerpo. Tocar el sol o chocar muy rapido destruye el carro.
        float impact = -dot(velocity + gravityVelocity, n);
        if (b.deadly || impact > tuning.crashSpeed) {
            crashed = true;
            crashBody = (int)i;
            crashPoint = b.center + n * b.surfaceRadius(position);
            velocity = gravityVelocity = Vec3{0, 0, 0};
            return;
        }
        position = b.center + n * minDist;
        float into = dot(velocity, n);
        if (into < 0) velocity -= n * (into * (1.0f + tuning.bounce));
        // La gravedad tampoco puede seguir empujando hacia adentro.
        float fall = dot(gravityVelocity, n);
        if (fall < 0) gravityVelocity -= n * fall;
    }
}
