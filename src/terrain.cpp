#include "terrain.h"

void hfInit(Heightfield& f, int res) {
    f.res = res;
    f.h.assign((size_t)res * res, 0.f);
    f.computeNormals();
}

void hfNormalize(Heightfield& f) {
    auto mm = std::minmax_element(f.h.begin(), f.h.end());
    float lo = *mm.first, range = *mm.second - lo;
    if (range > 1e-6f)
        for (float& v : f.h) v = (v - lo) / range;
    f.computeNormals();
}

const char* brushName(BrushMode m) {
    static const char* n[] = {"Raise", "Lower", "Smooth", "Flatten", "Peak", "Plateau", "Roughen", "Erode"};
    return n[(int)m];
}

const char* terrainKindName(TerrainKind k) {
    static const char* n[] = {"Mountains", "Alpine", "Rolling Hills", "Canyon", "Dunes", "Volcano", "Mesas", "Archipelago", "Crags"};
    return n[(int)k];
}

namespace {

float fbmR(V3 p, int oct, float rough) {
    float s = 0, a = 1, n = 0;
    for (int o = 0; o < oct; o++, a *= rough, p = p * 2.03f + V3(17.3f, 0, 9.1f)) { s += noise3(p) * a; n += a; }
    return s / n;
}

// Musgrave-style ridged multifractal: sharp crests, smooth valleys, detail concentrated on ridges.
float ridgedMF(V3 p, int oct, float rough) {
    float s = 0, w = 1, a = 1, n = 0;
    for (int o = 0; o < oct; o++, a *= rough, p = p * 2.03f + V3(5.7f, 0, 3.9f)) {
        float r = 1 - std::fabs(noise3(p) * 1.4f);
        r = clampf(r, 0, 1);
        r *= r * w;
        w = clampf(r * 2, 0, 1);
        s += r * a;
        n += a;
    }
    return s / n;
}

V3 warp(V3 p, float k) { return p + V3(fbmR(p * 0.6f + V3(5.2f, 0, 1.3f), 4, 0.5f), 0, fbmR(p * 0.6f + V3(1.7f, 0, 9.2f), 4, 0.5f)) * k; }

// Irregular island outline: falls to exactly zero at the square border but never follows it.
float organicMask(float u, float v, V3 off, float reach) {
    float x = u * 2 - 1, z = v * 2 - 1;
    float d = lerpf(std::sqrt(x * x + z * z), std::max(std::fabs(x), std::fabs(z)), 0.45f);
    d += fbmR(V3(u * 3.1f + off.x, 0.37f, v * 3.1f + off.z), 4, 0.55f) * 0.42f;
    float e = std::min(std::min(u, 1 - u), std::min(v, 1 - v));
    return smoothstep(reach, reach * 0.42f, d) * smoothstep(0.f, 0.06f, e);
}

}  // namespace

