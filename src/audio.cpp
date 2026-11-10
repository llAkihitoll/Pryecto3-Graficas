#include "audio.h"
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace audio {
namespace {

const int RATE = 44100;
const float TWO_PI = 6.2831853f;
const int MAX_VOICES = 24;

SDL_AudioDeviceID device = 0;
std::vector<float> clips[(int)Sfx::Count];

// Un efecto sonando: avanza por su clip a "step" muestras por muestra.
struct Voice {
    const std::vector<float>* clip = nullptr;
    double pos = 0.0;
    float step = 1.0f;
    float volume = 1.0f;
};
Voice voices[MAX_VOICES];
bool isMuted = false;

// Lo que pide el juego para el motor y el boost (se cambia con el
// dispositivo bloqueado) y los valores suavizados que usa el callback.
struct CarSound {
    float active = 0, speed = 0, throttle = 0, boost = 0; // objetivos
    float gain = 0, curSpeed = 0, curThrottle = 0, curBoost = 0;
    float phase = 0, subPhase = 0, rumblePhase = 0;
    float engineLow = 0, noiseLow1 = 0, noiseLow2 = 0, noiseHigh = 0;
} car;

// Ruido blanco (xorshift): siempre la misma secuencia.
uint32_t noiseState = 0x12345678u;
float noise() {
    noiseState ^= noiseState << 13;
    noiseState ^= noiseState >> 17;
    noiseState ^= noiseState << 5;
    return (float)(noiseState & 0xFFFFFF) / (float)0x7FFFFF - 1.0f;
}

// Coeficiente de un filtro de un polo para una frecuencia de corte.
float onePole(float cutoff) { return 1.0f - std::exp(-TWO_PI * cutoff / RATE); }
// Coeficiente para seguir un valor con constante de tiempo tau (segundos).
float smoothing(float tau) { return 1.0f - std::exp(-1.0f / (RATE * tau)); }

float square(float phase) { return phase - std::floor(phase) < 0.5f ? 1.0f : -1.0f; }
float saw(float phase) { return 2.0f * (phase - std::floor(phase)) - 1.0f; }
float triangle(float phase) { return 1.0f - 4.0f * std::fabs(phase - std::floor(phase) - 0.5f); }

// Oscilador con frecuencia que cambia: acumula la fase muestra por muestra.
struct Osc {
    float phase = 0.0f;
    float next(float freq) {
        phase += freq / RATE;
        if (phase > 1.0f) phase -= std::floor(phase);
        return phase;
    }
};
struct LowPass {
    float y = 0.0f;
    float next(float x, float cutoff) { return y += onePole(cutoff) * (x - y); }
};

// Genera un clip de "seconds" segundos con f(t) por muestra.
template <typename F>
std::vector<float> render(float seconds, F f) {
    int n = (int)(seconds * RATE);
    std::vector<float> out(n);
    for (int i = 0; i < n; ++i) out[i] = f((float)i / RATE);
    // Fundido final corto para que no truene al cortarse.
    int fade = std::min(n, RATE / 100);
    for (int i = 0; i < fade; ++i) out[n - 1 - i] *= (float)i / fade;
    return out;
}

void buildClips() {
    // Aterrizar: rafaga de aire que baja y un golpe grave al tocar.
    {
        Osc boom;
        LowPass air;
        clips[(int)Sfx::Landing] = render(1.1f, [&](float t) {
            float whoosh = air.next(noise(), 2500.0f * std::exp(-t * 3.0f) + 150.0f) * (1.0f - std::exp(-t * 20.0f)) *
                           std::exp(-t * 3.5f);
            float hitT = std::max(0.0f, t - 0.25f);
            float thump = t < 0.25f ? 0.0f : std::sin(TWO_PI * boom.next(85.0f * std::exp(-hitT * 4.0f) + 35.0f)) *
                                                 std::exp(-hitT * 6.0f);
            return 0.8f * whoosh + 0.9f * thump;
        });
    }
    // Despegar: ruido de cohete que sube de tono y un zumbido que acelera.
    {
        Osc tone;
        LowPass air;
        clips[(int)Sfx::TakeOff] = render(1.4f, [&](float t) {
            float env = std::min(1.0f, t * 4.0f) * std::exp(-std::max(0.0f, t - 0.6f) * 4.0f);
            float roar = air.next(noise(), 300.0f + 3000.0f * t);
            float hum = saw(tone.next(70.0f + 260.0f * t * t)) * 0.25f;
            return env * (0.9f * roar + hum);
        });
    }
    // Caer al suelo: golpe sordo de la suspension.
    {
        Osc boom;
        LowPass dust;
        clips[(int)Sfx::Touchdown] = render(0.3f, [&](float t) {
            float thump = std::sin(TWO_PI * boom.next(70.0f * std::exp(-t * 6.0f) + 40.0f)) * std::exp(-t * 14.0f);
            float scrape = dust.next(noise(), 1200.0f) * std::exp(-t * 25.0f);
            return thump + 0.6f * scrape;
        });
    }
    // Explosion: chasquido inicial, fuego que se apaga (el filtro se cierra) y estruendo grave.
    {
        Osc boom;
        LowPass fire, fire2;
        clips[(int)Sfx::Crash] = render(2.2f, [&](float t) {
            float n = noise();
            float crack = n * std::exp(-t * 40.0f);
            float cutoff = 3500.0f * std::exp(-t * 2.5f) + 120.0f;
            float rumble = fire2.next(fire.next(n, cutoff), cutoff) * std::exp(-t * 1.8f);
            float low = std::sin(TWO_PI * boom.next(55.0f * std::exp(-t * 1.5f) + 25.0f)) * std::exp(-t * 2.5f);
            return 0.5f * crack + 1.6f * rumble + 0.9f * low;
        });
    }
    // Choque con un objeto: golpe metalico (tonos que no son armonicos) y un poco de ruido.
    {
        Osc body, ring1, ring2;
        LowPass grit;
        clips[(int)Sfx::Bump] = render(0.45f, [&](float t) {
            float thunk = std::sin(TWO_PI * body.next(160.0f * std::exp(-t * 5.0f) + 70.0f)) * std::exp(-t * 13.0f);
            float metal = (std::sin(TWO_PI * ring1.next(523.0f)) + 0.7f * std::sin(TWO_PI * ring2.next(797.0f))) *
                          std::exp(-t * 16.0f);
            float hit = grit.next(noise(), 3000.0f) * std::exp(-t * 35.0f);
            return 0.9f * thunk + 0.25f * metal + 0.7f * hit;
        });
    }
    // Golpe al balon: "pum" hueco de pelota inflada.
    {
        Osc body, top;
        LowPass slap;
        clips[(int)Sfx::BallHit] = render(0.4f, [&](float t) {
            float pom = std::sin(TWO_PI * body.next(230.0f * std::exp(-t * 6.0f) + 90.0f)) * std::exp(-t * 11.0f);
            float ping = std::sin(TWO_PI * top.next(690.0f)) * std::exp(-t * 30.0f);
            float hit = slap.next(noise(), 2500.0f) * std::exp(-t * 45.0f);
            return pom + 0.3f * ping + 0.8f * hit;
        });
    }
    // Laser de arcade: onda cuadrada que cae rapido de tono.
    {
        Osc osc;
        clips[(int)Sfx::Laser] = render(0.2f, [&](float t) {
            return 0.4f * square(osc.next(1500.0f * std::exp(-t * 14.0f) + 220.0f)) * std::exp(-t * 9.0f);
        });
    }
    // Plasma: zumbido de sierra que baja, con un golpe grave de retroceso.
    {
        Osc zap, kick;
        LowPass fizz;
        clips[(int)Sfx::Plasma] = render(0.4f, [&](float t) {
            float z = saw(zap.next(900.0f * std::exp(-t * 9.0f) + 110.0f)) * std::exp(-t * 7.0f);
            float k = std::sin(TWO_PI * kick.next(110.0f * std::exp(-t * 15.0f) + 40.0f)) * std::exp(-t * 18.0f);
            float f = fizz.next(noise(), 4000.0f) * std::exp(-t * 20.0f);
            return 0.35f * z + 0.8f * k + 0.3f * f;
        });
    }
    // Enemigo alcanzado: explosion chica de 8 bits.
    {
        Osc osc;
        LowPass crunch;
        clips[(int)Sfx::Hit] = render(0.3f, [&](float t) {
            float n = crunch.next(noise(), 2500.0f * std::exp(-t * 6.0f) + 300.0f) * std::exp(-t * 12.0f);
            float sq = square(osc.next(260.0f * std::exp(-t * 8.0f) + 50.0f)) * std::exp(-t * 15.0f);
            return 1.1f * n + 0.25f * sq;
        });
    }
    // Obtener un objeto: tres notas que suben (La, Do#, Mi).
    {
        Osc osc;
        clips[(int)Sfx::Pickup] = render(0.36f, [&](float t) {
            const float notes[3] = {880.0f, 1108.7f, 1318.5f};
            int k = std::min(2, (int)(t / 0.07f));
            float local = t - k * 0.07f;
            float env = std::min(1.0f, local * 300.0f) * std::exp(-local * (k == 2 ? 9.0f : 25.0f));
            float p = osc.next(notes[k]);
            return 0.45f * env * (0.7f * triangle(p) + 0.3f * square(p));
        });
    }
    // Objeto especial: barrido que sube con vibrato.
    {
        Osc osc;
        clips[(int)Sfx::PowerUp] = render(0.7f, [&](float t) {
            float f = 350.0f * std::pow(2.0f, t * 3.2f) * (1.0f + 0.03f * std::sin(TWO_PI * 18.0f * t));
            float env = std::min(1.0f, t * 50.0f) * (1.0f - t / 0.7f);
            return 0.3f * env * square(osc.next(f));
        });
    }
    // Punto de Pac-Man: blip muy corto.
    {
        Osc osc;
        clips[(int)Sfx::Dot] = render(0.075f, [&](float t) {
            return 0.25f * triangle(osc.next(420.0f + 4000.0f * t)) * (1.0f - t / 0.075f);
        });
    }
    // Perder: tono que cae ondulando, como en las maquinas de arcade.
    {
        Osc osc;
        clips[(int)Sfx::Die] = render(1.1f, [&](float t) {
            float f = (900.0f * std::exp(-t * 2.2f) + 80.0f) * (1.0f + 0.12f * std::sin(TWO_PI * 9.0f * t));
            return 0.3f * square(osc.next(f)) * (1.0f - t / 1.1f);
        });
    }
    // Dano: gruñido grave y aspero.
    {
        Osc osc;
        LowPass dull;
        clips[(int)Sfx::Hurt] = render(0.3f, [&](float t) {
            float g = saw(osc.next(170.0f * std::exp(-t * 2.5f))) + 0.5f * noise();
            return 0.8f * dull.next(g, 900.0f) * std::min(1.0f, t * 80.0f) * std::exp(-t * 9.0f);
        });
    }
    // Gol: explosion y un acorde mayor que suena encima.
    {
        Osc n1, n2, n3, n4;
        const std::vector<float>& boom = clips[(int)Sfx::Crash];
        clips[(int)Sfx::Goal] = render(2.4f, [&](float t) {
            int i = (int)(t * RATE);
            float b = i < (int)boom.size() ? boom[i] : 0.0f;
            float ct = std::max(0.0f, t - 0.15f);
            float env = t < 0.15f ? 0.0f : std::min(1.0f, ct * 20.0f) * std::exp(-ct * 1.4f);
            float trem = 1.0f + 0.25f * std::sin(TWO_PI * 7.0f * t);
            float chord = triangle(n1.next(523.3f)) + triangle(n2.next(659.3f)) + triangle(n3.next(784.0f)) +
                          0.6f * square(n4.next(1046.5f));
            return 0.8f * b + 0.13f * env * trem * chord;
        });
    }
}

// Motor y boost del carro, una muestra.
float carSample() {
    CarSound& c = car;
    float kFast = smoothing(0.06f), kSlow = smoothing(0.25f);
    c.gain += (c.active - c.gain) * kFast;
    c.curSpeed += (c.speed - c.curSpeed) * kSlow;
    c.curThrottle += (c.throttle - c.curThrottle) * kFast;
    c.curBoost += (c.boost - c.curBoost) * (c.boost > c.curBoost ? smoothing(0.05f) : smoothing(0.3f));
    if (c.gain < 1e-4f) return 0.0f;

    // Motor: sierra filtrada (se abre con el acelerador) y una subarmonica que le da el "rugido".
    float freq = 42.0f + 95.0f * c.curSpeed + 18.0f * c.curThrottle;
    c.phase += freq / RATE;
    c.phase -= std::floor(c.phase);
    c.subPhase += freq * 0.5f / RATE;
    c.subPhase -= std::floor(c.subPhase);
    float raw = saw(c.phase) + 0.6f * square(c.subPhase) + 0.15f * noise();
    c.engineLow += onePole(250.0f + 900.0f * c.curThrottle + 500.0f * c.curSpeed) * (raw - c.engineLow);
    float engine = c.engineLow * (0.10f + 0.16f * c.curThrottle + 0.06f * c.curSpeed);

    // Boost: ruido de cohete (pasabanda) con un retumbo grave que late.
    float boost = 0.0f;
    if (c.curBoost > 1e-4f) {
        float n = noise();
        c.noiseLow1 += onePole(1800.0f) * (n - c.noiseLow1);
        c.noiseLow2 += onePole(1800.0f) * (c.noiseLow1 - c.noiseLow2);
        c.noiseHigh += onePole(120.0f) * (c.noiseLow2 - c.noiseHigh);
        float roar = c.noiseLow2 - c.noiseHigh;
        c.rumblePhase += 48.0f / RATE;
        c.rumblePhase -= std::floor(c.rumblePhase);
        float rumble = std::sin(TWO_PI * c.rumblePhase) * (0.7f + 0.3f * c.noiseLow2);
        boost = c.curBoost * (0.9f * roar + 0.18f * rumble);
    }
    return c.gain * (engine + boost);
}

void callback(void*, Uint8* stream, int bytes) {
    float* out = (float*)stream;
    int n = bytes / (int)sizeof(float);
    for (int i = 0; i < n; ++i) {
        float s = carSample();
        for (Voice& v : voices) {
            if (!v.clip) continue;
            size_t k = (size_t)v.pos;
            if (k + 1 >= v.clip->size()) {
                v.clip = nullptr;
                continue;
            }
            float frac = (float)(v.pos - (double)k);
            s += v.volume * ((*v.clip)[k] * (1.0f - frac) + (*v.clip)[k + 1] * frac);
            v.pos += v.step;
        }
        // Volumen general y saturacion suave (varios sonidos juntos no truenan).
        out[i] = isMuted ? 0.0f : std::tanh(0.6f * s);
    }
}

} // namespace

bool init() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "Audio no disponible: %s\n", SDL_GetError());
        return false;
    }
    buildClips();
    SDL_AudioSpec want{}, have{};
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512; // ~12 ms: los sonidos salen casi al instante
    want.callback = callback;
    device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!device) {
        std::fprintf(stderr, "No se pudo abrir el audio: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    SDL_PauseAudioDevice(device, 0);
    return true;
}

void shutdown() {
    if (!device) return;
    SDL_CloseAudioDevice(device);
    device = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void play(Sfx sfx, float volume, float pitch) {
    if (!device || isMuted || volume <= 0.0f) return;
    SDL_LockAudioDevice(device);
    // Una voz libre; si no hay, se reemplaza la que va mas avanzada.
    Voice* slot = &voices[0];
    float bestProgress = -1.0f;
    for (Voice& v : voices) {
        if (!v.clip) {
            slot = &v;
            break;
        }
        float progress = (float)(v.pos / v.clip->size());
        if (progress > bestProgress) {
            bestProgress = progress;
            slot = &v;
        }
    }
    slot->clip = &clips[(int)sfx];
    slot->pos = 0.0;
    slot->step = pitch;
    slot->volume = volume;
    SDL_UnlockAudioDevice(device);
}

void setCar(bool active, float speed01, float throttle, bool boosting) {
    if (!device) return;
    auto clamp01 = [](float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); };
    SDL_LockAudioDevice(device);
    car.active = active ? 1.0f : 0.0f;
    car.speed = clamp01(speed01);
    car.throttle = clamp01(throttle);
    car.boost = active && boosting ? 1.0f : 0.0f;
    SDL_UnlockAudioDevice(device);
}

void toggleMute() {
    if (!device) return;
    SDL_LockAudioDevice(device);
    isMuted = !isMuted;
    if (isMuted)
        for (Voice& v : voices) v.clip = nullptr;
    SDL_UnlockAudioDevice(device);
    std::printf("Sonido: %s\n", isMuted ? "silenciado" : "activado");
}

bool muted() { return isMuted; }

} // namespace audio
