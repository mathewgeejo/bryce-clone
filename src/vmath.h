#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

constexpr float PI = 3.14159265358979f;
inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0.f, 1.f); return t * t * (3 - 2 * t); }
inline float rad(float d) { return d * PI / 180.f; }

struct V3 {
    float x = 0, y = 0, z = 0;
    V3() = default;
    constexpr V3(float a, float b, float c) : x(a), y(b), z(c) {}
    explicit constexpr V3(float a) : x(a), y(a), z(a) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(const V3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    V3 operator*(float s) const { return {x * s, y * s, z * s}; }
    V3 operator/(float s) const { return {x / s, y / s, z / s}; }
    V3 operator-() const { return {-x, -y, -z}; }
    V3& operator+=(const V3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    V3& operator-=(const V3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    V3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};
inline V3 operator*(float s, const V3& v) { return v * s; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float length(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 normalize(V3 a) { float l = length(a); return l > 0 ? a / l : a; }
inline V3 lerp(V3 a, V3 b, float t) { return a + (b - a) * t; }
inline V3 reflect(V3 d, V3 n) { return d - n * (2 * dot(d, n)); }
inline float maxc(V3 a) { return std::max(a.x, std::max(a.y, a.z)); }

struct Ray { V3 o, d; };

struct M3 {
    float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    V3 operator*(const V3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    M3 operator*(const M3& o) const {
        M3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] + m[i][2] * o.m[2][j];
        return r;
    }
    M3 transpose() const {
        M3 r;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) r.m[i][j] = m[j][i];
        return r;
    }
    M3 inverse() const {
        const auto& a = m;
        float c00 = a[1][1] * a[2][2] - a[1][2] * a[2][1], c01 = a[1][2] * a[2][0] - a[1][0] * a[2][2], c02 = a[1][0] * a[2][1] - a[1][1] * a[2][0];
        float det = a[0][0] * c00 + a[0][1] * c01 + a[0][2] * c02, id = std::fabs(det) > 1e-20f ? 1 / det : 0;
        M3 r;
        r.m[0][0] = c00 * id; r.m[0][1] = (a[0][2] * a[2][1] - a[0][1] * a[2][2]) * id; r.m[0][2] = (a[0][1] * a[1][2] - a[0][2] * a[1][1]) * id;
        r.m[1][0] = c01 * id; r.m[1][1] = (a[0][0] * a[2][2] - a[0][2] * a[2][0]) * id; r.m[1][2] = (a[0][2] * a[1][0] - a[0][0] * a[1][2]) * id;
        r.m[2][0] = c02 * id; r.m[2][1] = (a[0][1] * a[2][0] - a[0][0] * a[2][1]) * id; r.m[2][2] = (a[0][0] * a[1][1] - a[0][1] * a[1][0]) * id;
        return r;
    }
    V3 col(int j) const { return {m[0][j], m[1][j], m[2][j]}; }
};
inline M3 rotX(float a) { M3 r; float c = std::cos(a), s = std::sin(a); r.m[1][1] = c; r.m[1][2] = -s; r.m[2][1] = s; r.m[2][2] = c; return r; }
inline M3 rotY(float a) { M3 r; float c = std::cos(a), s = std::sin(a); r.m[0][0] = c; r.m[0][2] = s; r.m[2][0] = -s; r.m[2][2] = c; return r; }
inline M3 rotZ(float a) { M3 r; float c = std::cos(a), s = std::sin(a); r.m[0][0] = c; r.m[0][1] = -s; r.m[1][0] = s; r.m[1][1] = c; return r; }
inline M3 scaleM(V3 s) { M3 r; r.m[0][0] = s.x; r.m[1][1] = s.y; r.m[2][2] = s.z; return r; }

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed = 1) : s(seed ? seed : 1) {}
    uint32_t u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float f() { return (u() >> 8) * (1.f / 16777216.f); }
};

float noise3(V3 p);
float fbm(V3 p, int octaves);
float turb(V3 p, int octaves);
float hash3(int x, int y, int z);
