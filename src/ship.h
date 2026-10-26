#pragma once
#include <functional>
#include <vector>
#include "math3d.h"

// Esfera con la que el carro puede chocar (el sol o un planeta).
struct SphereCollider {
    Vec3 center;
    Vec3 velocity;     // velocidad orbital del cuerpo
    float radius;
    float slowZone = 0.0f;       // altitud donde el carro empieza a frenar y a moverse con el planeta (0 = no tiene)
    float surfaceGravity = 0.0f; // aceleracion de la gravedad en su superficie (unidades/s^2)
    float surfaceZone = 0.0f;    // altitud donde se activa el modo superficie (0 = no tiene)
    float gravityRange = 0.0f;   // distancia al centro donde la gravedad ya no actua (0 = sin limite)
    bool deadly = false;         // tocarlo destruye el carro (el sol)

    // Si el planeta esta deformado: su forma (en el espacio local del
    // planeta) y el giro inverso para pasar una direccion del mundo a ese espacio.
    std::function<float(const Vec3&)> shape;
    Mat4 toLocal = Mat4::identity();

    // Distancia del centro a la superficie en la direccion de "point".
    float surfaceRadius(const Vec3& point) const {
        if (!shape) return radius;
        return radius * shape(toLocal.transformDir(normalize(point - center)));
    }
};

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

    // Acercamiento a planetas
    float collisionRadius = 0.8f;  // radio de la esfera que envuelve al carro
    float bounce = 0.25f;          // cuanto rebota al chocar (0 = nada, 1 = todo)
    float nearSpeed = 9.0f;        // velocidad maxima pegado a la superficie
    float nearBoostSpeed = 18.0f;
    float limitDeceleration = 25.0f;    // que tan rapido baja al nuevo limite
    float approachDeceleration = 60.0f; // lo mismo, dentro de la zona de un planeta

    // Modo de vuelo de superficie
    float cruiseAltitude = 5.0f;   // altitud a la que baja solo al entrar a la zona (sobre arboles y balizas)
    float minAltitude = 1.5f;      // lo mas bajo que se puede fijar con la flecha abajo
    float climbSpeed = 10.0f;      // unidades/s al subir o bajar con las flechas (a ras del suelo)
    float climbPerAltitude = 0.6f; // mas alto sube mas rapido, para salir de la zona sin tardar
    float alignRate = 3.5f;        // que tan rapido el carro se endereza con el planeta

    // Choques
    float crashSpeed = 20.0f;      // chocar con un planeta mas rapido que esto destruye el carro
    float captureSpeed = 28.0f;    // bajando en picada mas rapido que esto, el modo superficie no atrapa

    // Gravedad
    float gravityDrag = 0.12f;     // perdida lenta de la deriva por gravedad (por segundo)
    float gravitySurfaceDecay = 3.0f; // en modo superficie la deriva se apaga rapido
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

    // Cuerpo mas cercano (indice en la lista de colisionadores) y altura
    // sobre su superficie. Se usa para el HUD y la camara.
    int nearest = -1;
    float altitude = 0.0f;

    // Modo de vuelo de superficie: cerca de un planeta el carro se alinea con
    // su vertical y vuela paralelo al suelo, asi avanzar derecho le da la
    // vuelta al planeta. surfaceEnabled se cambia con la tecla F.
    bool surfaceEnabled = true;
    bool diving = false;  // entro a la zona en picada: el modo superficie no lo atrapa

    // Explosion: se activa al tocar el sol o chocar rapido con un planeta. El
    // carro se queda quieto (main.cpp lo hace reaparecer).
    bool crashed = false;
    int crashBody = -1;   // indice del cuerpo con el que choco
    Vec3 crashPoint;      // punto de la superficie donde choco

    // Gravedad: deriva que acumulan las atracciones de los cuerpos. Va aparte
    // de "velocity" porque el agarre alinearia esa velocidad con el frente
    // y borraria el efecto; asi la gravedad si curva la trayectoria.
    bool gravityEnabled = true; // tecla V
    Vec3 gravityVelocity;
    bool surfaceMode = false;  // activo en este momento
    float holdAltitude = 4.0f; // altitud que mantiene sin tocar las flechas

    // Avanza la simulacion dt segundos. Internamente usa pasos fijos.
    void update(const ShipInput& input, float dt, const std::vector<SphereCollider>& bodies);

    // Velocidad hacia adelante (negativa = reversa).
    float forwardSpeed() const { return dot(velocity, forward); }

    // El modelo mira a -Z, asi que su eje Z local apunta hacia atras.
    Mat4 modelMatrix() const { return fromBasis(right, up, -forward, position); }

    void lookAt(const Vec3& target);

private:
    float accumulator = 0.0f;
    void step(const ShipInput& input, float h, const std::vector<SphereCollider>& bodies);
    void collide(const std::vector<SphereCollider>& bodies);
    void rotate(Vec3 axis, float angle);
    void alignUpWith(const Vec3& n, float amount);
};