void hfGenerate(Heightfield& f, TerrainKind k, uint32_t seed, float rough) {
    Rng r(seed * 2654435761u + 1);
    V3 off(r.f() * 200, r.f() * 7, r.f() * 200), off2(r.f() * 200, 3.3f, r.f() * 200);
    float ang = r.f() * 2 * PI;
    int N = f.res;
    bool masked = true;
    float gamma = 1, scale = 1, reach = 0.95f;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float u = x / (float)(N - 1), v = z / (float)(N - 1), h = 0;
            V3 p(u * 3 + off.x, off.y, v * 3 + off.z);
            switch (k) {
            case TerrainKind::Mountains: {
                V3 q = warp(p, 0.55f);
                h = fbmR(q, 9, rough) * 0.5f + 0.5f + ridgedMF(q * 0.8f + off2, 6, rough) * 0.35f;
                gamma = 1.5f;
                break;
            }
            case TerrainKind::Alpine: {
                V3 q = warp(p * 1.1f, 0.4f);
                h = ridgedMF(q, 9, rough + 0.05f) + fbmR(q * 0.5f, 3, 0.5f) * 0.25f;
                gamma = 1.25f;
                break;
            }
            case TerrainKind::Hills: {
                V3 q = warp(p * 0.8f, 0.7f);
                h = fbmR(q, 5, rough * 0.8f) * 0.5f + 0.5f;
                gamma = 1.1f; scale = 0.45f; reach = 1.02f;
                break;
            }
            case TerrainKind::Canyon: {
                V3 q = warp(p * 0.9f, 0.8f);
                float river = std::fabs(fbmR(q * 0.7f + off2, 5, 0.5f));
                float wall = smoothstep(0.03f, 0.16f + 0.05f * noise3(q * 3.f), river);
                float side = std::fabs(fbmR(q * 1.6f + off2 * 0.5f, 4, 0.5f));
                wall *= smoothstep(0.015f, 0.09f, side) * 0.35f + 0.65f;
                float steps = wall * 6, b = std::floor(steps);
                wall = (b + smoothstep(0.55f, 0.95f, steps - b)) / 6;
                h = 0.06f + wall * 0.82f + fbmR(q * 6.f, 4, 0.5f) * 0.03f;
                reach = 1.06f;
                break;
            }
            case TerrainKind::Dunes: {
                V3 q = warp(p * 0.8f, 1.2f);
                float d = (q.x * std::cos(ang) + q.z * std::sin(ang)) * 2.6f + fbmR(q * 0.7f, 3, 0.5f) * 1.5f;
                float s = d - std::floor(d);
                float crest = s < 0.72f ? smoothstep(0, 0.72f, s) : smoothstep(1, 0.72f, s);
                float amp = 0.45f + 0.55f * (fbmR(q * 0.5f + off2, 3, 0.5f) * 0.5f + 0.5f);
                h = crest * amp * 0.7f + (fbmR(q * 0.35f, 3, 0.5f) * 0.5f + 0.5f) * 0.45f + fbmR(q * 12.f, 2, 0.5f) * 0.01f;
                gamma = 1.2f; scale = 0.5f; reach = 1.04f;
                break;
            }
            case TerrainKind::Volcano: {
                float cx = u - 0.5f, cz = v - 0.5f;
                float rr = std::sqrt(cx * cx + cz * cz) * 2.f;
                float a = std::atan2(cz, cx);
                rr += fbmR(V3(std::cos(a) * 1.5f + off.x, 0.5f, std::sin(a) * 1.5f + off.z), 4, 0.5f) * 0.12f;
                float cone = std::pow(clampf(1 - rr, 0, 1), 1.7f);
                float gully = ridgedMF(V3(a * 3.f + off.x, rr * 4.f, off2.z), 5, 0.5f);
                h = cone * (0.85f + 0.15f * gully) - smoothstep(0.2f, 0.08f, rr) * 0.42f + fbmR(p * 3.f, 5, rough) * 0.04f * cone;
                h += (fbmR(p, 5, rough) * 0.5f + 0.5f) * 0.12f;
                gamma = 1; masked = false;
                h *= organicMask(u, v, off, 1.f);
                break;
            }
            case TerrainKind::Mesas: {
                V3 q = warp(p * 1.1f, 0.6f);
                float n = fbmR(q, 6, 0.5f) * 0.5f + 0.5f;
                float top = smoothstep(0.54f, 0.565f, n) * 0.75f + smoothstep(0.64f, 0.66f, n) * 0.22f;
                float talus = smoothstep(0.42f, 0.56f, n) * 0.18f;
                h = std::max(top, talus) + 0.04f + fbmR(q * 8.f, 4, 0.5f) * 0.02f;
                reach = 1.f;
                break;
            }
            case TerrainKind::Archipelago: {
                V3 q = warp(p * 1.3f, 0.6f);
                float n = fbmR(q, 8, rough) * 0.5f + 0.5f + ridgedMF(q * 1.2f + off2, 5, 0.5f) * 0.2f;
                h = std::max(0.f, n - 0.55f);
                gamma = 1.3f;
                break;
            }
            case TerrainKind::Crags: {
                V3 q = warp(p * 1.4f, 0.3f);
                h = std::pow(ridgedMF(q, 8, rough + 0.1f), 0.8f) + std::fabs(fbmR(q * 4.f, 3, 0.5f)) * 0.15f;
                gamma = 1.8f;
                break;
            }
            default: break;
            }
            f.h[z * N + x] = h;
        }
    hfNormalize(f);
    if (masked)
        for (int z = 0; z < N; z++)
            for (int x = 0; x < N; x++) {
                float& h = f.h[z * N + x];
                h = std::pow(h, gamma) * organicMask(x / (float)(N - 1), z / (float)(N - 1), off, reach);
            }
    hfNormalize(f);
    for (float& h : f.h) h = std::max(0.f, h - 0.012f) / 0.988f * scale;
    f.computeNormals();
}

