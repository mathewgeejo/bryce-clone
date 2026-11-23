#include "vmath.h"

#include <utility>

static int P[512];
static struct PermInit {
    PermInit() {
        int p[256];
        for (int i = 0; i < 256; i++) p[i] = i;
        Rng r(1337);
        for (int i = 255; i > 0; i--) std::swap(p[i], p[r.u() % (uint32_t)(i + 1)]);
        for (int i = 0; i < 512; i++) P[i] = p[i & 255];
    }
} permInit;

static inline float fade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
static inline float grad(int h, float x, float y, float z) {
    h &= 15;
    float u = h < 8 ? x : y, v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

float noise3(V3 p) {
    float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
    int X = (int)fx & 255, Y = (int)fy & 255, Z = (int)fz & 255;
    float x = p.x - fx, y = p.y - fy, z = p.z - fz, u = fade(x), v = fade(y), w = fade(z);
    int A = P[X] + Y, AA = P[A] + Z, AB = P[A + 1] + Z, B = P[X + 1] + Y, BA = P[B] + Z, BB = P[B + 1] + Z;
    return lerpf(lerpf(lerpf(grad(P[AA], x, y, z), grad(P[BA], x - 1, y, z), u),
                       lerpf(grad(P[AB], x, y - 1, z), grad(P[BB], x - 1, y - 1, z), u), v),
                 lerpf(lerpf(grad(P[AA + 1], x, y, z - 1), grad(P[BA + 1], x - 1, y, z - 1), u),
                       lerpf(grad(P[AB + 1], x, y - 1, z - 1), grad(P[BB + 1], x - 1, y - 1, z - 1), u), v),
                 w);
}

float fbm(V3 p, int octaves) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < octaves; i++, p = p * 2.02f, a *= 0.5f) { s += a * noise3(p); n += a; }
    return s / n;
}

float turb(V3 p, int octaves) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < octaves; i++, p = p * 2.02f, a *= 0.5f) { s += a * std::fabs(noise3(p)); n += a; }
    return s / n;
}

float hash3(int x, int y, int z) {
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return (h & 0xffffff) / 16777216.f;
}
