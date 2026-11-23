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

void hfFractal(Heightfield& f, uint32_t seed, float rough, bool ridged) {
    Rng r(seed * 2654435761u + 1);
    V3 off(r.f() * 200, r.f() * 200, r.f() * 200);
    int N = f.res;
    for (int z = 0; z < N; z++)
        for (int x = 0; x < N; x++) {
            float u = x / (float)(N - 1), v = z / (float)(N - 1);
            V3 p(u * 3 + off.x, off.y, v * 3 + off.z);
            p += V3(fbm(p * 0.7f + V3(5.2f, 0, 1.3f), 3), 0, fbm(p * 0.7f + V3(1.7f, 0, 9.2f), 3)) * 0.6f;
            float sum = 0, amp = 1, norm = 0, freq = 1;
            for (int o = 0; o < 9; o++, amp *= rough, freq *= 2.03f) {
                float nv = noise3(p * freq);
                if (ridged) { nv = 1 - std::fabs(nv); nv *= nv; }
                else nv = nv * 0.5f + 0.5f;
                sum += nv * amp;
                norm += amp;
            }
            float e = std::min(std::min(u, 1 - u), std::min(v, 1 - v));
            f.h[z * N + x] = sum / norm * smoothstep(0, 0.22f, e);
        }
    hfNormalize(f);
    for (float& v : f.h) v = std::pow(v, 1.6f);
    f.computeNormals();
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
    const float inertia = 0.05f, capK = 2.f, minSlope = 0.002f, depK = 0.3f, eroK = 0.05f, evap = 0.03f, gravity = 4.f;
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
        for (int life = 0; life < 30; life++) {
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
                addAt(x, y, -er);
                sed += er;
            }
            speed = std::sqrt(std::max(0.f, speed * speed - dh * gravity));
            water *= 1 - evap;
            x = nx; y = ny;
        }
    }
    for (float& v : h) v = clampf(v, 0, 1);
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

void hfBrush(Heightfield& f, float u, float v, float radius, float amount) {
    int N = f.res;
    float cx = u * (N - 1), cz = v * (N - 1), rp = std::max(1.f, radius * (N - 1));
    int x0 = std::max(0, (int)(cx - rp)), x1 = std::min(N - 1, (int)(cx + rp) + 1);
    int z0 = std::max(0, (int)(cz - rp)), z1 = std::min(N - 1, (int)(cz + rp) + 1);
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            float d2 = ((x - cx) * (x - cx) + (z - cz) * (z - cz)) / (rp * rp);
            if (d2 >= 1) continue;
            float w = (1 - d2) * (1 - d2);
            float& hv = f.h[z * N + x];
            hv = clampf(hv + amount * w, 0, 1);
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
