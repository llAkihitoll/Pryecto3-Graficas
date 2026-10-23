#pragma once
#include "ship.h"

// Camara que sigue al carro desde atras y un poco arriba. No es la camara
// de raylib: solo calcula el ojo, el objetivo y el "arriba" para armar la
// matriz de vista con lookAt().
struct ChaseCamera {
    float distance = 5.0f;     // detras del carro
    float height = 1.4f;       // arriba del carro
    float speedZoom = 0.05f;   // distancia extra por unidad de velocidad
    float followRate = 7.0f;   // que tan rapido alcanza la posicion deseada
    float lookSensitivity = 0.005f; // radianes por pixel del raton

    Vec3 eye;
    Vec3 target;
    Vec3 up{0, 1, 0};

    // Vista libre con el raton (angulos alrededor del carro).
    float lookYaw = 0.0f;
    float lookPitch = 0.0f;

    // looking = boton derecho presionado; mouseDx/Dy en pixeles.
    void update(const Ship& ship, float dt, bool looking, float mouseDx, float mouseDy);
    void snapTo(const Ship& ship);
    Mat4 view() const { return lookAt(eye, target, up); }

private:
    Vec3 offset; // posicion del ojo relativa al carro (suavizada)
    Vec3 desiredOffset(const Ship& ship) const;
};
