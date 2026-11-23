#define _CRT_SECURE_NO_WARNINGS
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "../../src/render.h"
#include "../../src/terrain.h"

static bool saveBMP(const std::string& p, int w, int h, const std::vector<uint32_t>& px) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    int rowB = (w * 3 + 3) & ~3;
    uint8_t hd[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { for (int k = 0; k < 4; k++) hd[o + k] = (uint8_t)(v >> (8 * k)); };
    put32(2, 54 + rowB * h); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h); put32(34, rowB * h);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f);
    std::vector<uint8_t> row(rowB, 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; x++) { uint32_t c = px[(size_t)y * w + x]; row[x * 3] = c & 255; row[x * 3 + 1] = (c >> 8) & 255; row[x * 3 + 2] = (c >> 16) & 255; }
        fwrite(row.data(), 1, rowB, f);
    }
    return fclose(f) == 0;
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "build/t/terr.bmp";
    std::vector<std::pair<const char*, std::function<void(Heightfield&)>>> ops = {
        {"default", [](Heightfield& f) { hfFractal(f, 20, 0.5f, false); hfErode(f, 60000, 1); }},
        {"fractal", [](Heightfield& f) { hfFractal(f, 21, 0.5f, false); }},
        {"ridged", [](Heightfield& f) { hfFractal(f, 22, 0.5f, true); }},
        {"rough.7", [](Heightfield& f) { hfFractal(f, 23, 0.7f, false); }},
        {"erode x3", [](Heightfield& f) { hfFractal(f, 24, 0.5f, false); for (int i = 0; i < 3; i++) hfErode(f, f.res * f.res, i); }},
        {"terrace", [](Heightfield& f) { hfFractal(f, 25, 0.5f, false); hfTerrace(f, 6); }},
        {"crater", [](Heightfield& f) { hfFractal(f, 26, 0.5f, false); hfCrater(f, 0.5f, 0.5f, 0.28f, 0.3f); }},
        {"spikes", [](Heightfield& f) { hfInit(f, f.res); hfSpikes(f, 3, 12); }},
        {"plateau", [](Heightfield& f) { hfFractal(f, 27, 0.5f, false); hfPlateau(f, 0.6f); }},
    };
    const int tw = 360, th = 240, cols = 3;
    int rows = (int)((ops.size() + cols - 1) / cols);
    std::vector<uint32_t> big((size_t)tw * cols * th * rows);
    for (size_t i = 0; i < ops.size(); i++) {
        Scene s = makeEmptyScene();
        s.objs[0].mat = findMaterial("Desert Sand");
        Object t = makeObject(ObjType::Terrain, 1);
        hfInit(*t.hf, 257);
        ops[i].second(*t.hf);
        t.scl = {30, 9, 30};
        s.objs.push_back(t);
        s.cam.target = {0, 3, 0}; s.cam.dist = 62; s.cam.pitch = 24; s.cam.yaw = 30; s.cam.fov = 50;
        s.updateAll();
        std::vector<uint32_t> px((size_t)tw * th);
        renderImage(s, tw, th, px.data(), 1);
        int ox = (int)(i % cols) * tw, oy = (int)(i / cols) * th;
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) big[(size_t)(oy + y) * tw * cols + ox + x] = px[(size_t)y * tw + x];
        printf("%s\n", ops[i].first);
    }
    saveBMP(out, tw * cols, th * rows, big);
}
