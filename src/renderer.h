#pragma once
#include "framebuffer.h"
#include "mesh.h"

struct PlanetTextures; // texturas horneadas de un planeta (planets.h)

// Valores que son iguales para toda la malla mientras se dibuja.
struct Uniforms {
    Mat4 model = Mat4::identity();
    Mat4 view = Mat4::identity();
    Mat4 projection = Mat4::identity();
    Vec3 cameraPos;
    Vec3 lightPos;      // posicion de la luz (el sol) en el mundo
    float time = 0.0f;  // segundos desde que arranco el programa
    bool cullBackFaces = true;
    const PlanetTextures* textures = nullptr; // solo para sol y planetas
};

// Lo que recibe el shader de fragmentos por cada pixel cubierto. worldPos,
// localPos y normal se interpolan con correccion de perspectiva entre los 3
// vertices. localPos es la posicion en el espacio del modelo: sirve para
// que una textura gire junto con el planeta.
struct Fragment {
    Vec3 worldPos;
    Vec3 localPos;
    Vec3 normal;
    const Material* material;
    int x, y;
};

// Un shader de fragmentos devuelve el color final (0..1) del pixel.
using FragmentShader = Vec3 (*)(const Fragment&, const Uniforms&);

// Contadores para verificar cuanto trabajo hace el renderizador.
struct RenderStats {
    int trianglesIn = 0;    // triangulos enviados
    int trianglesDrawn = 0; // triangulos que llegaron a rasterizarse
    long long pixelsShaded = 0;

    void reset() { *this = RenderStats{}; }
};

// Pipeline completo de una malla: vertices -> espacio de recorte -> recorte
// contra el plano cercano -> division de perspectiva -> pantalla -> raster
// con prueba de profundidad -> shader de fragmentos.
void drawMesh(Framebuffer& fb, const Mesh& mesh, const Uniforms& u, FragmentShader shader, RenderStats& stats);
