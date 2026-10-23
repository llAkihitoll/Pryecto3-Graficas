#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "framebuffer.h"
#include "obj_loader.h"
#include "renderer.h"
#include "shaders.h"

// Resolucion interna del framebuffer. La ventana es mas grande y SDL solo
// estira la imagen ya terminada al mostrarla.
const int RENDER_WIDTH = 960;
const int RENDER_HEIGHT = 540;
const int WINDOW_WIDTH = 1280;
const int WINDOW_HEIGHT = 720;

const float FOV_DEG = 60.0f;
const float Z_NEAR = 0.05f;
const float Z_FAR = 5000.0f;

// Ajusta como reacciona cada material del Fennec a la luz.
void setupCarMaterials(Mesh& car) {
    for (Material& m : car.materials) {
        if (m.name == "Fennec - Body") { m.specular = 0.6f; m.shininess = 48.0f; }
        else if (m.name == "Window") { m.color = Vec3{0.03f, 0.04f, 0.06f}; m.specular = 0.9f; m.shininess = 96.0f; }
        else if (m.name == "Headlights") { m.emissive = 1.0f; }
        else if (m.name == "Alpha Rim") { m.specular = 0.8f; m.shininess = 64.0f; }
        else { m.specular = 0.15f; m.shininess = 16.0f; } // chasis, pintura negra, llantas
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

int main(int argc, char* argv[]) {
    // Opciones para pruebas: --screenshot archivo.png guarda un cuadro y
    // termina; --yaw/--pitch/--dist colocan la camara; --lod usa el modelo reducido.
    const char* screenshotPath = nullptr;
    // yaw = 0 pone la camara en +Z, detras del carro (el frente mira a -Z).
    float yaw = radians(215.0f), pitch = radians(20.0f), distance = 2.2f;
    bool useLod = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshotPath = argv[++i];
        else if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) yaw = radians((float)std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--pitch") == 0 && i + 1 < argc) pitch = radians((float)std::atof(argv[++i]));
        else if (std::strcmp(argv[i], "--dist") == 0 && i + 1 < argc) distance = (float)std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--lod") == 0) useLod = true;
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
    u.projection = perspective(radians(FOV_DEG), (float)RENDER_WIDTH / RENDER_HEIGHT, Z_NEAR, Z_FAR);
    u.lightPos = Vec3{-30.0f, 25.0f, 20.0f};

    RenderStats stats;
    bool spin = false;
    float carAngle = 0.0f;
    float elapsed = 0.0f;

    // Para el titulo de la ventana: promedio de FPS cada medio segundo.
    float titleTimer = 0.0f;
    int framesCounted = 0;

    Uint64 last = SDL_GetPerformanceCounter();
    bool running = true;
    while (running) {
        // Delta time: segundos reales desde el cuadro anterior. Todo lo que
        // se mueve se multiplica por dt para no depender de los FPS.
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.1f) dt = 0.1f; // evita saltos enormes si la ventana se congela
        elapsed += dt;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_l: useLod = !useLod; break;
                    case SDLK_b: u.cullBackFaces = !u.cullBackFaces; break;
                    case SDLK_r: spin = !spin; break;
                    case SDLK_p:
                        if (fb.savePNG("captura.png")) std::printf("Captura guardada en captura.png\n");
                        break;
                }
            }
            if (event.type == SDL_MOUSEWHEEL) distance *= event.wheel.y > 0 ? 0.9f : 1.1f;
        }

        // Camara orbital alrededor del carro (temporal, hasta la fase de controles).
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float orbitSpeed = 1.5f; // radianes por segundo
        if (keys[SDL_SCANCODE_LEFT]) yaw -= orbitSpeed * dt;
        if (keys[SDL_SCANCODE_RIGHT]) yaw += orbitSpeed * dt;
        if (keys[SDL_SCANCODE_UP]) pitch += orbitSpeed * dt;
        if (keys[SDL_SCANCODE_DOWN]) pitch -= orbitSpeed * dt;
        if (keys[SDL_SCANCODE_EQUALS] || keys[SDL_SCANCODE_KP_PLUS]) distance *= 1.0f - 1.5f * dt;
        if (keys[SDL_SCANCODE_MINUS] || keys[SDL_SCANCODE_KP_MINUS]) distance *= 1.0f + 1.5f * dt;
        pitch = clampf(pitch, radians(-85.0f), radians(85.0f));
        distance = clampf(distance, 0.3f, 50.0f);
        if (spin) carAngle += 0.6f * dt;

        Vec3 eye{distance * std::cos(pitch) * std::sin(yaw), distance * std::sin(pitch),
                 distance * std::cos(pitch) * std::cos(yaw)};
        u.cameraPos = eye;
        u.view = lookAt(eye, Vec3{0, 0, 0}, Vec3{0, 1, 0});
        u.model = rotateY(carAngle);
        u.time = elapsed;

        Uint64 frameStart = SDL_GetPerformanceCounter();
        fb.clear();
        stats.reset();
        drawMesh(fb, useLod ? carLod : carFull, u, carShader, stats);
        double frameMs = 1000.0 * (SDL_GetPerformanceCounter() - frameStart) / SDL_GetPerformanceFrequency();

        if (screenshotPath) {
            fb.savePNG(screenshotPath);
            std::printf("%s: render %.1f ms, triangulos enviados %d, rasterizados %d, pixeles %lld\n",
                        screenshotPath, frameMs, stats.trianglesIn, stats.trianglesDrawn, stats.pixelsShaded);
            running = false;
        }

        fb.renderBuffer(renderer, texture);

        framesCounted++;
        titleTimer += dt;
        if (titleTimer >= 0.5f) {
            char title[160];
            std::snprintf(title, sizeof(title), "Proyecto 3 - Galaxia | %.0f FPS | %s | tris %d / raster %d | culling %s",
                          framesCounted / titleTimer, useLod ? "LOD" : "completo", stats.trianglesIn,
                          stats.trianglesDrawn, u.cullBackFaces ? "si" : "no");
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
