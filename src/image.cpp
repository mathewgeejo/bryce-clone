#define _CRT_SECURE_NO_WARNINGS
#include "image.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }

V3 Image::sample(float u, float v) const {
    if (w <= 0 || h <= 0) return V3(0.f);
    u -= std::floor(u);
    v -= std::floor(v);
    float x = u * w - 0.5f, y = v * h - 0.5f, fx0 = std::floor(x), fy0 = std::floor(y), fx = x - fx0, fy = y - fy0;
    int x0 = ((int)fx0 % w + w) % w, y0 = ((int)fy0 % h + h) % h, x1 = (x0 + 1) % w, y1 = (y0 + 1) % h;
    return lerp(lerp(px[y0 * w + x0], px[y0 * w + x1], fx), lerp(px[y1 * w + x0], px[y1 * w + x1], fx), fy);
}

std::shared_ptr<Image> loadBMP(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> b;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        b.resize((size_t)sz);
        if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    }
    fclose(f);
    if (b.size() < 54 || b[0] != 'B' || b[1] != 'M') return nullptr;
    uint32_t off = rd32(&b[10]), dib = rd32(&b[14]), comp = rd32(&b[30]), ncol = rd32(&b[46]);
    int w = (int32_t)rd32(&b[18]), h = (int32_t)rd32(&b[22]), bpp = (int)rd16(&b[28]);
    if (w <= 0 || h == 0 || w > 16384 || std::abs(h) > 16384) return nullptr;
    if (bpp != 8 && bpp != 24 && bpp != 32) return nullptr;
    if (!(comp == 0 || (comp == 3 && bpp == 32))) return nullptr;
    bool topDown = h < 0;
    h = std::abs(h);
    size_t stride = ((size_t)w * bpp + 31) / 32 * 4;
    if ((size_t)off + stride * h > b.size()) return nullptr;
    if (bpp == 8) {
        if (ncol == 0 || ncol > 256) ncol = 256;
        if ((size_t)14 + dib + ncol * 4 > b.size()) return nullptr;
    }
    const uint8_t* pal = b.data() + 14 + dib;
    auto img = std::make_shared<Image>();
    img->w = w;
    img->h = h;
    img->px.resize((size_t)w * h);
    for (int y = 0; y < h; y++) {
        const uint8_t* row = &b[off + stride * (topDown ? y : h - 1 - y)];
        for (int x = 0; x < w; x++) {
            const uint8_t* p;
            if (bpp == 8) {
                uint32_t i = row[x];
                p = pal + (i < ncol ? i : 0) * 4;
            } else p = row + x * (bpp / 8);
            img->px[(size_t)y * w + x] = V3(p[2], p[1], p[0]) / 255.f;
        }
    }
    return img;
}

std::shared_ptr<const Image> loadImageCached(const std::string& path) {
    static std::mutex mu;
    static std::map<std::string, std::shared_ptr<const Image>> cache;
    std::lock_guard<std::mutex> lock(mu);
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    auto img = loadBMP(path);
    cache[path] = img;
    return img;
}
