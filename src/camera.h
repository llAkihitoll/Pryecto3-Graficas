#pragma once
#include "ship.h"

// Camara que sigue al carro desde atras y un poco arriba. No es la camara
// de raylib: solo calcula el ojo, el objetivo y el "arriba" para armar la
// matriz de vista con lookAt().
//
// En vuelo libre funciona como la camara de carro de Rocket League: sigue al
// carro pero no su orientacion. Se queda nivelada con el horizonte (no rueda)
// y se coloca detras del rumbo horizontal del carro, asi el carro puede
// inclinarse, rodar o dar volteretas delante de la camara sin que la pantalla
// gire. En modo superficie se alinea con el carro y la vertical del planeta.
struct ChaseCamera {
    float distance = 5.0f;     // detras del carro (rueda del raton: 2 a 12)
    float height = 1.4f;       // arriba del carro
    float speedZoom = 0.05f;   // distancia extra por unidad de velocidad
    float followRate = 7.0f;   // que tan rapido alcanza la posicion deseada
    float headingRate = 6.0f;  // que tan rapido gira detras del rumbo del carro
    float lookSensitivity = 0.005f; // radianes por pixel del raton

    // En modo superficie la camara sube y mira por debajo del carro: en un
    // planeta tan pequeno el horizonte queda muy abajo y si no, no se veria.
    float surfaceHeight = 3.5f;
    float surfaceLookDown = 2.5f;  // que tan abajo del carro apunta
    float surfaceBlend = 0.0f;     // 0 = vuelo libre, 1 = superficie (transicion suave)

    Vec3 eye;
    Vec3 target;
    Vec3 up{0, 1, 0};

    // Vista libre con el raton (angulos alrededor del carro).
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;

    // looking = boton derecho presionado; mouseDx/Dy en pixeles.
    void update(const Ship& ship, float dt, bool looking, float mouseDx, float mouseDy);
    void snapTo(const Ship& ship);

    // Si el ojo quedo dentro de un planeta (el carro va pegado a la
    // superficie), lo saca hasta "margin" unidades sobre ella.
    void keepOutside(const std::vector<SphereCollider>& bodies, float margin = 0.6f);
    Mat4 view() const { return lookAt(eye, target, up); }

private:
    Vec3 offset;            // posicion del ojo relativa al carro (suavizada)
    float headingYaw = 0.0f; // rumbo horizontal que sigue la camara en vuelo libre (radianes)

    void updateHeading(const Ship& ship, float k);
    Vec3 desiredOffset(const Ship& ship) const;
    Vec3 lookTarget(const Ship& ship) const;
};
