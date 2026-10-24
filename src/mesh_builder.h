#pragma once
#include <algorithm>
#include <vector>
#include "geometry.h"
#include "noise.h"

// Ayudante para armar modelos en codigo con caras planas (estilo low poly):
// cada triangulo tiene sus propios vertices con la normal de la cara. Lo usan
// los objetos de superficie y el kit espacial del carro.
struct MeshBuilder {
    Mesh mesh;

    int material(const char* name, Vec3 color, float specular, float shininess, float emissive = 0.0f) {
        Material m{name, color};
        m.specular = specular;
        m.shininess = shininess;
        m.emissive = emissive;
        mesh.materials.push_back(m);
        return (int)mesh.materials.size() - 1;
    }

    // Agrega el triangulo mirando hacia afuera: si su normal apunta hacia el
    // punto "inside" (el centro de la pieza), se invierte el orden.
    void triangle(Vec3 a, Vec3 b, Vec3 c, int mat, const Vec3& inside) {
        Vec3 n = cross(b - a, c - a);
        if (dot(n, (a + b + c) * (1.0f / 3.0f) - inside) < 0) {
            std::swap(b, c);
            n = -n;
        }
        n = normalize(n);
        int base = (int)mesh.vertices.size();
        mesh.vertices.push_back(Vertex{a, n});
        mesh.vertices.push_back(Vertex{b, n});
        mesh.vertices.push_back(Vertex{c, n});
        mesh.triangles.push_back(Triangle{base, base + 1, base + 2, mat});
    }

    // Prisma o tronco de cono de "sides" lados, de y0 (radio r0) a y1 (radio
    // r1), con tapas. xf permite inclinarlo o moverlo.
    void frustum(int sides, float r0, float r1, float y0, float y1, int mat, const Mat4& xf = Mat4::identity()) {
        Vec3 inside = xf.transformPoint({0, (y0 + y1) * 0.5f, 0});
        Vec3 bottomCenter = xf.transformPoint({0, y0, 0}), topCenter = xf.transformPoint({0, y1, 0});
        for (int i = 0; i < sides; ++i) {
            float a0 = 2.0f * PI * i / sides, a1 = 2.0f * PI * (i + 1) / sides;
            Vec3 b0 = xf.transformPoint({r0 * std::cos(a0), y0, r0 * std::sin(a0)});
            Vec3 b1 = xf.transformPoint({r0 * std::cos(a1), y0, r0 * std::sin(a1)});
            Vec3 t0 = xf.transformPoint({r1 * std::cos(a0), y1, r1 * std::sin(a0)});
            Vec3 t1 = xf.transformPoint({r1 * std::cos(a1), y1, r1 * std::sin(a1)});
            triangle(b0, b1, t1, mat, inside);
            if (r1 > 0) triangle(b0, t1, t0, mat, inside);
            triangle(bottomCenter, b0, b1, mat, inside);
            if (r1 > 0) triangle(topCenter, t0, t1, mat, inside);
        }
    }

    // Placa: un poligono convexo (puntos x, z) extruido entre las alturas y0
    // e y1. Sirve para alas y aletas. xf permite girarla o moverla.
    void slab(const std::vector<Vec2>& outline, float y0, float y1, int mat, const Mat4& xf = Mat4::identity()) {
        Vec3 center;
        for (const Vec2& p : outline) center += Vec3{p.x, (y0 + y1) * 0.5f, p.y};
        center = center * (1.0f / outline.size());
        Vec3 inside = xf.transformPoint(center);
        size_t n = outline.size();
        for (size_t i = 0; i < n; ++i) {
            const Vec2& p = outline[i];
            const Vec2& q = outline[(i + 1) % n];
            Vec3 p0 = xf.transformPoint({p.x, y0, p.y}), p1 = xf.transformPoint({p.x, y1, p.y});
            Vec3 q0 = xf.transformPoint({q.x, y0, q.y}), q1 = xf.transformPoint({q.x, y1, q.y});
            triangle(p0, q0, q1, mat, inside); // borde
            triangle(p0, q1, p1, mat, inside);
            if (i > 0 && i + 1 < n) { // tapas en abanico desde el primer punto
                const Vec2& f = outline[0];
                triangle(xf.transformPoint({f.x, y0, f.y}), p0, q0, mat, inside);
                triangle(xf.transformPoint({f.x, y1, f.y}), p1, q1, mat, inside);
            }
        }
    }

    // Esfera irregular: icosfera estirada a "size" y deformada con ruido.
    void blob(int subdivisions, Vec3 center, Vec3 size, float roughness, unsigned seed, int mat) {
        Mesh ico = makeIcosphere(subdivisions, Material{});
        Noise noise(seed);
        std::vector<Vec3> p;
        for (const Vertex& v : ico.vertices) {
            float bump = 1.0f + roughness * noise.perlin(v.position * 1.8f);
            p.push_back(center + v.position * size * bump);
        }
        for (const Triangle& t : ico.triangles) triangle(p[t.a], p[t.b], p[t.c], mat, center);
    }

    // Bipiramide: un anillo de "sides" puntos con una punta arriba y otra abajo.
    void bipyramid(int sides, float r, float yMid, float yTop, float yBottom, int mat, const Mat4& xf) {
        Vec3 inside = xf.transformPoint({0, yMid, 0});
        Vec3 top = xf.transformPoint({0, yTop, 0}), bottom = xf.transformPoint({0, yBottom, 0});
        for (int i = 0; i < sides; ++i) {
            float a0 = 2.0f * PI * i / sides, a1 = 2.0f * PI * (i + 1) / sides;
            Vec3 m0 = xf.transformPoint({r * std::cos(a0), yMid, r * std::sin(a0)});
            Vec3 m1 = xf.transformPoint({r * std::cos(a1), yMid, r * std::sin(a1)});
            triangle(m0, m1, top, mat, inside);
            triangle(m0, m1, bottom, mat, inside);
        }
    }
};
