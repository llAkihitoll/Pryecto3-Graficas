#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "camera.h"
#include "framebuffer.h"
#include "font.h"
#include "hud.h"
#include "obj_loader.h"
#include "renderer.h"
#include "scene.h"
#include "shaders.h"
#include "ship.h"
#include "asteroids.h"
#include "blackhole.h"
#include "boost_flame.h"
#include "particles.h"
#include "openworld.h"
#include "arcade.h"

// Resolucion interna del framebuffer. La ventana es mas grande y SDL solo
// estira la imagen ya terminada al mostrarla.
const int RENDER_WIDTH = 960;
const int RENDER_HEIGHT = 540;
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

const float FOV_DEG = 60.0f;
const float Z_NEAR = 0.05f;
const float Z_FAR = 5000.0f;

// Donde empieza el carro: a un lado del planeta, con el sol de costado
// para que se vea el lado de dia y el de noche.
const Vec3 CAR_START{-70.0f, 8.0f, 90.0f};

// Segundos que pasan desde que el carro explota hasta que reaparece.
const float RESPAWN_DELAY = 3.0f;

// Duracion del viaje por el agujero negro: a la mitad (con la pantalla en
// blanco) se cambia de universo.
const float WARP_TIME = 2.4f;

// Duracion del destello al aterrizar en Arena (o al despegar de su mundo abierto).
const float LAND_TIME = 1.6f;

// Cinturon de asteroides: cuantas rocas y con que semilla se reparten.
const int ASTEROID_COUNT = 300;
const unsigned ASTEROID_SEED = 4242;

// Por debajo de este radio en pantalla (pixeles) el carro usa el modelo reducido.
const float CAR_LOD_PIXELS = 40.0f;

// Ajusta como reacciona cada material del Fennec a la luz.
void setupCarMaterials(Mesh& car) {
    for (Material& m : car.materials) {
        if (m.name == "Fennec - Body") { m.specular = 0.6f; m.shininess = 48.0f; }
        else if (m.name == "Window") { m.color = Vec3{0.03f, 0.04f, 0.06f}; m.specular = 0.9f; m.shininess = 96.0f; }
        else if (m.name == "Headlights") { m.emissive = 1.0f; }
        else if (m.name == "Alpha Rim") { m.specular = 0.8f; m.shininess = 64.0f; }
        else if (m.name == "Dieci Tread") {
            // Llantas: el dibujo de la banda es muy fino para el tamano en
            // pantalla; con menos intensidad no se ve tan granulado.
            m.specular = 0.15f; m.shininess = 16.0f; m.normalStrength = 0.6f;
        }
        else { m.specular = 0.15f; m.shininess = 16.0f; } // chasis y pintura negra
    }
}

bool loadCar(const std::string& path, Mesh& mesh) {
    Uint64 start = SDL_GetPerformanceCounter();
    if (!loadOBJ(path, mesh)) {
        std::fprintf(stderr, "No se pudo cargar el modelo: %s\n", path.c_str());
        return false;
    }
    setupCarMaterials(mesh);
    double ms = 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
    std::printf("%s: %zu vertices, %zu triangulos, %zu materiales (%.0f ms)\n", path.c_str(),
                mesh.vertices.size(), mesh.triangles.size(), mesh.materials.size(), ms);
    return true;
}

// Convierte una cadena de teclas simuladas ("wa", "wb", ...) en entrada.
// Solo se usa para pruebas sin ventana: w s a d q e, i/k = flechas arriba/abajo,
// b = boost, x = freno fuerte. En el guion de vuelo, f cambia el modo superficie,
// v la gravedad. En los mundos abiertos, x (Espacio) es saltar, c cambia a la
// camara al balon y u despega.
ShipInput inputFromString(const std::string& keys) {
    ShipInput in;
    for (char k : keys) {
        if (k == 'w') in.throttle += 1;
        if (k == 's') in.throttle -= 1;
        if (k == 'a') in.steer += 1;
        if (k == 'd') in.steer -= 1;
        if (k == 'i') in.pitch += 1;
        if (k == 'k') in.pitch -= 1;
        if (k == 'e') in.roll += 1;
        if (k == 'q') in.roll -= 1;
        if (k == 'b') in.boost = true;
        if (k == 'x') in.brake = true;
    }
    return in;
}

ShipInput readKeyboard() {
    const Uint8* k = SDL_GetKeyboardState(nullptr);
    ShipInput in;
    in.throttle = (float)k[SDL_SCANCODE_W] - (float)k[SDL_SCANCODE_S];
    in.steer = (float)(k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) - (float)(k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]);
    in.pitch = (float)k[SDL_SCANCODE_UP] - (float)k[SDL_SCANCODE_DOWN];
    in.roll = (float)k[SDL_SCANCODE_E] - (float)k[SDL_SCANCODE_Q];
    in.boost = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    in.brake = k[SDL_SCANCODE_SPACE];
    return in;
}

// Eje del control de -1 a 1, con una zona muerta para que el stick en reposo
// no mueva el carro.
float padAxis(SDL_GameController* pad, SDL_GameControllerAxis axis, float deadZone) {
    float v = SDL_GameControllerGetAxis(pad, axis) / 32767.0f;
    if (std::fabs(v) < deadZone) return 0.0f;
    return clampf((v - (v > 0 ? deadZone : -deadZone)) / (1.0f - deadZone), -1.0f, 1.0f);
}

// Suma el control de Xbox a las teclas para manejar el carro (como en Rocket
// League): RT acelera, LT frena / reversa, stick izquierdo gira y levanta la
// nariz, A salta / frena, B boost, LB / RB giran sobre el eje (roll).
void addGamepad(SDL_GameController* pad, ShipInput& in) {
    if (!pad) return;
    auto button = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    float throttle = padAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.08f) -
                     padAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT, 0.08f);
    float steer = -padAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 0.2f);
    float pitch = -padAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0.2f);
    float roll = (float)button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) - (float)button(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    in.throttle = clampf(in.throttle + throttle, -1.0f, 1.0f);
    in.steer = clampf(in.steer + steer, -1.0f, 1.0f);
    in.pitch = clampf(in.pitch + pitch, -1.0f, 1.0f);
    in.roll = clampf(in.roll + roll, -1.0f, 1.0f);
    in.boost = in.boost || button(SDL_CONTROLLER_BUTTON_B);
    in.brake = in.brake || button(SDL_CONTROLLER_BUTTON_A);
}

// El control en el juego en primera persona (Infierno): stick izquierdo
// camina (adelante y de lado), stick derecho gira, RT o A dispara. La cruceta
// tambien camina y gira.
void addGamepadShooter(SDL_GameController* pad, ShipInput& in) {
    if (!pad) return;
    auto button = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    float walk = -padAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0.2f) + (float)button(SDL_CONTROLLER_BUTTON_DPAD_UP) -
                 (float)button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    float side = padAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 0.2f);
    float turn = -padAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX, 0.15f) + (float)button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) -
                 (float)button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    in.throttle = clampf(in.throttle + walk, -1.0f, 1.0f);
    in.roll = clampf(in.roll + side, -1.0f, 1.0f);
    in.steer = clampf(in.steer + turn, -1.0f, 1.0f);
    in.brake = in.brake || button(SDL_CONTROLLER_BUTTON_A) || padAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0.3f) > 0.0f;
}

