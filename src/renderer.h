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
    // Giro extra para una capa de textura animada (las nubes de Vergel): se
    // calcula una vez por cuadro para no hacer senos y cosenos por pixel.
    Mat4 textureMotion = Mat4::identity();
    Vec3 objectCenter; // centro del planeta sobre el que estan los objetos de superficie
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
    int objectsDrawn = 0;   // objetos que pasaron el descarte
    int objectsCulled = 0;  // objetos descartados por estar fuera de la vista
    int propsDrawn = 0;     // objetos de superficie (rocas, arboles...) dibujados
    int trianglesIn = 0;    // triangulos enviados
    int trianglesDrawn = 0; // triangulos que llegaron a rasterizarse
    long long pixelsShaded = 0;

    void reset() { *this = RenderStats{}; }
};

// Pipeline completo de una malla: vertices -> espacio de recorte -> recorte
// contra el plano cercano -> division de perspectiva -> pantalla -> raster
// con prueba de profundidad -> shader de fragmentos.
void drawMesh(Framebuffer& fb, const Mesh& mesh, const Uniforms& u, FragmentShader shader, RenderStats& stats);

// Optimizaciones que se pueden apagar para comparar el trabajo (teclas O y L).
struct Optimizations {
    bool cullObjects = true; // descartar objetos fuera de la vista
    bool autoLod = true;     // elegir el nivel de detalle segun la distancia
};

// Descarte de objetos: true si la esfera (centro en el mundo, radio) toca el
// volumen de vision de la camara descrita por u.view y u.projection.
bool sphereInFrustum(const Uniforms& u, const Vec3& center, float radius);

// Dibuja un punto 3D (cuadradito de "size" pixeles) con prueba de
// profundidad, sin escribirla: lo tapa lo que este delante. Se usa para la
// trayectoria prevista del carro.
void drawPoint3D(Framebuffer& fb, const Uniforms& u, const Vec3& world, uint32_t argb, int size);

// Cuantos pixeles de radio ocupa en pantalla una esfera: depende de su
// tamano y de su distancia a la camara. Se usa para elegir el nivel de detalle.
float projectedRadius(const Uniforms& u, const Vec3& center, float radius, int screenHeight);
