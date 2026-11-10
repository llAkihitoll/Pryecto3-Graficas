#pragma once

// Sonido del juego, sintetizado por procedimiento: no se usan archivos de
// audio. SDL2 solo abre la salida de sonido; cada efecto se calcula muestra
// por muestra al arrancar (osciladores, ruido y filtros) y el motor y el boost
// del carro se generan en tiempo real segun su velocidad.
//
// Si audio::init() no se llamo (capturas, pruebas) o fallo, todas las
// funciones no hacen nada: los modulos del juego pueden llamarlas siempre.
namespace audio {

enum class Sfx {
    Landing,   // aterrizar en un planeta (al entrar al destello)
    TakeOff,   // despegar de vuelta al espacio
    Touchdown, // el carro cae al suelo despues de un salto
    Crash,     // explosion del carro (o de un asteroide)
    Bump,      // choque con un objeto (asteroide, rival, pared, planeta)
    BallHit,   // golpe al balon
    Laser,     // disparo de Galaga y Centipede
    Plasma,    // disparo de Infierno
    Hit,       // un disparo le da a un enemigo
    Pickup,    // obtener un objeto (fruta, botiquin, pad de boost, anillo...)
    PowerUp,   // objeto especial (pildora de poder, ultimo anillo)
    Dot,       // Pac-Man come un punto
    Die,       // el jugador pierde en un juego de arcade
    Hurt,      // dano en Infierno
    Goal,      // gol
    Count
};

bool init();     // abre la salida de audio y sintetiza los efectos
void shutdown();

// Volumen 0..1. pitch > 1 lo reproduce mas agudo (y mas corto).
void play(Sfx sfx, float volume = 1.0f, float pitch = 1.0f);

// Motor y boost del carro (sonidos continuos). active = false los apaga
// (juegos de arcade). speed01 y throttle van de 0 a 1.
void setCar(bool active, float speed01, float throttle, bool boosting);

void toggleMute(); // tecla K
bool muted();

} // namespace audio