// El control en los juegos de arcade: el stick izquierdo o la cruceta mueven
// (arriba = W, izquierda = A) y A dispara. Del stick solo cuenta el eje que mas
// se empuja, si el otro va mucho menos: asi en Pac-Man y Snake una diagonal
// casi horizontal no se lee como "arriba".
void addGamepadArcade(SDL_GameController* pad, ShipInput& in) {
    if (!pad) return;
    auto button = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    float x = -padAxis(pad, SDL_CONTROLLER_AXIS_LEFTX, 0.3f);
    float y = -padAxis(pad, SDL_CONTROLLER_AXIS_LEFTY, 0.3f);
    if (std::fabs(y) < 0.6f * std::fabs(x)) y = 0.0f;
    else if (std::fabs(x) < 0.6f * std::fabs(y)) x = 0.0f;
    x += (float)button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) - (float)button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    y += (float)button(SDL_CONTROLLER_BUTTON_DPAD_UP) - (float)button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    in.throttle = clampf(in.throttle + y, -1.0f, 1.0f);
    in.steer = clampf(in.steer + x, -1.0f, 1.0f);
    in.brake = in.brake || button(SDL_CONTROLLER_BUTTON_A);
}

// Esferas de colision de todos los cuerpos en este instante. En los planetas
// el carro empieza a frenar a 2.5 radios de altitud, entra al modo superficie
// a 1.5 radios y su gravedad solo alcanza hasta 3 radios del centro. El sol no
// tiene modo superficie y su gravedad alcanza 5 radios.
std::vector<SphereCollider> collidersAt(const Scene& scene, float time) {
    std::vector<SphereCollider> colliders;
    for (size_t i = 0; i < scene.bodies.size(); ++i) {
        const CelestialBody& b = scene.bodies[i];
        bool sun = i == 0;
        SphereCollider c{b.position(time), b.velocity(time), b.radius};
        c.surfaceGravity = b.surfaceGravity;
        c.cruiseAltitude = b.cruiseAltitude;
        if (b.blackHole) {
            // Se atraviesa (no hay superficie) y su gravedad alcanza 7 radios.
            c.solid = false;
            c.gravityRange = 7.0f * b.radius;
            colliders.push_back(c);
            continue;
        }
        c.slowZone = sun ? 0.0f : 2.5f * b.radius;
        c.surfaceZone = sun ? 0.0f : 1.5f * b.radius;
        c.gravityRange = (sun ? 5.0f : 3.0f) * b.radius;
        c.deadly = sun; // tocar el sol destruye el carro
        if (b.shape && scene.deformTerrain) {
            // Giro inverso del planeta (deshace la inclinacion y el giro en orden contrario).
            c.shape = b.shape;
            c.toLocal = rotateY(-b.spinAngle(time)) * rotateZ(-b.axialTilt);
        }
        colliders.push_back(c);
    }
    return colliders;
}

// Guion de vuelo para pruebas: "w:3,wa:2.5,x:1" = W por 3 s, W+A por 2.5 s...
std::vector<std::pair<std::string, float>> parseFlightScript(const std::string& script) {
    std::vector<std::pair<std::string, float>> segments;
    size_t start = 0;
    while (start < script.size()) {
        size_t end = script.find(',', start);
        if (end == std::string::npos) end = script.size();
        std::string seg = script.substr(start, end - start);
        size_t colon = seg.find(':');
        if (colon != std::string::npos) {
            segments.push_back({seg.substr(0, colon), (float)std::atof(seg.c_str() + colon + 1)});
        }
        start = end + 1;
    }
    return segments;
}

Ship makeStartShip(const Scene& scene, float time) {
    Ship ship;
    ship.position = scene.spawnPoint;
    ship.lookAt(scene.bodies[scene.spawnLook].position(time)); // arranca mirando al planeta
    return ship;
}

// Tecla J: a la misma altura que el agujero negro (la camara de vuelo libre
// va nivelada, asi se ve de frente), fuera del alcance de su gravedad. Se
// mira desde el lado hacia el que esta inclinado el disco, para verlo abierto.
Ship makeHoleViewShip(const Scene& scene, float time) {
    const CelestialBody& hole = *scene.blackHole();
    Vec3 center = hole.position(time);
    Ship ship;
    ship.position = center + normalize(Vec3{0.9f, 0.0f, 0.4f}) * (hole.radius * 9.0f);
    ship.lookAt(center);
    return ship;
}

// Al llegar por el agujero negro a ese universo.
Ship makeArrivalShip(const Scene& scene, float time) {
    Ship ship;
    ship.position = scene.arrivalPoint;
    ship.lookAt(scene.bodies[scene.spawnLook].position(time));
    return ship;
}

// Prueba de consistencia: el mismo recorrido de 6 s (acelerar, girar, boost,
// frenar) simulado a distintos FPS debe terminar en el mismo lugar. Se cuentan
// cuadros enteros para que el cambio de teclas caiga en el mismo instante.
void runDtTest(const Scene& scene) {
    std::vector<SphereCollider> colliders = collidersAt(scene, 0.0f);
    const int fpsList[] = {20, 30, 60, 144, 240};
    for (int fps : fpsList) {
        Ship ship = makeStartShip(scene, 0.0f);
        float frame = 1.0f / fps;
        for (int i = 0; i < 6 * fps; ++i) {
            float t = (float)i / fps;
            const char* keys = t < 2.0f ? "w" : (t < 3.5f ? "wa" : (t < 5.0f ? "wbi" : "x"));
            ship.update(inputFromString(keys), frame, colliders);
        }
        std::printf("%3d FPS -> posicion (%.3f, %.3f, %.3f)  velocidad %.3f\n", fps,
                    ship.position.x, ship.position.y, ship.position.z, length(ship.velocity));
    }
}