void hfFractal(Heightfield& f, uint32_t seed, float rough, bool ridged) {
    hfGenerate(f, ridged ? TerrainKind::Alpine : TerrainKind::Mountains, seed, rough);
}

void hfSmooth(Heightfield& f) {
    int N = f.res;
    std::vector<float> o = f.h;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float s = 0;
            int c = 0;
            for (int dz = -1; dz <= 1; dz++)
                for (int dx = -1; dx <= 1; dx++) {
                    int xx = x + dx, zz = z + dz;
                    if (xx < 0 || zz < 0 || xx >= N || zz >= N) continue;
                    s += o[zz * N + xx];
                    c++;
                }
            f.h[z * N + x] = s / c;
        }
    f.computeNormals();
}

void hfErode(Heightfield& f, int drops, uint32_t seed) {
    int N = f.res;
    auto& h = f.h;
    Rng r(seed * 747796405u + 7);
    const float inertia = 0.1f, capK = 4.f, minSlope = 0.004f, depK = 0.3f, eroK = 0.3f, evap = 0.02f, gravity = 4.f;
    const int R = std::max(2, N / 96);
    struct Tap { int dx, dz; float w; };
    std::vector<Tap> taps;
    float wsum = 0;
    for (int dz = -R; dz <= R; dz++)
        for (int dx = -R; dx <= R; dx++) {
            float d = std::sqrt((float)(dx * dx + dz * dz));
            if (d < R) { taps.push_back({dx, dz, R - d}); wsum += R - d; }
        }
    for (auto& t : taps) t.w /= wsum;
    auto erodeAt = [&](float x, float y, float amt) {
        int ix = (int)x, iy = (int)y;
        for (auto& t : taps) {
            int xx = ix + t.dx, zz = iy + t.dz;
            if (xx < 0 || zz < 0 || xx >= N || zz >= N) continue;
            float& hv = h[zz * N + xx];
            hv = std::max(0.f, hv - amt * t.w);
        }
    };
    auto sample = [&](float x, float y, float& gx, float& gy) {
        int ix = (int)x, iy = (int)y;
        float fx = x - ix, fy = y - iy;
        float a = h[iy * N + ix], b = h[iy * N + ix + 1], c = h[(iy + 1) * N + ix], d = h[(iy + 1) * N + ix + 1];
        gx = (b - a) * (1 - fy) + (d - c) * fy;
        gy = (c - a) * (1 - fx) + (d - b) * fx;
        return a * (1 - fx) * (1 - fy) + b * fx * (1 - fy) + c * (1 - fx) * fy + d * fx * fy;
    };
    auto addAt = [&](float x, float y, float amt) {
        int ix = (int)x, iy = (int)y;
        float fx = x - ix, fy = y - iy;
        h[iy * N + ix] += amt * (1 - fx) * (1 - fy);
        h[iy * N + ix + 1] += amt * fx * (1 - fy);
        h[(iy + 1) * N + ix] += amt * (1 - fx) * fy;
        h[(iy + 1) * N + ix + 1] += amt * fx * fy;
    };
    for (int i = 0; i < drops; i++) {
        float x = r.f() * (N - 2), y = r.f() * (N - 2), dx = 0, dy = 0, speed = 1, water = 1, sed = 0;
        if (h[(int)y * N + (int)x] <= 1e-4f) continue;
        for (int life = 0; life < 64; life++) {
            float gx, gy, h0 = sample(x, y, gx, gy);
            dx = dx * inertia - gx * (1 - inertia);
            dy = dy * inertia - gy * (1 - inertia);
            float l = std::sqrt(dx * dx + dy * dy);
            if (l < 1e-7f) break;
            dx /= l; dy /= l;
            float nx = x + dx, ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= N - 1 || ny >= N - 1) break;
            float g2x, g2y, dh = sample(nx, ny, g2x, g2y) - h0;
            float cap = std::max(-dh, minSlope) * speed * water * capK;
            if (sed > cap || dh > 0) {
                float dep = dh > 0 ? std::min(dh, sed) : (sed - cap) * depK;
                sed -= dep;
                addAt(x, y, dep);
            } else {
                float er = std::min((cap - sed) * eroK, -dh);
                erodeAt(x, y, er);
                sed += er;
            }
            speed = std::sqrt(std::max(0.f, speed * speed - dh * gravity));
            water *= 1 - evap;
            x = nx; y = ny;
        }
    }
    for (float& v : h) v = v < 0.004f ? 0.f : clampf(v, 0, 1);
    f.computeNormals();
}

