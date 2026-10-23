#pragma once
#include "math3d.h"

// Teclas que estan presionadas en este momento (las llena main.cpp).
struct ShipInput {
    float throttle = 0; // W = +1 (acelerar), S = -1 (frenar / reversa)
    float steer = 0;    // A = +1 (izquierda), D = -1 (derecha)
    float pitch = 0;    // flecha arriba = +1 (nariz arriba), abajo = -1
    float roll = 0;     // E = +1 (rueda hacia la derecha), Q = -1
    bool boost = false; // Shift
    bool brake = false; // Espacio: frenado fuerte y estabilizacion
};

// Parametros del manejo. Estan juntos para poder ajustarlos facilmente.
struct ShipTuning {
    float acceleration = 14.0f;    // unidades/s^2 con W
    float boostAcceleration = 34.0f;
    float maxSpeed = 30.0f;        // unidades/s sin boost
    float maxBoostSpeed = 65.0f;
    float maxReverseSpeed = 8.0f;
    float brakeDeceleration = 25.0f; // S mientras se avanza
    float drag = 0.25f;            // perdida de velocidad sin acelerar (por segundo)
    float grip = 1.8f;             // que tan rapido la velocidad se alinea con el frente
    float turnRate = 1.6f;         // radianes/s maximos al girar
    float rollRate = 2.4f;
    float turnResponse = 6.0f;     // que tan rapido se llega al giro deseado
    float stabilize = 3.5f;        // fuerza del frenado con Espacio
    float boostCapacity = 3.0f;    // segundos de boost con el tanque lleno
    float boostRecharge = 0.35f;   // segundos de boost recuperados por segundo
};

// El Fennec como nave. Guarda su orientacion como tres ejes ortonormales
// (adelante, arriba, derecha) en vez de angulos, asi puede girar en
// cualquier direccion del espacio sin bloqueos.
struct Ship {
    ShipTuning tuning;

    Vec3 position;
    Vec3 velocity;
    Vec3 forward{0, 0, -1};
    Vec3 up{0, 1, 0};
    Vec3 right{1, 0, 0};
    Vec3 angularVelocity; // (pitch, yaw, roll) en radianes/s, ejes locales
    float boostFuel = 3.0f;
    bool boosting = false;

    // Avanza la simulacion dt segundos. Internamente usa pasos fijos.
    void update(const ShipInput& input, float dt);

    // Velocidad hacia adelante (negativa = reversa).
    float forwardSpeed() const { return dot(velocity, forward); }

    // El modelo mira a -Z, asi que su eje Z local apunta hacia atras.
    Mat4 modelMatrix() const { return fromBasis(right, up, -forward, position); }

    void lookAt(const Vec3& target);

private:
    float accumulator = 0.0f;
    void step(const ShipInput& input, float h);
    void rotate(Vec3 axis, float angle);
};