int main(int argc, char* argv[]) {
    // Opciones para pruebas:
    //   --screenshot archivo.png  guarda un cuadro y termina
    //   --time s                  adelanta la simulacion de los planetas
    //   --fly "w:3,wa:2"          antes de la captura, vuela con ese guion de teclas
    //   --look yaw pitch          vista libre de la camara (grados)
    //   --zoom D                  distancia de la camara al carro
    //   --no-opt                  sin descarte de objetos ni LOD (para comparar)
    //   --near N                  empieza frente al lado de dia del cuerpo N (1 = Mercurio, 2 = Venus... 8 = Neptuno)
    //   --near-dist D             con --near: a D unidades del centro, apuntando a donde
    //                             estara el planeta (para probar choques en picada)
    //   --seed N                  semilla para repartir los objetos de superficie
    //   --flat                    Venus sin deformar (esfera), para comparar
    //   --hide-help               empieza sin el panel de controles
    //   --test-impact             lanza un asteroide hacia Venus (para probar la explosion)
    //   --test-hit                pone un asteroide quieto delante del carro (para chocarlo)
    //   --no-normal-map           sin mapas de normales en el carro, para comparar
    //   --dt-test                 prueba de consistencia con distintos FPS
    //   --alt                     empieza en el universo alterno
    //   --hole                    empieza junto al agujero negro, mirandolo (como la tecla J)
    //   --arena                   empieza en el mundo abierto de Arena (afuera del estadio)
    //   --kickoff                 con --arena: empieza en el saque, dentro del estadio
    //   --training                empieza en el mundo abierto de Entrenamiento
    //   --pacman / --galaga / --centipede / --snake / --infierno   empieza en el juego de ese planeta
    const char* screenshotPath = nullptr;
    float elapsed = 0.0f, lookYawDeg = 0.0f, lookPitchDeg = 0.0f, zoom = 0.0f;
    std::string flightScript;
    bool dtTest = false;
    int nearBody = -1;
    float nearDist = 0.0f;
    unsigned propSeed = 2026;
    bool flatTerrain = false;
    bool showHelp = true;
    bool testImpact = false, testHit = false;
    bool showOrbits = true;     // tecla T: lineas de las orbitas de los planetas
    bool showTrajectory = true; // tecla Y: puntos de la trayectoria prevista del carro
    bool noNormalMap = false;
    bool startAlt = false, startAtHole = false, startArena = false, startKickoff = false, startTraining = false;
    int startArcade = 0; // planeta del juego con que se empieza (ALT_PACMAN...), 0 = ninguno
    Optimizations opt;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshotPath = argv[++i];
        else if (std::strcmp(argv[i], "--time") == 0 && i + 1 < argc) elapsed = (float)std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--fly") == 0 && i + 1 < argc) flightScript = argv[++i];
        else if (std::strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) zoom = (float)std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--look") == 0 && i + 2 < argc) {
            lookYawDeg = (float)std::atof(argv[++i]);
            lookPitchDeg = (float)std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--no-opt") == 0) opt.cullObjects = opt.autoLod = false;
        else if (std::strcmp(argv[i], "--near") == 0 && i + 1 < argc) nearBody = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--near-dist") == 0 && i + 1 < argc) nearDist = (float)std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) propSeed = (unsigned)std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--flat") == 0) flatTerrain = true;
        else if (std::strcmp(argv[i], "--hide-help") == 0) showHelp = false;
        else if (std::strcmp(argv[i], "--test-impact") == 0) testImpact = true;
        else if (std::strcmp(argv[i], "--test-hit") == 0) testHit = true;
        else if (std::strcmp(argv[i], "--no-normal-map") == 0) noNormalMap = true;
        else if (std::strcmp(argv[i], "--dt-test") == 0) dtTest = true;
        else if (std::strcmp(argv[i], "--alt") == 0) startAlt = true;
        else if (std::strcmp(argv[i], "--hole") == 0) startAtHole = true;
        else if (std::strcmp(argv[i], "--arena") == 0) startAlt = startArena = true;
        else if (std::strcmp(argv[i], "--kickoff") == 0) startKickoff = true;
        else if (std::strcmp(argv[i], "--training") == 0) startAlt = startTraining = true;
        else if (std::strcmp(argv[i], "--pacman") == 0) { startAlt = true; startArcade = ALT_PACMAN; }
        else if (std::strcmp(argv[i], "--galaga") == 0) { startAlt = true; startArcade = ALT_GALAGA; }
        else if (std::strcmp(argv[i], "--centipede") == 0) { startAlt = true; startArcade = ALT_CENTIPEDE; }
        else if (std::strcmp(argv[i], "--snake") == 0) { startAlt = true; startArcade = ALT_SNAKE; }
        else if (std::strcmp(argv[i], "--infierno") == 0) { startAlt = true; startArcade = ALT_DEMON; }
    }

    // Construir la escena incluye hornear las superficies del sol y Venus.
    auto buildStart = std::chrono::steady_clock::now();
    Scene homeScene;
    homeScene.build(propSeed);
    homeScene.spawnPoint = homeScene.arrivalPoint = CAR_START; // al regresar se sale en el inicio
    if (flatTerrain) {
        homeScene.deformTerrain = false;
        homeScene.buildProps(propSeed);
    }
    Scene* scene = &homeScene; // el universo donde esta el carro
    std::printf("Escena lista (texturas horneadas) en %.0f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count());
    if (dtTest) {
        runDtTest(homeScene);
        return 0;
    }

    // El universo alterno (y los mundos abiertos de sus planetas) se construye en otro
    // hilo mientras se juega en el normal; se espera a que termine solo al
    // cruzar el agujero negro. Al salir del programa tambien se espera (el
    // destructor lo une).
    Scene altScene;
    OpenWorld arenaWorld, trainingWorld;
    PacmanGame pacman;
    GalagaGame galaga;
    CentipedeGame centipede;
    SnakeGame snake;
    InfiernoGame infierno;
    struct Joiner {
        std::thread thread;
        ~Joiner() { if (thread.joinable()) thread.join(); }
    } altBuilder{std::thread([&altScene, &arenaWorld, &trainingWorld, &pacman, &galaga, &centipede, &snake, &infierno]() {
        altScene.buildAlternate();
        arenaWorld.build(2026, OpenWorld::Kind::Arena);
        trainingWorld.build(77, OpenWorld::Kind::Training);
        pacman.build();
        galaga.build();
        centipede.build();
        snake.build();
        infierno.build();
    })};
    bool altReady = false;
    auto ensureAlt = [&]() {
        if (altReady) return;
        altBuilder.thread.join();
        altReady = true;
    };
    if (startAlt) {
        ensureAlt();
        scene = &altScene;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Mesh carFull, carLod;
    if (!loadCar("assets/models/fennec.obj", carFull) || !loadCar("assets/models/fennec_lod.obj", carLod)) {
        SDL_Quit();
        return 1;
    }
    const float carRadius = carFull.boundingRadius();
    // El rival del mundo abierto: el mismo Fennec pintado de naranja.
    Mesh orangeFull = carFull, orangeLod = carLod;
    for (Mesh* m : {&orangeFull, &orangeLod})
        for (Material& mat : m->materials)
            if (mat.name == "Fennec - Body") mat.color = Vec3{1.0f, 0.4f, 0.05f};
    // Llama del boost (sale de los escapes del Fennec).
    const Mesh boostFlame = makeFlame({1.0f, 0.45f, 0.08f});
    const CarAssets carAssets{{&carFull, &carLod}, {&orangeFull, &orangeLod}, &boostFlame};
    bool carUsesLod = false;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_Window* window = SDL_CreateWindow("Proyecto 3 - Galaxia", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH, WINDOW_HEIGHT, screenshotPath ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             RENDER_WIDTH, RENDER_HEIGHT);
    if (!window || !renderer || !texture) {
        std::fprintf(stderr, "Error creando ventana/renderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Framebuffer fb(RENDER_WIDTH, RENDER_HEIGHT);
    fb.setClearColor(Color{4, 5, 14});

    Uniforms u;
    u.normalMapping = !noNormalMap;
    u.projection = perspective(radians(FOV_DEG), (float)RENDER_WIDTH / RENDER_HEIGHT, Z_NEAR, Z_FAR);

    Ship ship = makeStartShip(*scene, elapsed);
    ShipInput lastInput; // teclas del ultimo cuadro, para predecir la trayectoria

    // Particulas (estela del boost y explosiones) y cinturon de asteroides.
    Particles particles;
    AsteroidBelt belt;
    belt.build(ASTEROID_SEED, homeScene.rockModels, ASTEROID_COUNT);

    // Despues de mover el carro: choques con asteroides, vuelo de los
    // asteroides (y sus explosiones) y la estela del boost. Mientras haya
    // boost, cada escape suelta particulas hacia atras; como quedan en el
    // mundo, forman un rastro detras del carro.
    const Vec3 exhausts[2] = {EXHAUST_LEFT, EXHAUST_RIGHT};
    Vec3 lastExhaust[2];      // donde estaban los escapes en el cuadro anterior
    bool wasBoosting = false;
    // Explosion del carro: cuando la fisica marca "crashed" se lanza una vez
    // la explosion y empieza la cuenta para reaparecer.
    bool crashShown = false;
    float respawnTimer = 0.0f;
    auto updateEffects = [&](float step, const std::vector<SphereCollider>& colliders) {
        if (ship.crashed) {
            if (!crashShown) {
                Vec3 bodyVel = colliders[ship.crashBody].velocity;
                particles.burst(ship.crashPoint, bodyVel, 20, 0.0f, 2.0f, 30.0f, 0.2f, 0.4f);    // fogonazo
                particles.burst(ship.crashPoint, bodyVel, 220, 4.0f, 14.0f, 9.0f, 1.0f, 2.0f);  // fuego y humo
                crashShown = true;
                respawnTimer = RESPAWN_DELAY;
            }
            respawnTimer -= step;
        } else if (scene == &homeScene) {
            belt.collideShip(ship);
        }
        // El cinturon de asteroides es del universo normal.
        if (scene == &homeScene) belt.update(elapsed, step, colliders, particles);
        if (ship.boosting && !ship.crashed) {
            Mat4 carMatrix = ship.modelMatrix();
            for (int i = 0; i < 2; ++i) {
                Vec3 now = carMatrix.transformPoint(exhausts[i]);
                particles.emitStream(wasBoosting ? lastExhaust[i] : now, now, ship.forward * -6.0f, step);
                lastExhaust[i] = now;
            }
        }
        wasBoosting = ship.boosting;
        particles.update(step);
    };
    if (nearBody > 0 && nearBody < (int)scene->bodies.size()) {
        // Del lado del sol (de dia), a 3 radios del centro y mirando al planeta.
        const CelestialBody& b = scene->bodies[nearBody];
        Vec3 center = b.position(elapsed);
        Vec3 towardSun = normalize(scene->sunPosition(elapsed) - center);
        float dist = nearDist > 0.0f ? nearDist : b.radius * 3.0f;
        ship.position = center + normalize(towardSun + Vec3{0.0f, 0.3f, 0.0f}) * dist;
        // Con --near-dist apunta adelante del planeta (a ~45 u/s llega cuando el planeta ya avanzo).
        ship.lookAt(nearDist > 0.0f ? b.position(elapsed + dist / 45.0f) : center);
    }
    if (startAtHole) ship = makeHoleViewShip(*scene, elapsed);
    if (testImpact) {
        // Se apunta a donde estara Venus cuando llegue la roca (el planeta se mueve).
        Vec3 from = ship.position + Vec3{2.0f, 3.0f, 0.0f};
        float speed = 40.0f;
        float travel = length(scene->bodies[HOME_VENUS].position(elapsed) - from) / speed;
        Vec3 aim = scene->bodies[HOME_VENUS].position(elapsed + travel);
        belt.launchForTest(from, normalize(aim - from) * speed);
    }
    if (testHit) belt.launchForTest(ship.position + ship.forward * 8.0f, Vec3{0, 0, 0});
    ChaseCamera camera;
    camera.snapTo(ship);

    // Al explotar, la camara se aleja del punto de impacto (hacia afuera del
    // cuerpo) y se queda mirando la explosion hasta que el carro reaparece.
    bool crashCameraPlaced = false;
    auto placeCrashCamera = [&](const std::vector<SphereCollider>& colliders) {
        if (!ship.crashed || crashCameraPlaced) return;
        Vec3 outward = normalize(ship.crashPoint - colliders[ship.crashBody].center);
        Vec3 dir = normalize(normalize(camera.eye - ship.crashPoint) + outward);
        camera.eye = ship.crashPoint + dir * 18.0f;
        camera.target = ship.crashPoint;
        crashCameraPlaced = true;
    };

    auto respawnShip = [&]() {
        ship = makeStartShip(*scene, elapsed);
        crashShown = false;
        crashCameraPlaced = false;
        camera.snapTo(ship);
    };

    // Viaje por el agujero negro: al cruzar el horizonte empieza la cuenta;
    // a la mitad se cambia de universo y el carro sale junto al agujero del
    // otro lado. Devuelve true en el cuadro en que se cambio.
    float warpTimer = 0.0f;
    bool warpSwapped = false;
    auto updateWarp = [&](float step) {
        if (warpTimer <= 0.0f) {
            const CelestialBody* hole = scene->blackHole();
            if (hole && !ship.crashed && touchesBlackHole(*hole, elapsed, ship.position, ship.tuning.collisionRadius)) {
                warpTimer = WARP_TIME;
                warpSwapped = false;
            }
            return false;
        }
        warpTimer = std::max(0.0f, warpTimer - step);
        if (warpSwapped || warpTimer > WARP_TIME * 0.5f) return false;
        ensureAlt();
        scene = scene == &homeScene ? &altScene : &homeScene;
        std::printf("Cruzaste el agujero negro: %s\n", scene->alternate ? "universo alterno" : "universo normal");
        ship = makeArrivalShip(*scene, elapsed);
        particles.clear();
        crashShown = false;
        crashCameraPlaced = false;
        camera.snapTo(ship);
        warpSwapped = true;
        return true;
    };

    // Aterrizar en Arena o en Entrenamiento: al tocar su superficie (el modo
    // superficie baja solo hasta 1.7) la pantalla destella y se pasa a su mundo
    // abierto. Con U se despega: el carro sale al espacio por encima de ese
    // planeta. Para no volver a aterrizar de inmediato, hay que alejarse antes
    // de poder aterrizar otra vez.
    bool inOpenWorld = false;
    OpenWorld* world = &arenaWorld; // el mundo abierto actual
    ArcadeGame* arcade = nullptr;   // el juego de arcade actual, si hay
    // Juego de arcade de cada planeta (nullptr = no tiene: es un mundo abierto).
    auto arcadeFor = [&](int body) -> ArcadeGame* {
        switch (body) {
            case ALT_PACMAN: return &pacman;
            case ALT_GALAGA: return &galaga;
            case ALT_CENTIPEDE: return &centipede;
            case ALT_SNAKE: return &snake;
            case ALT_DEMON: return &infierno;
            default: return nullptr;
        }
    };
    int worldBody = ALT_ARENA;      // el planeta de ese mundo
    float landTimer = 0.0f;
    bool landSwapped = false, landing = true, landArmed = true;
    auto startTakeOff = [&]() {
        if (!(inOpenWorld || arcade) || landTimer > 0.0f) return;
        landTimer = LAND_TIME;
        landSwapped = false;
        landing = false;
    };
    auto updateLanding = [&](float step) {
        if (landTimer <= 0.0f) {
            if (!scene->alternate || inOpenWorld || arcade) return;
            // Todos los planetas del universo alterno (menos la estrella y el agujero) tienen juego.
            bool nearGame = ship.nearest >= ALT_ARENA && ship.nearest != scene->blackHoleIndex;
            if (!nearGame || ship.altitude > scene->bodies[ship.nearest].radius * 2.6f) landArmed = true;
            if (landArmed && nearGame && ship.surfaceMode && !ship.crashed && ship.altitude < 2.2f) {
                landTimer = LAND_TIME;
                landSwapped = false;
                landing = true;
                worldBody = ship.nearest;
                world = worldBody == ALT_TRAINING ? &trainingWorld : &arenaWorld;
            }
            return;
        }
        landTimer = std::max(0.0f, landTimer - step);
        if (landSwapped || landTimer > LAND_TIME * 0.5f) return;
        landSwapped = true;
        particles.clear();
        if (landing) {
            if (arcadeFor(worldBody)) { // juego de arcade
                arcade = arcadeFor(worldBody);
                arcade->enter();
                std::printf("Aterrizaste en %s: a jugar\n", scene->bodies[worldBody].name.c_str());
            } else {
                inOpenWorld = true;
                world->enter();
                std::printf("Aterrizaste en %s: mundo abierto\n", scene->bodies[worldBody].name.c_str());
            }
        } else {
            // Sale del lado de Albor, alto y alejandose del planeta.
            inOpenWorld = false;
            arcade = nullptr;
            landArmed = false;
            const CelestialBody& planet = scene->bodies[worldBody];
            Vec3 center = planet.position(elapsed);
            Vec3 out = normalize(scene->sunPosition(elapsed) - center);
            ship = Ship{};
            ship.position = center + out * (planet.radius + 40.0f);
            ship.lookAt(ship.position + out);
            ship.velocity = out * 15.0f;
            camera.snapTo(ship);
            std::printf("Despegaste de %s\n", planet.name.c_str());
        }
    };

    // Un paso de la simulacion: carro, efectos, minijuegos, agujero negro y
    // camara. Lo usan el ciclo normal y el guion de vuelo de las pruebas.
    auto simulate = [&](const ShipInput& in, float step, bool looking, float mouseDx, float mouseDy, bool respawn) {
        if (inOpenWorld || arcade) {
            // Mientras destella el despegue el carro ya no se controla.
            ShipInput none;
            const ShipInput& use = landTimer > 0.0f ? none : in;
            if (arcade) arcade->update(use, step, particles);
            else world->update(use, step, particles);
            particles.update(step);
            updateLanding(step);
            return;
        }
        std::vector<SphereCollider> colliders = collidersAt(*scene, elapsed);
        if (warpTimer <= 0.0f) ship.update(in, step, colliders); // dentro del agujero el carro no se controla
        updateEffects(step, colliders);
        placeCrashCamera(colliders);
        if (respawn && ship.crashed && respawnTimer <= 0.0f) respawnShip(); // reaparece en el punto de inicio
        if (updateWarp(step)) colliders = collidersAt(*scene, elapsed);
        updateLanding(step);
        if (inOpenWorld || arcade) return;
        // En la cancha la camara va mas baja y mira al frente, para ver el balon.
        bool arenaCamera = scene->alternate && ship.nearest >= ALT_ARENA && ship.nearest != scene->blackHoleIndex;
        float k = 1.0f - std::exp(-3.0f * step);
        camera.surfaceHeight += ((arenaCamera ? 2.0f : 3.5f) - camera.surfaceHeight) * k;
        camera.surfaceLookDown += ((arenaCamera ? 0.2f : 2.5f) - camera.surfaceLookDown) * k;
        // Mientras esta destruido la camara se queda quieta mirando la explosion.
        if (!ship.crashed) camera.update(ship, step, looking, mouseDx, mouseDy);
        camera.keepOutside(colliders);
    };

    if (startArena || startTraining) {
        inOpenWorld = true;
        if (startTraining) {
            world = &trainingWorld;
            worldBody = ALT_TRAINING;
        }
        world->enter();
        if (startKickoff) world->placeAtKickoff();
    }
    if (startArcade) {
        worldBody = startArcade;
        arcade = arcadeFor(startArcade);
        arcade->enter();
    }

    // Modo de prueba: simula el guion de vuelo a 60 FPS antes de la captura
    // e imprime la altitud al final de cada tramo, y cuantos grados ha
    // recorrido el carro alrededor del planeta (360 = una vuelta completa).
    if (screenshotPath) {
        float degreesAround = 0.0f;
        const CelestialBody& around = scene->bodies[nearBody > 0 ? nearBody : scene->spawnLook];
        Vec3 prevDir = normalize(ship.position - around.position(elapsed));
        for (const auto& seg : parseFlightScript(flightScript)) {
            ShipInput in = inputFromString(seg.first);
            lastInput = in;
            if (seg.first.find('f') != std::string::npos) ship.surfaceEnabled = !ship.surfaceEnabled;
            if (seg.first.find('v') != std::string::npos) ship.gravityEnabled = !ship.gravityEnabled;
            if (seg.first.find('u') != std::string::npos) startTakeOff();
            if (seg.first.find('c') != std::string::npos) world->ballCamera = !world->ballCamera;
            int frames = (int)(seg.second * 60.0f + 0.5f);
            for (int i = 0; i < frames; ++i) {
                simulate(in, 1.0f / 60.0f, false, 0, 0, false);
                elapsed += 1.0f / 60.0f;
                Vec3 dir = normalize(ship.position - around.position(elapsed));
                degreesAround += std::acos(clampf(dot(prevDir, dir), -1.0f, 1.0f)) * 180.0f / PI;
                prevDir = dir;
            }
            std::printf("  tramo %-4s %.1f s -> altitud %.2f sobre %s, velocidad %.1f, %s, %.0f grados alrededor"
                        " | asteroides golpeados %d, explosiones %d\n",
                        seg.first.c_str(), seg.second, ship.altitude,
                        ship.nearest >= 0 ? scene->bodies[ship.nearest].name.c_str() : "-",
                        length(ship.velocity),
                        ship.crashed ? "EXPLOTO" : (ship.surfaceMode ? "superficie" : "libre"), degreesAround,
                        belt.flying(), belt.explosions());
            if (arcade) std::printf("         %s: %d puntos\n", arcade->name(), arcade->score());
            if (inOpenWorld) {
                const GroundCar& c = world->player();
                Vec3 ball = world->ballPosition();
                std::printf("         mundo abierto: carro (%.1f, %.2f, %.1f) vel %.1f boost %.0f %s | balon (%.1f, %.2f, %.1f)"
                            " toques %d | rival (%.1f, %.1f) | marcador %d - %d\n",
                            c.position.x, c.position.y, c.position.z, length(c.velocity), c.boost,
                            world->playerInStadium() ? "en el estadio" : "afuera", ball.x, ball.y, ball.z,
                            world->ballTouches(), world->opponent().position.x, world->opponent().position.z,
                            world->blueScore(), world->orangeScore());
                if (world->training())
                    std::printf("         entrenamiento: siguiente anillo %d, mejor tiempo %.2f, goles %d\n",
                                world->nextRing(), world->bestRingTime(), world->practiceGoals());
            }
        }
        camera.lookYaw = radians(lookYawDeg);
        camera.lookPitch = radians(lookPitchDeg);
        if (zoom > 0.0f) camera.distance = zoom;
        if (!ship.crashed) camera.snapTo(ship);
        camera.keepOutside(collidersAt(*scene, elapsed));
    }

    RenderStats stats;

    // Para el titulo de la ventana: promedio de FPS cada medio segundo.
    float titleTimer = 0.0f;
    float displayedFps = 0.0f;
    float lastFrameMs = 0.0f; // tiempo de render del cuadro anterior (para el HUD)
    // La ayuda se oculta sola a los 15 s, a menos que se use la tecla H.
    float helpTimer = 15.0f;
    bool helpTouched = false;
    int framesCounted = 0;

    SDL_GameController* pad = nullptr; // control de Xbox (o compatible), si hay uno conectado

    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;
    int screenshotFrame = 0; // en modo captura se guarda el tercer cuadro (los primeros calientan)
    while (running) {
        // Delta time: segundos reales desde el cuadro anterior. Todo lo que
        // se mueve se multiplica por dt para no depender de los FPS.
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.1f) dt = 0.1f; // evita saltos enormes si la ventana se congela
        if (screenshotPath) dt = 0.0f;
        elapsed += dt;

        float mouseDx = 0, mouseDy = 0;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            // SDL avisa de los controles ya conectados al iniciar y de los que se conectan despues.
            if (event.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                pad = SDL_GameControllerOpen(event.cdevice.which);
                if (pad) std::printf("Control conectado: %s\n", SDL_GameControllerName(pad));
            }
            if (event.type == SDL_CONTROLLERDEVICEREMOVED && pad &&
                event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                SDL_GameControllerClose(pad);
                pad = nullptr;
                std::printf("Control desconectado\n");
            }
            // Botones del control que no son de manejo: Y cambia el modo superficie
            // (para aterrizar), Start reinicia el juego o el partido y View despega.
            if (event.type == SDL_CONTROLLERBUTTONDOWN && pad &&
                event.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                switch (event.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_Y:
                        if (!inOpenWorld && !arcade) {
                            ship.surfaceEnabled = !ship.surfaceEnabled;
                            std::printf("Modo superficie: %s\n", ship.surfaceEnabled ? "activado" : "desactivado");
                        }
                        break;
                    case SDL_CONTROLLER_BUTTON_START:
                        if (arcade) arcade->restart();
                        else if (inOpenWorld) world->restartMatch();
                        break;
                    case SDL_CONTROLLER_BUTTON_BACK: startTakeOff(); break;
                }
            }
            if (event.type == SDL_MOUSEMOTION) {
                mouseDx += (float)event.motion.xrel;
                mouseDy += (float)event.motion.yrel;
            }
            // Rueda del raton: acercar o alejar la camara del carro (para ver detalles).
            if (event.type == SDL_MOUSEWHEEL) {
                camera.distance = clampf(camera.distance * (event.wheel.y > 0 ? 0.85f : 1.15f), 2.0f, 12.0f);
            }
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_l: opt.autoLod = !opt.autoLod; break;
                    case SDLK_o: opt.cullObjects = !opt.cullObjects; break;
                    case SDLK_n: scene->buildProps(scene->propSeed + 1); break; // otra semilla de distribucion
                    case SDLK_v: ship.gravityEnabled = !ship.gravityEnabled; break;
                    case SDLK_t: showOrbits = !showOrbits; break;
                    case SDLK_y: showTrajectory = !showTrajectory; break;
                    case SDLK_m: u.normalMapping = !u.normalMapping; break;
                    case SDLK_h:
                        showHelp = !showHelp;
                        helpTouched = true;
                        break;
                    case SDLK_g: // deformacion de Venus: los objetos se vuelven a apoyar en el suelo
                        scene->deformTerrain = !scene->deformTerrain;
                        scene->buildProps(scene->propSeed);
                        break;
                    case SDLK_f: ship.surfaceEnabled = !ship.surfaceEnabled; break;
                    case SDLK_b: u.cullBackFaces = !u.cullBackFaces; break;
                    case SDLK_u: startTakeOff(); break;
                    case SDLK_c: world->ballCamera = !world->ballCamera; break;
                    case SDLK_RETURN:
                        if (arcade) arcade->restart();
                        else if (inOpenWorld) world->restartMatch();
                        break;
                    case SDLK_r:
                        if (arcade) {
                            arcade->restart();
                            break;
                        }
                        if (inOpenWorld) {
                            world->respawn();
                            break;
                        }
                        respawnShip();
                        camera.keepOutside(collidersAt(*scene, elapsed));
                        break;
                    case SDLK_j: // junto al agujero negro de este universo, mirandolo
                        if (warpTimer <= 0.0f && !inOpenWorld && !arcade) {
                            respawnShip();
                            ship = makeHoleViewShip(*scene, elapsed);
                            camera.snapTo(ship);
                        }
                        break;
                    case SDLK_p:
                        if (fb.savePNG("captura.png")) std::printf("Captura guardada en captura.png\n");
                        break;
                }
            }
        }
        bool looking = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

        if (!screenshotPath) {
            lastInput = readKeyboard();
            if (arcade && arcade->firstPerson()) addGamepadShooter(pad, lastInput);
            else if (arcade) addGamepadArcade(pad, lastInput);
            else addGamepad(pad, lastInput);
            simulate(lastInput, dt, looking, mouseDx, mouseDy, true);
        }

        u.time = elapsed;
        Uint64 frameStart = SDL_GetPerformanceCounter();
        fb.clear();
        stats.reset();
        if (!screenshotPath && !helpTouched && helpTimer > 0.0f) {
            helpTimer -= dt;
            if (helpTimer <= 0.0f) showHelp = false;
        }
        // En modo captura no hay tiempo real: se estiman los FPS con el render.
        float shownFps = screenshotPath ? (lastFrameMs > 0.0f ? 1000.0f / lastFrameMs : 0.0f) : displayedFps;

        if (arcade) {
            // Juego de arcade: el tablero flota en el cielo morado del universo alterno.
            u.cameraPos = arcade->eye;
            u.view = lookAt(arcade->eye, arcade->target, Vec3{0, 1, 0});
            u.lightPos = Vec3{0.0f, 1000.0f, 0.0f};
            scene->skybox.draw(fb, u);
            scene->drawVoxelDecor(fb, u, stats);
            arcade->draw(fb, u, stats, elapsed);
            particles.draw(fb, u);
            arcade->drawHud(fb, shownFps, showHelp);
        } else if (inOpenWorld) {
            u.cameraPos = world->eye;
            u.view = lookAt(world->eye, world->target, Vec3{0, 1, 0});
            world->draw(fb, u, stats, carAssets, elapsed);
            particles.draw(fb, u);
            world->drawHud(fb, shownFps, lastFrameMs, stats, showHelp);
        } else {
            u.cameraPos = camera.eye;
            u.view = camera.view();
            u.lightPos = scene->sunPosition(elapsed);
            scene->skybox.draw(fb, u); // nebulosas, galaxias y Via Lactea de fondo
            scene->drawStars(fb, u);   // el universo alterno no tiene: sus estrellas son de cubos
            if (scene->alternate) scene->drawVoxelDecor(fb, u, stats);
            scene->drawBodies(fb, u, stats, opt);
            if (showOrbits) scene->drawOrbits(fb, u);
            if (scene == &homeScene) belt.draw(fb, u, stats, opt);
            // Al final del fondo: su lente dobla lo que ya esta dibujado detras.
            scene->drawBlackHoles(fb, u, stats, opt);

            // El carro tambien pasa por el descarte y el nivel de detalle: si
            // se ve pequeno (camara lejos) basta con el modelo reducido.
            if (ship.crashed) {
                // destruido: no se dibuja el carro
            } else if (!opt.cullObjects || sphereInFrustum(u, ship.position, carRadius)) {
                stats.objectsDrawn++;
                carUsesLod = opt.autoLod && projectedRadius(u, ship.position, carRadius, RENDER_HEIGHT) < CAR_LOD_PIXELS;
                u.model = ship.modelMatrix();
                drawMesh(fb, carUsesLod ? carLod : carFull, u, carShader, stats);
                // Boost como en Rocket League: una llama sale de cada escape del
                // carro. El cono se coloca en el escape, con la matriz del carro,
                // y su largo oscila con el tiempo.
                if (ship.boosting) {
                    float length = 0.75f + 0.2f * std::sin(elapsed * 31.0f);
                    for (const Vec3& exhaust : {EXHAUST_LEFT, EXHAUST_RIGHT}) {
                        u.model = ship.modelMatrix() * translate(exhaust) *
                                  scale3({EXHAUST_RADIUS, EXHAUST_RADIUS, length});
                        drawMesh(fb, boostFlame, u, flameShader, stats);
                    }
                }
            } else {
                stats.objectsCulled++;
            }

            // Trayectoria prevista: una copia del carro avanza 3 s con las mismas
            // teclas; cada punto marca donde estara. Con gravedad se ve como la
            // curvan los planetas y el sol.
            if (showTrajectory && ship.gravityEnabled && !ship.surfaceMode && !ship.crashed) {
                Ship ghost = ship;
                std::vector<SphereCollider> colliders = collidersAt(*scene, elapsed);
                for (int i = 1; i <= 30; ++i) {
                    ghost.update(lastInput, 0.1f, colliders);
                    if (ghost.surfaceMode) break;
                    uint32_t g = (uint32_t)(255 - i * 6);
                    drawPoint3D(fb, u, ghost.position, 0xFF000000u | (g / 3 << 16) | (g << 8) | g, i < 10 ? 3 : 2);
                }
            }

            // Etiquetas con el nombre y la distancia encima de cada cuerpo que se
            // ve (si no esta tan cerca que ocupe toda la pantalla).
            for (const CelestialBody& b : scene->bodies) {
                if (b.currentLod < 0) continue; // descartado: no esta en la vista
                Vec3 center = b.position(elapsed);
                Vec4 c = u.projection * u.view * Vec4(center, 1.0f);
                float r = projectedRadius(u, center, b.radius, RENDER_HEIGHT);
                float dist = length(center - ship.position);
                if (c.w <= 0.0f || r > 220.0f || dist < b.radius * 3.0f) continue;
                int sx = (int)((c.x / c.w * 0.5f + 0.5f) * RENDER_WIDTH);
                int sy = (int)((1.0f - (c.y / c.w * 0.5f + 0.5f)) * RENDER_HEIGHT - r) - 14;
                char label[64];
                std::snprintf(label, sizeof(label), "%s %.0f", b.name.c_str(), dist);
                drawText(fb, sx - textWidth(label) / 2, sy, label, 0xFFD8E0F0);
            }

            // Las particulas (estela y explosiones) se dibujan despues de toda la
            // escena 3D (usan su profundidad para que los planetas las tapen) y
            // antes del HUD.
            particles.draw(fb, u);

            // HUD con texto: navegacion, contadores del renderizador y ayuda.
            HudInfo hud;
            hud.fps = shownFps;
            hud.frameMs = lastFrameMs;
            hud.speed = length(ship.velocity);
            hud.maxSpeed = ship.tuning.maxBoostSpeed;
            hud.boostFraction = ship.boostFuel / ship.tuning.boostCapacity;
            hud.boosting = ship.boosting;
            hud.nearName = ship.nearest >= 0 ? scene->bodies[ship.nearest].name : "-";
            hud.altitude = ship.altitude;
            hud.surfaceMode = ship.surfaceMode;
            hud.surfaceEnabled = ship.surfaceEnabled;
            hud.gravity = ship.gravityEnabled;
            hud.relief = scene->deformTerrain;
            hud.normalMapping = u.normalMapping;
            hud.asteroids = belt.total();
            hud.asteroidsFlying = belt.flying();
            hud.explosions = belt.explosions();
            hud.opt = opt;
            hud.stats = stats;
            hud.seed = scene->propSeed;
            hud.showHelp = showHelp;
            hud.alternate = scene->alternate;
            drawHud(fb, hud);
            // Panel del minijuego al acercarse a su planeta.
            if (scene->alternate && !ship.crashed && ship.nearest >= 0) {
                bool close = ship.altitude < scene->bodies[ship.nearest].radius * 1.5f;
                // Titulo de cada planeta con juego (en el orden de sus indices).
                static const char* titles[] = {"", "ARENA - ESTADIO DE FUTBOL", "ENTRENAMIENTO - ANILLOS Y TIROS", "",
                                               "PAC-MAN - LABERINTO", "GALAGA - NAVES", "CENTIPEDE - ORUGA Y HONGOS",
                                               "SNAKE - SERPIENTE", "INFIERNO - DEMONIOS EN PRIMERA PERSONA"};
                int n = ship.nearest;
                if (n >= ALT_ARENA && n <= ALT_DEMON && n != scene->blackHoleIndex && close) {
                    const char* title = titles[n];
                    char msg[96];
                    if (ship.surfaceMode) std::snprintf(msg, sizeof(msg), "ATERRIZANDO EN %s...", scene->bodies[n].name.c_str());
                    else std::snprintf(msg, sizeof(msg), "BAJA A %s PARA JUGAR", scene->bodies[n].name.c_str());
                    darkenRect(fb, (RENDER_WIDTH - 340) / 2, 8, 340, 30, 0.55f);
                    drawText(fb, (RENDER_WIDTH - textWidth(title)) / 2, 12, title, 0xFF98A2B8);
                    drawText(fb, (RENDER_WIDTH - textWidth(msg)) / 2, 25, msg, 0xFFFFD860);
                }
            }
            if (ship.crashed) {
                const char* msg = "EL CARRO EXPLOTO";
                drawText(fb, (RENDER_WIDTH - textWidth(msg, 3)) / 2, RENDER_HEIGHT / 2 - 40, msg, 0xFFFF8040, 3);
                char sub[64];
                std::snprintf(sub, sizeof(sub), "REAPARECE EN %.0f  (R: AHORA)", std::max(0.0f, std::ceil(respawnTimer)));
                drawText(fb, (RENDER_WIDTH - textWidth(sub, 2)) / 2, RENDER_HEIGHT / 2, sub, 0xFFE8ECF4, 2);
            }
        }
        // Aterrizaje o despegue de Arena: destello verde claro.
        if (landTimer > 0.0f) {
            float half = LAND_TIME * 0.5f;
            float phase = 1.0f - std::fabs(landTimer - half) / half;
            tintScreen(fb, Vec3{0.9f, 1.0f, 0.92f}, phase * phase * (3.0f - 2.0f * phase));
            if (phase > 0.35f) {
                const char* msg = landing ? (landSwapped ? "MUNDO ABIERTO DE ARENA" : "ATERRIZANDO")
                                          : (landSwapped ? "DE VUELTA EN EL ESPACIO" : "DESPEGANDO");
                drawText(fb, (RENDER_WIDTH - textWidth(msg, 3)) / 2, RENDER_HEIGHT / 2 - 12, msg, 0xFF204030, 3);
            }
        }
        // Viaje por el agujero negro: la pantalla se pone blanca y vuelve.
        if (warpTimer > 0.0f) {
            float half = WARP_TIME * 0.5f;
            float phase = 1.0f - std::fabs(warpTimer - half) / half;
            tintScreen(fb, Vec3{0.92f, 0.95f, 1.0f}, phase * phase * (3.0f - 2.0f * phase));
            if (phase > 0.35f) {
                const char* msg = warpSwapped ? (scene->alternate ? "UNIVERSO ALTERNO" : "DE REGRESO A CASA")
                                              : "CRUZANDO EL HORIZONTE";
                drawText(fb, (RENDER_WIDTH - textWidth(msg, 3)) / 2, RENDER_HEIGHT / 2 - 12, msg, 0xFF203050, 3);
            }
        }
        double frameMs = 1000.0 * (SDL_GetPerformanceCounter() - frameStart) / SDL_GetPerformanceFrequency();
        lastFrameMs = (float)frameMs;

        if (screenshotPath && ++screenshotFrame >= 3) {
            fb.savePNG(screenshotPath);
            std::printf("%s: render %.1f ms | objetos %d dibujados, %d descartados | objetos de superficie %d | "
                        "triangulos enviados %d, rasterizados %d | LOD (-1 = descartado) Sol %d, Venus %d, "
                        "Tierra %d, Marte %d, carro %s\n",
                        screenshotPath, frameMs, stats.objectsDrawn, stats.objectsCulled, stats.propsDrawn,
                        stats.trianglesIn, stats.trianglesDrawn, scene->bodies[0].currentLod,
                        scene->bodies[HOME_VENUS].currentLod, scene->bodies[HOME_EARTH].currentLod,
                        scene->bodies[HOME_MARS].currentLod,
                        carUsesLod ? "reducido" : "completo");
            running = false;
        }

        fb.renderBuffer(renderer, texture);

        framesCounted++;
        titleTimer += dt;
        if (titleTimer >= 0.5f) {
            displayedFps = framesCounted / titleTimer;
            char title[320];
            float speed = length(ship.velocity);
            const char* nearName = ship.nearest >= 0 ? scene->bodies[ship.nearest].name.c_str() : "-";
            std::snprintf(title, sizeof(title),
                          "Proyecto 3 - Galaxia%s | %.0f FPS | vel %.1f | boost %.0f%% | %s a %.1f%s | "
                          "objetos %d (descartados %d) | superficie %d (semilla %u) | tris %d / raster %d | "
                          "descarte %s, LOD %s, relieve %s, gravedad %s",
                          scene->alternate ? " (universo alterno)" : "", framesCounted / titleTimer, speed, 100.0f * ship.boostFuel / ship.tuning.boostCapacity,
                          nearName, ship.altitude, ship.surfaceMode ? " (superficie)" : "", stats.objectsDrawn,
                          stats.objectsCulled, stats.propsDrawn, scene->propSeed, stats.trianglesIn,
                          stats.trianglesDrawn, opt.cullObjects ? "si" : "no", opt.autoLod ? "si" : "no",
                          scene->deformTerrain ? "si" : "no", ship.gravityEnabled ? "si" : "no");
            if (arcade) {
                std::snprintf(title, sizeof(title), "Proyecto 3 - Galaxia (%s) | %.0f FPS | puntos %d | tris %d",
                              arcade->name(), framesCounted / titleTimer, arcade->score(), stats.trianglesIn);
            } else if (inOpenWorld) {
                std::snprintf(title, sizeof(title),
                              "Proyecto 3 - Galaxia (mundo abierto de %s) | %.0f FPS | azul %d - %d naranja | "
                              "vel %.1f | boost %.0f | tris %d / raster %d",
                              world->training() ? "Entrenamiento" : "Arena", framesCounted / titleTimer,
                              world->blueScore(), world->orangeScore(), length(world->player().velocity),
                              world->player().boost, stats.trianglesIn, stats.trianglesDrawn);
            }
            SDL_SetWindowTitle(window, title);
            titleTimer = 0.0f;
            framesCounted = 0;
        }
    }

    if (pad) SDL_GameControllerClose(pad);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