void hfAdd(Heightfield& f, float v) {
    for (float& x : f.h) x = clampf(x + v, 0, 1);
    f.computeNormals();
}

void hfInvert(Heightfield& f) {
    for (float& x : f.h) x = 1 - x;
    f.computeNormals();
}

void hfPlateau(Heightfield& f, float level) {
    for (float& x : f.h)
        if (x > level) x = level + (x - level) * 0.12f;
    f.computeNormals();
}

void hfBrush(Heightfield& f, float u, float v, float radius, float amount, BrushMode mode) {
    int N = f.res;
    float cx = u * (N - 1), cz = v * (N - 1), rp = std::max(1.f, radius * (N - 1));
    int x0 = std::max(0, (int)(cx - rp)), x1 = std::min(N - 1, (int)(cx + rp) + 1);
    int z0 = std::max(0, (int)(cz - rp)), z1 = std::min(N - 1, (int)(cz + rp) + 1);
    std::vector<float> o = f.h;
    float centre = o[std::min(N - 1, std::max(0, (int)cz)) * N + std::min(N - 1, std::max(0, (int)cx))];
    float top = 0;
    if (mode == BrushMode::Plateau)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                if ((x - cx) * (x - cx) + (z - cz) * (z - cz) < rp * rp * 0.25f) top = std::max(top, o[z * N + x]);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            float d2 = ((x - cx) * (x - cx) + (z - cz) * (z - cz)) / (rp * rp);
            if (d2 >= 1) continue;
            float w = (1 - d2) * (1 - d2), &hv = f.h[z * N + x];
            if (mode == BrushMode::Raise) hv += amount * w;
            else if (mode == BrushMode::Lower) hv -= amount * w;
            else if (mode == BrushMode::Flatten) hv = lerpf(hv, centre, clampf(amount * 8 * w, 0, 1));
            else if (mode == BrushMode::Peak) hv += amount * 2.2f * std::pow(1 - std::sqrt(d2), 2.4f);
            else if (mode == BrushMode::Plateau) {
                float target = std::min(1.f, top + amount * 0.6f);
                float shape = smoothstep(1.f, 0.55f, std::sqrt(d2));
                hv = std::max(hv, lerpf(hv, target, clampf(shape * (0.25f + amount * 6), 0, 1)));
            } else if (mode == BrushMode::Roughen) {
                float n = noise3(V3(x * 0.35f, 0.5f, z * 0.35f)) + 0.5f * noise3(V3(x * 0.9f, 1.5f, z * 0.9f));
                hv += amount * 1.2f * n * w;
            } else if (mode == BrushMode::Erode) {
                float lo = hv;
                int lx = x, lz = z;
                for (int dz = -1; dz <= 1; dz++)
                    for (int dx = -1; dx <= 1; dx++) {
                        int xx = x + dx, zz = z + dz;
                        if (xx >= 0 && zz >= 0 && xx < N && zz < N && o[zz * N + xx] < lo) { lo = o[zz * N + xx]; lx = xx; lz = zz; }
                    }
                float drop = o[z * N + x] - lo, talus = 1.5f / N;
                if (drop > talus) {
                    float m = (drop - talus) * clampf(amount * 10 * w, 0, 0.5f);
                    hv -= m;
                    f.h[lz * N + lx] += m * 0.6f;
                }
                float n = std::fabs(noise3(V3(x * 0.5f, 2.5f, z * 0.5f)));
                hv -= amount * 0.15f * w * smoothstep(0.f, 0.2f, n) * clampf(drop * N, 0, 1);
            } else {
                float s = 0;
                int c = 0;
                for (int dz = -2; dz <= 2; dz++)
                    for (int dx = -2; dx <= 2; dx++) {
                        int xx = x + dx, zz = z + dz;
                        if (xx >= 0 && zz >= 0 && xx < N && zz < N) { s += o[zz * N + xx]; c++; }
                    }
                hv = lerpf(hv, s / c, clampf(amount * 12 * w, 0, 1));
            }
            hv = clampf(hv, 0, 1);
        }
    f.computeNormals();
}

