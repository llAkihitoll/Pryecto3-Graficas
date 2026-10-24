#include "spacekit.h"
#include "mesh_builder.h"

// Las medidas estan en las coordenadas del carro: mide ~1.4 de largo (la
// parte trasera esta en z = +0.70), ~0.72 de ancho y ~0.52 de alto.
Mesh makeSpaceKit() {
    MeshBuilder b;
    int metal = b.material("Kit metal", {0.32f, 0.34f, 0.38f}, 0.6f, 48.0f);
    int glow = b.material("Kit tobera", {1.0f, 0.55f, 0.15f}, 0.0f, 1.0f, 1.0f);
    int white = b.material("Kit alas", {0.92f, 0.93f, 0.95f}, 0.5f, 40.0f);
    int accent = b.material("Kit franja", {1.0f, 0.45f, 0.08f}, 0.3f, 24.0f);

    // rotateX(90 grados) lleva el eje Y de las piezas al eje Z del carro:
    // los propulsores quedan acostados apuntando hacia atras.
    const Mat4 alongZ = rotateX(PI / 2);
    for (float side : {-1.0f, 1.0f}) {
        Mat4 at = translate({side * 0.30f, 0.03f, 0.0f}) * alongZ;
        b.frustum(10, 0.07f, 0.10f, 0.42f, 0.80f, metal, at);  // cuerpo con tobera ensanchada
        b.frustum(10, 0.075f, 0.075f, 0.79f, 0.805f, glow, at); // interior incandescente

        // Alerones en flecha (vistos desde arriba: x hacia afuera, z hacia atras).
        std::vector<Vec2> wing = {{0.33f, -0.05f}, {0.78f, 0.32f}, {0.78f, 0.46f}, {0.33f, 0.42f}};
        std::vector<Vec2> tip = {{0.70f, 0.25f}, {0.79f, 0.32f}, {0.79f, 0.47f}, {0.70f, 0.45f}};
        for (Vec2& p : wing) p.x *= side;
        for (Vec2& p : tip) p.x *= side;
        b.slab(wing, 0.0f, 0.03f, white);
        b.slab(tip, -0.004f, 0.034f, accent); // un poco mas gruesa para que tape al ala
    }

    // Aleta dorsal: una placa vertical sobre el techo. Se arma acostada
    // (x = altura) y se gira 90 grados alrededor de Z para pararla.
    std::vector<Vec2> fin = {{0.0f, 0.02f}, {0.0f, 0.36f}, {0.15f, 0.40f}};
    b.slab(fin, -0.012f, 0.012f, accent, translate({0.0f, 0.235f, 0.0f}) * rotateZ(PI / 2));
    return b.mesh;
}

Mesh makeFlame(Vec3 tint) {
    MeshBuilder b;
    int mat = b.material("Llama", tint, 0.0f, 1.0f, 1.0f);
    b.frustum(12, 1.0f, 0.0f, 0.0f, 1.0f, mat, rotateX(PI / 2));
    return b.mesh;
}
