#include <SDL.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
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
#include "boost_flame.h"
#include "particles.h"

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

// Cinturon de asteroides: cuantas rocas y con que semilla se reparten.
const int ASTEROID_COUNT = 220;
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
// b = boost, x = freno fuerte. En el guion de vuelo, f cambia el modo superficie
// y v la gravedad.
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
    in.steer = (float)k[SDL_SCANCODE_A] - (float)k[SDL_SCANCODE_D];
    in.pitch = (float)k[SDL_SCANCODE_UP] - (float)k[SDL_SCANCODE_DOWN];
    in.roll = (float)k[SDL_SCANCODE_E] - (float)k[SDL_SCANCODE_Q];
    in.boost = k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT];
    in.brake = k[SDL_SCANCODE_SPACE];
    return in;
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
        c.slowZone = sun ? 0.0f : 2.5f * b.radius;
        c.surfaceZone = sun ? 0.0f : 1.5f * b.radius;
        c.gravityRange = (sun ? 5.0f : 3.0f) * b.radius;
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
    ship.position = CAR_START;
    ship.lookAt(scene.bodies[1].position(time)); // arranca mirando al planeta
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
    //   --near N                  empieza frente al lado de dia del cuerpo N (1 = Duna, 2 = Glacia...)
    //   --seed N                  semilla para repartir los objetos de superficie
    //   --flat                    Duna sin deformar (esfera), para comparar
    //   --hide-help               empieza sin el panel de controles
    //   --test-impact             lanza un asteroide hacia Duna (para probar la explosion)
    //   --test-hit                pone un asteroide quieto delante del carro (para chocarlo)
    //   --no-normal-map           sin mapas de normales en el carro, para comparar
    //   --dt-test                 prueba de consistencia con distintos FPS
    const char* screenshotPath = nullptr;
    float elapsed = 0.0f, lookYawDeg = 0.0f, lookPitchDeg = 0.0f, zoom = 0.0f;
    std::string flightScript;
    bool dtTest = false;
    int nearBody = -1;
    unsigned propSeed = 2026;
    bool flatTerrain = false;
    bool showHelp = true;
    bool testImpact = false, testHit = false;
    bool showOrbits = true;     // tecla T: lineas de las orbitas de los planetas
    bool showTrajectory = true; // tecla Y: puntos de la trayectoria prevista del carro
    bool noNormalMap = false;
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
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) propSeed = (unsigned)std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--flat") == 0) flatTerrain = true;
        else if (std::strcmp(argv[i], "--hide-help") == 0) showHelp = false;
        else if (std::strcmp(argv[i], "--test-impact") == 0) testImpact = true;
        else if (std::strcmp(argv[i], "--test-hit") == 0) testHit = true;
        else if (std::strcmp(argv[i], "--no-normal-map") == 0) noNormalMap = true;
        else if (std::strcmp(argv[i], "--dt-test") == 0) dtTest = true;
    }

    // Construir la escena incluye hornear las superficies del sol y Duna.
    auto buildStart = std::chrono::steady_clock::now();
    Scene scene;
    scene.build(propSeed);
    if (flatTerrain) {
        scene.deformTerrain = false;
        scene.buildProps(propSeed);
    }
    std::printf("Escena lista (texturas horneadas) en %.0f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count());
    if (dtTest) {
        runDtTest(scene);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    Mesh carFull, carLod;
    if (!loadCar("assets/models/fennec.obj", carFull) || !loadCar("assets/models/fennec_lod.obj", carLod)) {
        SDL_Quit();
        return 1;
    }
    const float carRadius = carFull.boundingRadius();
    // Llama del boost (sale de los escapes del Fennec).
    const Mesh boostFlame = makeFlame({1.0f, 0.45f, 0.08f});
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

    Ship ship = makeStartShip(scene, elapsed);
    ShipInput lastInput; // teclas del ultimo cuadro, para predecir la trayectoria

    // Particulas (estela del boost y explosiones) y cinturon de asteroides.
    Particles particles;
    AsteroidBelt belt;
    belt.build(ASTEROID_SEED, scene.rockModels, ASTEROID_COUNT);

    // Despues de mover el carro: choques con asteroides, vuelo de los
    // asteroides (y sus explosiones) y la estela del boost. Mientras haya
    // boost, cada escape suelta particulas hacia atras; como quedan en el
    // mundo, forman un rastro detras del carro.
    const Vec3 exhausts[2] = {EXHAUST_LEFT, EXHAUST_RIGHT};
    Vec3 lastExhaust[2];      // donde estaban los escapes en el cuadro anterior
    bool wasBoosting = false;
    auto updateEffects = [&](float step, const std::vector<SphereCollider>& colliders) {
        belt.collideShip(ship);
        belt.update(elapsed, step, colliders, particles);
        if (ship.boosting) {
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
    if (nearBody > 0 && nearBody < (int)scene.bodies.size()) {
        // Del lado del sol (de dia), a 3 radios del centro y mirando al planeta.
        const CelestialBody& b = scene.bodies[nearBody];
        Vec3 center = b.position(elapsed);
        Vec3 towardSun = normalize(scene.sunPosition(elapsed) - center);
        ship.position = center + normalize(towardSun + Vec3{0.0f, 0.3f, 0.0f}) * (b.radius * 3.0f);
        ship.lookAt(center);
    }
    if (testImpact) {
        // Se apunta a donde estara Duna cuando llegue la roca (el planeta se mueve).
        Vec3 from = ship.position + Vec3{2.0f, 3.0f, 0.0f};
        float speed = 40.0f;
        float travel = length(scene.bodies[1].position(elapsed) - from) / speed;
        Vec3 aim = scene.bodies[1].position(elapsed + travel);
        belt.launchForTest(from, normalize(aim - from) * speed);
    }
    if (testHit) belt.launchForTest(ship.position + ship.forward * 8.0f, Vec3{0, 0, 0});
    ChaseCamera camera;
    camera.snapTo(ship);

    // Modo de prueba: simula el guion de vuelo a 60 FPS antes de la captura
    // e imprime la altitud al final de cada tramo, y cuantos grados ha
    // recorrido el carro alrededor del planeta (360 = una vuelta completa).
    if (screenshotPath) {
        float degreesAround = 0.0f;
        const CelestialBody& around = scene.bodies[nearBody > 0 ? nearBody : 1];
        Vec3 prevDir = normalize(ship.position - around.position(elapsed));
        for (const auto& seg : parseFlightScript(flightScript)) {
            ShipInput in = inputFromString(seg.first);
            lastInput = in;
            if (seg.first.find('f') != std::string::npos) ship.surfaceEnabled = !ship.surfaceEnabled;
            if (seg.first.find('v') != std::string::npos) ship.gravityEnabled = !ship.gravityEnabled;
            int frames = (int)(seg.second * 60.0f + 0.5f);
            for (int i = 0; i < frames; ++i) {
                std::vector<SphereCollider> colliders = collidersAt(scene, elapsed);
                ship.update(in, 1.0f / 60.0f, colliders);
                updateEffects(1.0f / 60.0f, colliders);
                camera.update(ship, 1.0f / 60.0f, false, 0, 0);
                elapsed += 1.0f / 60.0f;
                Vec3 dir = normalize(ship.position - around.position(elapsed));
                degreesAround += std::acos(clampf(dot(prevDir, dir), -1.0f, 1.0f)) * 180.0f / PI;
                prevDir = dir;
            }
            std::printf("  tramo %-4s %.1f s -> altitud %.2f sobre %s, velocidad %.1f, %s, %.0f grados alrededor"
                        " | asteroides golpeados %d, explosiones %d\n",
                        seg.first.c_str(), seg.second, ship.altitude, scene.bodies[ship.nearest].name.c_str(),
                        length(ship.velocity), ship.surfaceMode ? "superficie" : "libre", degreesAround,
                        belt.flying(), belt.explosions());
        }
        camera.lookYaw = radians(lookYawDeg);
        camera.lookPitch = radians(lookPitchDeg);
        if (zoom > 0.0f) camera.distance = zoom;
        camera.snapTo(ship);
        camera.keepOutside(collidersAt(scene, elapsed));
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
                    case SDLK_n: scene.buildProps(scene.propSeed + 1); break; // otra semilla de distribucion
                    case SDLK_v: ship.gravityEnabled = !ship.gravityEnabled; break;
                    case SDLK_t: showOrbits = !showOrbits; break;
                    case SDLK_y: showTrajectory = !showTrajectory; break;
                    case SDLK_m: u.normalMapping = !u.normalMapping; break;
                    case SDLK_h:
                        showHelp = !showHelp;
                        helpTouched = true;
                        break;
                    case SDLK_g: // deformacion de Duna: los objetos se vuelven a apoyar en el suelo
                        scene.deformTerrain = !scene.deformTerrain;
                        scene.buildProps(scene.propSeed);
                        break;
                    case SDLK_f: ship.surfaceEnabled = !ship.surfaceEnabled; break;
                    case SDLK_b: u.cullBackFaces = !u.cullBackFaces; break;
                    case SDLK_r:
                        ship = makeStartShip(scene, elapsed);
                        camera.snapTo(ship);
                        camera.keepOutside(collidersAt(scene, elapsed));
                        break;
                    case SDLK_p:
                        if (fb.savePNG("captura.png")) std::printf("Captura guardada en captura.png\n");
                        break;
                }
            }
        }
        bool looking = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;

        if (!screenshotPath) {
            std::vector<SphereCollider> colliders = collidersAt(scene, elapsed);
            lastInput = readKeyboard();
            ship.update(lastInput, dt, colliders);
            updateEffects(dt, colliders);
            camera.update(ship, dt, looking, mouseDx, mouseDy);
            camera.keepOutside(colliders);
        }

        u.cameraPos = camera.eye;
        u.view = camera.view();
        u.time = elapsed;
        u.lightPos = scene.sunPosition(elapsed);

        Uint64 frameStart = SDL_GetPerformanceCounter();
        fb.clear();
        stats.reset();
        scene.drawStars(fb, u);
        scene.drawBodies(fb, u, stats, opt);
        if (showOrbits) scene.drawOrbits(fb, u);
        belt.draw(fb, u, stats, opt);

        // El carro tambien pasa por el descarte y el nivel de detalle: si
        // se ve pequeno (camara lejos) basta con el modelo reducido.
        if (!opt.cullObjects || sphereInFrustum(u, ship.position, carRadius)) {
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
        if (showTrajectory && ship.gravityEnabled && !ship.surfaceMode) {
            Ship ghost = ship;
            std::vector<SphereCollider> colliders = collidersAt(scene, elapsed);
            for (int i = 1; i <= 30; ++i) {
                ghost.update(lastInput, 0.1f, colliders);
                if (ghost.surfaceMode) break;
                uint32_t g = (uint32_t)(255 - i * 6);
                drawPoint3D(fb, u, ghost.position, 0xFF000000u | (g / 3 << 16) | (g << 8) | g, i < 10 ? 3 : 2);
            }
        }

        // Etiquetas con el nombre y la distancia encima de cada cuerpo que se
        // ve (si no esta tan cerca que ocupe toda la pantalla).
        for (const CelestialBody& b : scene.bodies) {
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
        float speed = length(ship.velocity);
        if (!screenshotPath && !helpTouched && helpTimer > 0.0f) {
            helpTimer -= dt;
            if (helpTimer <= 0.0f) showHelp = false;
        }
        HudInfo hud;
        // En modo captura no hay tiempo real: se estiman los FPS con el render.
        hud.fps = screenshotPath ? (lastFrameMs > 0.0f ? 1000.0f / lastFrameMs : 0.0f) : displayedFps;
        hud.frameMs = lastFrameMs;
        hud.speed = speed;
        hud.maxSpeed = ship.tuning.maxBoostSpeed;
        hud.boostFraction = ship.boostFuel / ship.tuning.boostCapacity;
        hud.boosting = ship.boosting;
        hud.nearName = ship.nearest >= 0 ? scene.bodies[ship.nearest].name : "-";
        hud.altitude = ship.altitude;
        hud.surfaceMode = ship.surfaceMode;
        hud.surfaceEnabled = ship.surfaceEnabled;
        hud.gravity = ship.gravityEnabled;
        hud.relief = scene.deformTerrain;
        hud.normalMapping = u.normalMapping;
        hud.asteroids = belt.total();
        hud.asteroidsFlying = belt.flying();
        hud.explosions = belt.explosions();
        hud.opt = opt;
        hud.stats = stats;
        hud.seed = scene.propSeed;
        hud.showHelp = showHelp;
        drawHud(fb, hud);
        double frameMs = 1000.0 * (SDL_GetPerformanceCounter() - frameStart) / SDL_GetPerformanceFrequency();
        lastFrameMs = (float)frameMs;

        if (screenshotPath && ++screenshotFrame >= 3) {
            fb.savePNG(screenshotPath);
            std::printf("%s: render %.1f ms | objetos %d dibujados, %d descartados | objetos de superficie %d | "
                        "triangulos enviados %d, rasterizados %d | LOD (-1 = descartado) Sol %d, Duna %d, "
                        "Glacia %d, Vergel %d, carro %s\n",
                        screenshotPath, frameMs, stats.objectsDrawn, stats.objectsCulled, stats.propsDrawn,
                        stats.trianglesIn, stats.trianglesDrawn, scene.bodies[0].currentLod,
                        scene.bodies[1].currentLod, scene.bodies[2].currentLod, scene.bodies[3].currentLod,
                        carUsesLod ? "reducido" : "completo");
            running = false;
        }

        fb.renderBuffer(renderer, texture);

        framesCounted++;
        titleTimer += dt;
        if (titleTimer >= 0.5f) {
            displayedFps = framesCounted / titleTimer;
            char title[320];
            const char* nearName = ship.nearest >= 0 ? scene.bodies[ship.nearest].name.c_str() : "-";
            std::snprintf(title, sizeof(title),
                          "Proyecto 3 - Galaxia | %.0f FPS | vel %.1f | boost %.0f%% | %s a %.1f%s | "
                          "objetos %d (descartados %d) | superficie %d (semilla %u) | tris %d / raster %d | "
                          "descarte %s, LOD %s, relieve %s, gravedad %s",
                          framesCounted / titleTimer, speed, 100.0f * ship.boostFuel / ship.tuning.boostCapacity,
                          nearName, ship.altitude, ship.surfaceMode ? " (superficie)" : "", stats.objectsDrawn,
                          stats.objectsCulled, stats.propsDrawn, scene.propSeed, stats.trianglesIn,
                          stats.trianglesDrawn, opt.cullObjects ? "si" : "no", opt.autoLod ? "si" : "no",
                          scene.deformTerrain ? "si" : "no", ship.gravityEnabled ? "si" : "no");
            SDL_SetWindowTitle(window, title);
            titleTimer = 0.0f;
            framesCounted = 0;
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
