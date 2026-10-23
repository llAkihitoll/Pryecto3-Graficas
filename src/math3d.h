#pragma once
#include <cmath>

// Vectores y matrices para el pipeline 3D. Convencion (igual que OpenGL):
// sistema de mano derecha, +Y arriba, la camara mira hacia -Z y los
// vectores son columnas: p' = M * p.

const float PI = 3.14159265f;

inline float radians(float deg) { return deg * PI / 180.0f; }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3 {
    float x = 0, y = 0, z = 0;

    Vec3() = default;
    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline Vec3 operator*(float s, const Vec3& v) { return v * s; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    return len > 1e-8f ? v * (1.0f / len) : Vec3{0, 0, 0};
}
inline Vec3 lerp(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }

struct Vec4 {
    float x = 0, y = 0, z = 0, w = 0;

    Vec4() = default;
    Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    Vec4(const Vec3& v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}

    Vec3 xyz() const { return {x, y, z}; }
};

// Matriz 4x4 guardada por filas: m[fila][columna].
struct Mat4 {
    float m[4][4] = {};

    static Mat4 identity() {
        Mat4 r;
        for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k) r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    Vec4 operator*(const Vec4& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
                m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
                m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w};
    }

    // Transforma un punto (w = 1) o una direccion (w = 0) sin la parte proyectiva.
    Vec3 transformPoint(const Vec3& p) const { return ((*this) * Vec4(p, 1.0f)).xyz(); }
    Vec3 transformDir(const Vec3& d) const { return ((*this) * Vec4(d, 0.0f)).xyz(); }
};

inline Mat4 translate(const Vec3& t) {
    Mat4 r = Mat4::identity();
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
}

inline Mat4 scale(float s) {
    Mat4 r = Mat4::identity();
    r.m[0][0] = r.m[1][1] = r.m[2][2] = s;
    return r;
}

inline Mat4 rotateX(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[1][1] = c; r.m[1][2] = -s;
    r.m[2][1] = s; r.m[2][2] = c;
    return r;
}

inline Mat4 rotateY(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

inline Mat4 rotateZ(float a) {
    Mat4 r = Mat4::identity();
    float c = std::cos(a), s = std::sin(a);
    r.m[0][0] = c; r.m[0][1] = -s;
    r.m[1][0] = s; r.m[1][1] = c;
    return r;
}

// Matriz cuyas columnas son los ejes locales del objeto (derecha, arriba,
// atras) y su posicion. Sirve para orientar la nave a partir de su base.
inline Mat4 fromBasis(const Vec3& right, const Vec3& up, const Vec3& back, const Vec3& pos) {
    Mat4 r = Mat4::identity();
    r.m[0][0] = right.x; r.m[0][1] = up.x; r.m[0][2] = back.x; r.m[0][3] = pos.x;
    r.m[1][0] = right.y; r.m[1][1] = up.y; r.m[1][2] = back.y; r.m[1][3] = pos.y;
    r.m[2][0] = right.z; r.m[2][1] = up.z; r.m[2][2] = back.z; r.m[2][3] = pos.z;
    return r;
}

// Matriz de vista: lleva el mundo al espacio de la camara, donde la camara
// esta en el origen mirando hacia -Z. Es la inversa de colocar la camara.
inline Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& worldUp) {
    Vec3 f = normalize(target - eye);    // hacia adelante
    Vec3 r = normalize(cross(f, worldUp)); // derecha
    Vec3 u = cross(r, f);                // arriba real

    Mat4 v = Mat4::identity();
    v.m[0][0] = r.x;  v.m[0][1] = r.y;  v.m[0][2] = r.z;  v.m[0][3] = -dot(r, eye);
    v.m[1][0] = u.x;  v.m[1][1] = u.y;  v.m[1][2] = u.z;  v.m[1][3] = -dot(u, eye);
    v.m[2][0] = -f.x; v.m[2][1] = -f.y; v.m[2][2] = -f.z; v.m[2][3] = dot(f, eye);
    return v;
}

// Proyeccion en perspectiva. Despues de multiplicar, w = distancia del punto
// a la camara (-z en espacio de vista), y dividir entre w acerca al centro
// lo que esta lejos. Los puntos visibles quedan en -w <= x,y,z <= w.
inline Mat4 perspective(float fovyRad, float aspect, float zNear, float zFar) {
    float f = 1.0f / std::tan(fovyRad / 2.0f);
    Mat4 p;
    p.m[0][0] = f / aspect;
    p.m[1][1] = f;
    p.m[2][2] = (zFar + zNear) / (zNear - zFar);
    p.m[2][3] = 2.0f * zFar * zNear / (zNear - zFar);
    p.m[3][2] = -1.0f;
    return p;
}