void hfTerrace(Heightfield& f, int levels) {
    for (float& x : f.h) {
        float s = x * levels, b = std::floor(s);
        x = clampf((b + smoothstep(0.65f, 1.f, s - b)) / levels, 0, 1);
    }
    f.computeNormals();
}

void hfSpikes(Heightfield& f, uint32_t seed, int count) {
    Rng r(seed * 2891336453u + 11);
    int N = f.res;
    for (int i = 0; i < count; i++) {
        float cx = (0.15f + 0.7f * r.f()) * (N - 1), cz = (0.15f + 0.7f * r.f()) * (N - 1), rad = (0.02f + 0.05f * r.f()) * N, ht = 0.3f + 0.6f * r.f();
        for (int z = std::max(0, (int)(cz - rad)); z <= std::min(N - 1, (int)(cz + rad) + 1); z++)
            for (int x = std::max(0, (int)(cx - rad)); x <= std::min(N - 1, (int)(cx + rad) + 1); x++) {
                float d = std::sqrt((x - cx) * (x - cx) + (z - cz) * (z - cz)) / rad;
                if (d < 1) f.h[z * N + x] = std::max(f.h[z * N + x], ht * std::pow(1 - d, 1.6f));
            }
    }
    f.computeNormals();
}

void hfCrater(Heightfield& f, float u, float v, float radius, float depth) {
    int N = f.res;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float dx = x / (float)(N - 1) - u, dz = z / (float)(N - 1) - v, d = std::sqrt(dx * dx + dz * dz) / radius;
            float bowl = d < 1 ? -(1 - d * d) * depth : 0, rim = depth * 0.6f * std::exp(-(d - 1) * (d - 1) * 18);
            f.h[z * N + x] = clampf(f.h[z * N + x] + bowl + rim, 0, 1);
        }
    f.computeNormals();
}

void hfRoundEdges(Heightfield& f) {
    int N = f.res;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float u = x / (float)(N - 1) * 2 - 1, v = z / (float)(N - 1) * 2 - 1;
            f.h[z * N + x] *= smoothstep(1.f, 0.55f, std::sqrt(u * u + v * v));
        }
    f.computeNormals();
}

void hfMound(Heightfield& f) {
    int N = f.res;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float u = x / (float)(N - 1) - 0.5f, v = z / (float)(N - 1) - 0.5f;
            f.h[z * N + x] = clampf(f.h[z * N + x] + 0.35f * std::exp(-(u * u + v * v) * 14), 0, 1);
        }
    f.computeNormals();
}

void hfNoise(Heightfield& f, uint32_t seed, float amount) {
    int N = f.res;
    V3 off((seed % 997) * 1.37f, 0, (seed % 389) * 2.11f);
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            V3 p(x * 24.f / N + off.x, 0.5f, z * 24.f / N + off.z);
            f.h[z * N + x] = clampf(f.h[z * N + x] + fbm(p, 3) * amount, 0, 1);
        }
    f.computeNormals();
}

void hfSharpen(Heightfield& f) {
    std::vector<float> o = f.h;
    hfSmooth(f);
    for (size_t i = 0; i < o.size(); i++) f.h[i] = clampf(o[i] + (o[i] - f.h[i]) * 1.5f, 0, 1);
    f.computeNormals();
}

void hfFromImage(Heightfield& f, const Image& img) {
    int N = f.res;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            V3 c = img.sample((x + 0.5f) / N, (z + 0.5f) / N);
            f.h[z * N + x] = clampf(c.x * 0.3f + c.y * 0.59f + c.z * 0.11f, 0, 1);
        }
    f.computeNormals();
}

void hfResample(Heightfield& f, int res) {
    Heightfield o = f;
    f.res = res;
    f.h.assign((size_t)res * res, 0.f);
    for (int z = 0; z < res; z++)
        for (int x = 0; x < res; x++) {
            float sx = x * (o.res - 1) / (float)(res - 1), sz = z * (o.res - 1) / (float)(res - 1);
            int ix = std::min((int)sx, o.res - 2), iz = std::min((int)sz, o.res - 2);
            float fx = sx - ix, fz = sz - iz;
            f.h[z * res + x] = lerpf(lerpf(o.at(ix, iz), o.at(ix + 1, iz), fx), lerpf(o.at(ix, iz + 1), o.at(ix + 1, iz + 1), fx), fz);
        }
    f.computeNormals();
}
