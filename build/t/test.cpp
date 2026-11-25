#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

static double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

static std::vector<uint32_t> render(const Scene& s, int w, int h, int q, const char* out) {
    std::vector<uint32_t> px((size_t)w * h);
    double t = now();
    renderImage(s, w, h, px.data(), q);
    printf("  %-28s %dx%d q%d %.2fs\n", out, w, h, q, now() - t);
    saveBMP(std::string("build/t/") + out, w, h, px);
    return px;
}

// Tiles many small renders into one sheet.
static void sheet(const std::vector<Scene>& scenes, int tw, int th, int cols, const char* out) {
    int rows = (int)((scenes.size() + cols - 1) / cols);
    std::vector<uint32_t> big((size_t)tw * cols * th * rows, 0);
    for (size_t i = 0; i < scenes.size(); i++) {
        std::vector<uint32_t> px((size_t)tw * th);
        renderImage(scenes[i], tw, th, px.data(), 1);
        int ox = (int)(i % cols) * tw, oy = (int)(i / cols) * th;
        for (int y = 0; y < th; y++)
            for (int x = 0; x < tw; x++) big[(size_t)(oy + y) * tw * cols + ox + x] = px[(size_t)y * tw + x];
    }
    saveBMP(std::string("build/t/") + out, tw * cols, th * rows, big);
    printf("  %s (%d tiles)\n", out, (int)scenes.size());
}

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
    printf("HDR lighting, terrain precision, and linked scatter\n");
    {
        const char* hdr = "build/t/parity fixture.hdr";
        const char* path = "build/t/parity fixture.c3d";
        const char* height = "build/t/parity height.tif";
        const char* pgm = "build/t/parity height.pgm";
        {
            std::ofstream file(hdr, std::ios::binary);
            file << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 8\n";
            const uint8_t scan[] = {2, 2, 0, 8, 136, 192, 136, 96, 136, 48, 136, 130};
            file.write(reinterpret_cast<const char*>(scan), sizeof scan);
        }
        {
            std::ofstream file(pgm);
            file << "P2\n# 16-bit terrain\n2 1\n65535\n12000 12001\n";
        }
        auto image = loadImage(hdr);
        CHECK(image && image->hdr && image->px[0].x == 3 && image->lightCdf.back() > 0);
        auto elevation = loadImage(pgm);
        CHECK(elevation && elevation->px[0].x < elevation->px[1].x);
        CHECK(saveHeightTiff(height, 2, 1, {12000 / 65535.f, 12001 / 65535.f}));
        auto tiff = loadImage(height);
        CHECK(tiff && std::fabs(tiff->px[1].x - 12001 / 65535.f) < 1e-6f && tiff->px[1].x > tiff->px[0].x);
        Heightfield hf;
        hfInit(hf, 9);
        Image mask; mask.w = mask.h = 1; mask.px = {V3(.5f)};
        if (elevation) hfBlendImage(hf, {2, 2, 6, 6}, *elevation, 1, &mask);
        CHECK(hf.at(0, 0) == 0 && std::fabs(hf.at(4, 4) - 6000.25f / 65535.f) < 1e-6f);
        Scene scene;
        scene.sky.environment = hdr;
        scene.sky.environmentImage = image;
        scene.sky.indirectStrength = .5f;
        auto terrain = makeObject(ObjType::Terrain, 1);
        terrain.pos = {}; terrain.scl = {10, 2, 10};
        hfInit(*terrain.hf, 9);
        hfAdd(*terrain.hf, .3f);
        terrain.mat.tex = TexType::None;
        terrain.mat.color1 = V3(.4f);
        Image baked = terrainMaterialHeightMap(terrain, 9);
        CHECK(baked.w == 9 && baked.h == 9 && std::fabs(baked.px.back().x - .4f) < .0001f);
        scene.objs = {makeObject(ObjType::Torus, 1), terrain};
        TextureLayer layer;
        layer.tex = TexType::Ridged;
        layer.mask = TexMask::Slope;
        layer.blend = TexBlend::Screen;
        layer.warp = .6f;
        layer.octaves = 7;
        scene.objs[0].mat.layers.push_back(layer);
        scene.updateAll();
        Rng rng(11);
        CHECK(scene.scatter({0}, 1, {{{3, 5, 0}, {0, -1, 0}}, {{6, 5, 0}, {0, -1, 0}}}, 1, 1, 0, true, rng) == 2);
        if (scene.objs.size() == 4) {
            CHECK(scene.objs[2].terrainAnchor && scene.objs[2].parent == 1);
            scene.objs[0].torus.major = .9f;
            scene.updateAll();
            CHECK(scene.objs[2].instanceSource == 0 && scene.objs[3].torus.major == .9f);
            V3 before = scene.objs[3].worldPos;
            scene.objs[1].pos.z += 2;
            hfAdd(*scene.objs[1].hf, .1f);
            scene.updateAll();
            CHECK(length(scene.objs[3].worldPos - before - V3(0, .2f, 2)) < .001f);
            Scene detached = scene.subset({3});
            CHECK(detached.objs[0].instanceSource == -1 && detached.objs[0].torus.major == .9f);
        }
        std::string error;
        CHECK(scene.embedAssets(error) && scene.save(path));
        Scene loaded;
        CHECK(loaded.load(path) && loaded.objs.size() == 4 && loaded.objs[2].instanceSource == 0 && loaded.objs[2].terrainAnchor &&
              loaded.sky.environmentImage && loaded.sky.environmentImage->hdr && loaded.sky.environmentImage->px[0].x == 3 &&
              loaded.sky.indirectStrength == .5f && loaded.objs[0].mat.layers[0].octaves == 7);
        Object light = makeObject(ObjType::Light, 1);
        light.lighting.targetGroup = 3;
        light.lighting.diffuseStrength = .2f;
        putKey(light.appearances, light.appearance(0));
        light.lighting.diffuseStrength = .8f;
        putKey(light.appearances, light.appearance(10));
        loaded.objs.push_back(light);
        CHECK(loaded.save(path));
        Scene animation;
        CHECK(animation.load(path) && animation.objs.size() == 5);
        if (animation.objs.size() == 5) {
            animation.applyFrame(5);
            CHECK(std::fabs(animation.objs.back().lighting.diffuseStrength - .5f) < .0001f &&
                  animation.objs.back().lighting.targetGroup == 3);
        }
        Heightfield large;
        hfInit(large, MAX_TERRAIN_RESOLUTION);
        CHECK(large.n.empty() && large.normal(2000, 2000).y == 1);
        for (const char* file : {hdr, path, height, pgm}) std::remove(file);
    }
    {
        const char* source = "build/t/parity-uv.obj";
        const char* exported = "build/t/parity-export.obj";
        {
            std::ofstream file(source);
            file << "v -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0.5 1\nf 1/1 2/2 3/3\n";
        }
        Mesh mesh, roundTrip;
        std::string error;
        CHECK(loadOBJ(source, mesh, error) && mesh.uv.size() == 3);
        V3 n, uv;
        float t = 0;
        CHECK(mesh.intersect({0, 0, 1}, {0, 0, -1}, .001f, 5, t, n, false, &uv) && length(uv - V3(.5f, .5f, 0)) < .001f);
        CHECK(saveGeometry(exported, {{"UV triangle", mesh}}, false) && loadOBJ(exported, roundTrip, error) && roundTrip.uv.size() == 3);
        std::remove(source);
        std::remove(exported);
    }
    printf("DXF import/export and implicit geometry\n");
    {
        const char* path = "build/t/brycescape-format.dxf";
        Mesh m;
        m.v = {{2, 3, 4}, {5, 3, 4}, {2, 3, 1}};
        m.tri = {0, 1, 2};
        std::vector<MeshLayer> layers{{"Survey", m}}, loaded;
        std::string error;
        CHECK(saveGeometry(path, layers, true));
        CHECK(loadDXF(path, loaded, 0.5f, true, true, error));
        CHECK(loaded.size() == 1 && loaded[0].name == "Survey" && loaded[0].mesh.triCount() == 1 &&
              length(loaded[0].mesh.v[0] - V3(1, 1.5f, 2)) < 0.001f);
        std::ofstream(path) << "0\nSECTION\n2\nENTITIES\n0\n3DFACE\n8\nBroken\n0\nEOF\n";
        CHECK(!loadDXF(path, loaded, 1, true, true, error) && loaded.size() == 1);
        std::remove(path);
        Scene s;
        s.objs = {makeObject(ObjType::Sphere, 1)};
        s.updateAll();
        auto baked = meshSelection(s, {0}, 12);
        CHECK(baked.size() == 1 && baked[0].mesh.triCount() > 100);
    }
    printf("hierarchies, nested CSG, cameras and linked heightfields\n");
    {
        Scene s;
        Object parent = makeObject(ObjType::Cube, 1), child = makeObject(ObjType::Sphere, 1);
        parent.pos = {}; parent.scl = V3(1);
        child.pos = {3, 2, 1};
        s.objs = {parent, child};
        s.updateAll();
        CHECK(s.setParent(1, 0) && !s.setParent(0, 1));
        putKey(s.objs[0].keys, Key{0, {}, {}, V3(1), Ease::Hold});
        putKey(s.objs[0].keys, Key{10, {4, 0, 0}, {}, V3(1), Ease::Linear});
        AppearanceKey a = s.objs[1].appearance(0), b = a;
        b.frame = 10; b.material.reflect = 1; b.hidden = true;
        s.objs[1].appearances = {a, b};
        s.applyFrame(5);
        CHECK(length(s.objs[1].worldPos - V3(3, 2, 1)) < 0.001f && !s.objs[1].hidden);
        CHECK(std::fabs(s.objs[1].mat.reflect - 0.5f) < 0.001f);
        s.applyFrame(10);
        CHECK(length(s.objs[1].worldPos - V3(7, 2, 1)) < 0.001f && s.objs[1].hidden);
        Scene single = s.subset({1});
        CHECK(single.objs.size() == 1 && single.objs[0].parent == -1 && length(single.objs[0].worldPos - V3(7, 2, 1)) < 0.001f);
        Object lattice;
        lattice.type = ObjType::Lattice;
        lattice.hf = std::make_shared<Heightfield>();
        hfInit(*lattice.hf, 9);
        s.objs.push_back(lattice); s.objs.push_back(lattice);
        s.cam.orthographic = true;
        s.cam.lookAt = 0;
        s.cameras = {{"Ortho", s.cam, {}}};
        s.activeCamera = 0;
        s.updateAll();
        const char* path = "build/t/brycescape-hierarchy.c3d";
        CHECK(s.save(path));
        s.title = "Saved again";
        CHECK(s.save(path));
        Scene copy;
        CHECK(copy.load(path) && copy.objs.size() == 4 && copy.objs[1].parent == 0 &&
              copy.objs[2].hf == copy.objs[3].hf && copy.activeCamera == 0 && cameraBasis(copy.viewCamera()).ortho && copy.title == "Saved again");
        std::remove(path);
        Camera top;
        top.pitch = 90; top.yaw = 0; top.dist = 2000; top.orthographic = true;
        ViewBasis matched = cameraBasis(top), editor = orthoBasis(1, top.target, top.orthoScale);
        CHECK(length(matched.f - editor.f) < 0.001f && length(matched.r - editor.r) < 0.001f &&
              length(matched.u - editor.u) < 0.001f && matched.k == editor.k);

        parent.scl = V3(2); parent.group = 1; parent.boolMode = BoolMode::Positive;
        child.pos = {0, 0, 1.5f}; child.scl = V3(1.25f);
        child.group = 2; child.boolMode = BoolMode::Positive;
        Scene csg;
        csg.objs = {parent, child};
        CHECK(csg.setBooleanParent(2, 1, BoolMode::Negative));
        CHECK(!csg.setBooleanParent(1, 2, BoolMode::Positive));
        csg.updateAll();
        Hit hit;
        CHECK(intersectScene(csg, {{0, 0, 5}, {0, 0, -1}}, 10, hit) && std::fabs(hit.t - 4.75f) < 0.002f && hit.n.z > 0.9f);
    }
    printf("image codecs and portable scene assets\n");
    {
        const std::string png = "build/t/brycescape-source.png", tiff = "build/t/brycescape-source.tiff";
        std::vector<uint32_t> rgba{0xffaa0000, 0x0000ff00};
        CHECK(saveRaster(png, 2, 1, rgba, true));
        auto picture = loadImage(png);
        CHECK(picture && picture->w == 2 && picture->alpha.size() == 2 && picture->opacity(0.75f, 0) == 0);
        CHECK(saveRaster(tiff, 2, 1, rgba, false));
        auto proof = loadImage(tiff);
        CHECK(proof && proof->w == 2 && proof->px[0].x > 0.6f);
        Scene s;
        Object card = makeObject(ObjType::Picture, 1);
        card.picture.path = png;
        s.objs.push_back(card);
        s.updateAll();
        const std::string packaged = "build/t/brycescape-packaged.c3d", embedded = "build/t/brycescape-embedded.c3d";
        std::string error;
        CHECK(s.package(packaged, error));
        CHECK(s.embedAssets(error) && s.save(embedded));
        std::remove(png.c_str());
        Scene copy, portable;
        CHECK(copy.load(embedded) && copy.objs.size() == 1 && copy.objs[0].picture.image &&
              copy.objs[0].picture.image->opacity(0.75f, 0) == 0);
        CHECK(portable.load(packaged) && portable.objs.size() == 1 && portable.objs[0].picture.image && portable.missingAssets().empty());
        if (portable.objs.size() == 1) std::remove(portable.objs[0].picture.path.c_str());
        std::remove(packaged.c_str()); std::remove(embedded.c_str()); std::remove(tiff.c_str());
        std::error_code ec;
        std::filesystem::remove("build/t/brycescape-packaged_assets", ec);
    }
    printf("scene BVH, render region and resume\n");
    {
        Scene s;
        for (int i = 0; i < 24; ++i) {
            Object o = makeObject(ObjType::Sphere, i + 1);
            o.pos = {(float)i * 4, 0, 0}; o.scl = V3(1);
            s.objs.push_back(o);
        }
        s.cam.target = {};
        s.cam.yaw = s.cam.pitch = 0;
        s.cam.dist = 5;
        s.render.region = true;
        s.render.cropX = s.render.cropY = 0.25f;
        s.updateAll();
        Hit hit;
        CHECK(!s.rayNodes.empty() && intersectScene(s, {{0, 0, 5}, {0, 0, -1}}, 20, hit) && hit.obj == 0);
        Renderer render;
        render.start(s, 64, 40, 0);
        render.cancel();
        if (render.resumable()) CHECK(render.resume());
        render.wait();
        std::vector<uint32_t> result, reference(64 * 40);
        render.copyTo(result);
        renderImage(s, 64, 40, reference.data(), 0);
        CHECK(render.finished() && result == reference && result[0] == 0 && result[20 * 64 + 32] != 0);
    }
    printf("metaball fields and Booleans\n");
    {
        Scene s;
        Object ball = makeObject(ObjType::Metaball, 1);
        ball.scl = V3(1);
        ball.pos = {-1.04f, 0, 0};
        s.objs.push_back(ball);
        ball.pos.x = 1.04f;
        s.objs.push_back(ball);
        Ray ray{{0, 0, 4}, {0, 0, -1}};
        Hit hit;
        s.updateAll();
        CHECK(!intersectScene(s, ray, 10, hit));
        s.objs[0].group = s.objs[1].group = 1;
        s.updateAll();
        CHECK(intersectScene(s, ray, 10, hit) && hit.n.z > 0.9f);
        float outside = hit.t;

        ball.pos = {};
        ball.group = 1;
        ball.metaRadius = 0.22f;
        ball.metaNegative = true;
        s.objs.push_back(ball);
        s.updateAll();
        CHECK(intersectScene(s, {{}, {0, 0, 1}}, 10, hit) && hit.n.z < -0.9f);

        Object cutter = makeObject(ObjType::Cube, 1);
        cutter.group = 1;
        cutter.pos = {0, 0, 0.55f};
        cutter.scl = V3(0.35f);
        cutter.boolMode = BoolMode::Negative;
        s.objs.push_back(cutter);
        s.updateAll();
        CHECK(intersectScene(s, ray, 10, hit) && hit.t > outside && hit.n.z > 0.9f);

        cutter.boolMode = BoolMode::Intersect;
        s.objs.back() = cutter;
        s.updateAll();
        CHECK(intersectScene(s, ray, 10, hit) && std::fabs(hit.t - outside) < 0.001f);
        CHECK(s.save("build/t/metaballs.c3d"));
        Scene copy;
        CHECK(copy.load("build/t/metaballs.c3d"));
        CHECK(copy.objs.size() == 4 && copy.objs[2].metaNegative && copy.objs[2].metaRadius == ball.metaRadius);
        CHECK(intersectScene(copy, ray, 10, hit) && std::fabs(hit.t - outside) < 0.001f);
        std::remove("build/t/metaballs.c3d");
    }
    printf("picture planes, cutouts and ray visibility\n");
    {
        const char* imagePath = "build/t/picture fixture.bmp";
        const char* scenePath = "build/t/picture fixture.c3d";
        CHECK(saveBMP(imagePath, 2, 1, {0xff00ff, 0xff0000}));
        Scene s;
        Object card = makeObject(ObjType::Picture, 1);
        card.pos = {};
        card.scl = V3(1);
        card.picture.path = imagePath;
        card.picture.aspect = 2;
        card.picture.chroma = true;
        card.picture.shadows = card.picture.reflections = false;
        s.objs.push_back(card);
        s.updateAll();
        Hit hit;
        Ray opaque{{0.5f, 0, 2}, {0, 0, -1}};
        CHECK(intersectScene(s, opaque, 10, hit) && hit.pictureColor.x > 0.99f && hit.pictureColor.z < 0.01f);
        CHECK(!intersectScene(s, {{-0.5f, 0, 2}, opaque.d}, 10, hit));
        CHECK(!intersectScene(s, {{0.5f, 0.75f, 2}, opaque.d}, 10, hit));
        CHECK(!intersectScene(s, opaque, 10, hit, -1, RayPurpose::Shadow));
        CHECK(!intersectScene(s, opaque, 10, hit, -1, RayPurpose::Reflection));
        s.cam.target = {};
        s.cam.yaw = 90;
        s.cam.pitch = 0;
        s.objs.front().picture.billboard = true;
        s.updateAll();
        CHECK(dot(normalize(s.objs.front().fwd.col(2)), V3(1, 0, 0)) > 0.99f);
        CHECK(s.save(scenePath));
        Scene copy;
        CHECK(copy.load(scenePath));
        CHECK(copy.objs.size() == 1 && copy.objs[0].picture.image && copy.objs[0].picture.billboard &&
              copy.objs[0].picture.chroma && !copy.objs[0].picture.reflections);
        std::remove(imagePath);
        std::remove(scenePath);
    }
    printf("layered materials and texture presets\n");
    {
        const char* scenePath = "build/t/layers fixture.c3d";
        const char* texturePath = "build/t/layers fixture.c3tex";
        const char* libraryPath = "build/t/layers fixture.txt";
        Scene s;
        s.cam.target = {};
        s.cam.yaw = s.cam.pitch = 0;
        s.cam.dist = 4;
        s.sky.mode = 3;
        s.sky.sunAlt = -90;
        s.sky.ambient = 0.5f;
        s.sky.ambientColor = V3(1);
        Object sphere = makeObject(ObjType::Sphere, 1);
        sphere.pos = {};
        sphere.scl = V3(1);
        sphere.mat.ambient = 1;
        sphere.mat.color1 = V3(0.2f);
        s.objs.push_back(sphere);
        uint32_t base = 0, layered = 0, masked = 0;
        renderImage(s, 1, 1, &base, 0);
        TextureLayer layer;
        layer.color1 = {0.9f, 0.1f, 0.1f};
        layer.map = TexMap::World;
        layer.offset = {1, 2, 3};
        layer.rotation.z = 45;
        s.objs[0].mat.layers = {layer};
        renderImage(s, 1, 1, &layered, 0);
        CHECK(((layered >> 16) & 255) > ((base >> 16) & 255));
        CHECK((layered & 255) < (base & 255));

        TextureLayer mask;
        mask.color1 = V3(0);
        mask.influence.fill(0);
        layer.mask = TexMask::Previous;
        layer.blend = TexBlend::Multiply;
        layer.influence[TexReflection] = 0.35f;
        s.objs[0].mat.layers = {mask, layer};
        renderImage(s, 1, 1, &masked, 0);
        CHECK(masked == base);

        CHECK(layer.save(texturePath));
        TextureLayer restored;
        CHECK(restored.load(texturePath) && restored.map == TexMap::World && restored.rotation.z == 45 &&
              restored.offset.y == 2 && restored.influence[TexReflection] == 0.35f &&
              restored.mask == TexMask::Previous && restored.blend == TexBlend::Multiply);
        CHECK(s.save(scenePath));
        Scene copy;
        CHECK(copy.load(scenePath) && copy.objs.size() == 1 && copy.objs[0].mat.layers.size() == 2);
        Library lib, recovered;
        lib.mats.push_back(s.objs[0].mat);
        CHECK(lib.save(libraryPath) && recovered.load(libraryPath) && recovered.mats.size() == 1 &&
              recovered.mats[0].layers.size() == 2);
        std::remove(scenePath);
        std::remove(texturePath);
        std::remove(libraryPath);
    }
    printf("object editor geometry and persistence\n");
    {
        Scene s;
        Object torus = makeObject(ObjType::Torus, 1);
        torus.pos = {};
        torus.scl = V3(1);
        torus.torus = {0.8f, 0.2f, 1.5f, 0, 90};
        torus.update();
        Hit hit;
        CHECK(intersectObject(torus, {{0.8f, 2, 0.2f}, {0, -1, 0}}, 10, hit) && hit.p.y > 0.26f);
        CHECK(!intersectObject(torus, {{-0.8f, 2, 0.2f}, {0, -1, 0}}, 10, hit));
        CHECK(intersectObject(torus, {{0.8f, 0, -1}, {0, 0, 1}}, 10, hit) &&
              std::fabs(hit.p.z) < 0.001f && hit.n.z < -0.99f);

        Object stone = makeObject(ObjType::Stone, 1);
        stone.pos = {};
        stone.scl = V3(1);
        stone.stone = {0, 0, 0.8f};
        stone.update();
        Ray corner{{0.72f, 0.72f, 3}, {0, 0, -1}};
        CHECK(!intersectObject(stone, corner, 10, hit));
        stone.stone.blockiness = 1;
        CHECK(intersectObject(stone, corner, 10, hit) && std::fabs(hit.p.z - 0.8f) < 0.001f);

        Object mesh = makeObject(ObjType::Mesh, 1);
        auto data = std::make_shared<Mesh>();
        data->v = {{-1, -1, 0}, {1, -1, 0}, {0, 1, 0}};
        data->n.assign(3, V3(0, 1, 0));
        data->tri = {0, 1, 2};
        data->build();
        mesh.mesh = data;
        mesh.update();
        Ray front{{0, 0, 2}, {0, 0, -1}};
        CHECK(intersectObject(mesh, front, 10, hit) && hit.n.y > 0.99f);
        mesh.flatMesh = true;
        CHECK(intersectObject(mesh, front, 10, hit) && hit.n.z > 0.99f);

        s.objs = {torus, stone, mesh};
        const char* path = "build/t/object editors.c3d";
        CHECK(s.save(path));
        Scene copy;
        CHECK(copy.load(path) && copy.objs.size() == 3 && copy.objs[0].torus.sweep == 90 &&
              copy.objs[1].stone.blockiness == 1 && copy.objs[2].flatMesh);
        std::remove(path);
    }
    printf("premium / dof / soft shadows\n");
    {
        Scene s = makeExample(3);
        s.render.quality = 2; s.render.samples = 24; s.render.dof = true; s.render.aperture = 1.0f;
        ViewBasis vb = cameraBasis(s.cam);
        for (auto& o : s.objs) {
            if (o.mat.name == "Glass") s.render.focusDist = dot(o.pos - vb.o, vb.f);
            if (o.type == ObjType::Spotlight) o.lightSize = 1.5f;
            if (o.mat.name == "Chrome") o.mat.blur = 0.25f;
        }
        render(s, 640, 400, -1, "premium.bmp");
    }
    printf("panorama\n");
    {
        Scene s = makeExample(0);
        s.render.panorama = true;
        render(s, 800, 400, 1, "pano.bmp");
    }
    printf("sky presets\n");
    {
        std::vector<Scene> v;
        for (auto& k : skyPresets()) {
            Scene s;
            s.sky = k;
            s.objs.push_back(makeObject(ObjType::Ground, 0));
            Object sp = makeObject(ObjType::Sphere, 1);
            sp.pos = {0, 1.5f, 0}; sp.scl = V3(1.5f);
            s.objs.push_back(sp);
            s.cam.target = {0, 3, 0}; s.cam.dist = 9; s.cam.pitch = 2; s.cam.fov = 75;
            s.cam.yaw = k.moon ? k.moonAz + 180 : (k.rainbow > 0 ? k.sunAz : k.sunAz + 160);
            s.updateAll();
            v.push_back(s);
        }
        sheet(v, 200, 120, 6, "skies.bmp");
    }
    printf("material presets\n");
    {
        std::vector<Scene> v;
        for (auto& m : materialPresets()) {
            Scene s;
            s.sky = skyPresets()[0];
            Object g = makeObject(ObjType::Ground, 0);
            g.mat = findMaterial("Checker");
            Object sp = makeObject(ObjType::Sphere, 1);
            sp.pos = {0, 1, 0}; sp.scl = V3(1.f); sp.mat = m;
            if (m.category == "Clouds") { sp.type = ObjType::Cube; sp.scl = {1, 0.05f, 1}; sp.mat.texScale *= 30; }
            s.objs = {g, sp};
            s.cam.target = {0, 1, 0}; s.cam.dist = 4; s.cam.pitch = 14; s.cam.yaw = 30; s.cam.fov = 42;
            s.updateAll();
            v.push_back(s);
        }
        sheet(v, 96, 96, 10, "materials.bmp");
    }
    printf("picture textures + mappings, all primitives, OBJ import, lights\n");
    {
        Scene s = makeEmptyScene();
        s.objs[0].mat = findMaterial("Concrete");
        auto pic = [](TexMap m) { Material x; x.name = "pic"; x.tex = TexType::Image; x.map = m; x.image = "build/t/ex1.bmp"; return x; };
        ObjType types[] = {ObjType::Sphere, ObjType::Cube, ObjType::Cylinder, ObjType::Cone, ObjType::Pyramid, ObjType::Torus, ObjType::Disc, ObjType::Square, ObjType::Stone, ObjType::Lattice};
        TexMap maps[] = {TexMap::Spherical, TexMap::Object, TexMap::Cylindrical, TexMap::Object, TexMap::Object, TexMap::World, TexMap::PlanarXZ, TexMap::PlanarXZ, TexMap::Object, TexMap::Object};
        for (int i = 0; i < 10; i++) {
            Object o = makeObject(types[i], i + 1);
            o.pos = {-12.f + (i % 5) * 6, i < 5 ? 2.f : 1.5f, i < 5 ? 0.f : 7.f};
            o.scl = types[i] == ObjType::Lattice ? V3(2, 1.2f, 2) : V3(2.f);
            o.mat = pic(maps[i]);
            if (types[i] == ObjType::Disc || types[i] == ObjType::Square) { o.pos.y = 0.05f; o.scl = V3(2.4f); }
            s.objs.push_back(o);
        }
        FILE* f = fopen("build/t/oct.obj", "w");
        fprintf(f, "v 0 1 0\nv 1 0 0\nv 0 0 1\nv -1 0 0\nv 0 0 -1\nv 0 -1 0\nf 1 2 3\nf 1 3 4\nf 1 4 5\nf 1 5 2\nf 6 3 2\nf 6 4 3\nf 6 5 4\nf 6 2 5\n");
        fclose(f);
        auto mesh = std::make_shared<Mesh>();
        std::string err;
        CHECK(loadOBJ("build/t/oct.obj", *mesh, err));
        CHECK(mesh->triCount() == 8);
        Object mo = makeObject(ObjType::Mesh, 1);
        mo.mesh = mesh; mo.pos = {10, 2.5f, 7}; mo.scl = V3(2.f); mo.mat = findMaterial("Gold");
        s.objs.push_back(mo);
        Object sp = makeObject(ObjType::Spotlight, 1);
        sp.pos = {0, 12, 4}; sp.spotAngle = 25; sp.lightColor = {1, 0.4f, 0.3f}; sp.lightPower = 3;
        s.objs.push_back(sp);
        Object pl = makeObject(ObjType::Parallel, 1);
        pl.rot = {-40, 60, 0}; pl.lightColor = {0.3f, 0.5f, 1}; pl.lightPower = 0.5f;
        s.objs.push_back(pl);
        s.cam.target = {0, 2, 3}; s.cam.dist = 30; s.cam.pitch = 22; s.cam.yaw = 0;
        s.sky.sunAz = 30;
        s.updateAll();
        render(s, 800, 480, 1, "prims.bmp");
    }
    printf("keyframes\n");
    {
        Scene s = makeEmptyScene();
        Object o = makeObject(ObjType::Sphere, 1);
        putKey(o.keys, Key{0, V3(0.f), V3(0.f), V3(1.f)});
        putKey(o.keys, Key{20, V3(20, 0, 0), V3(0, 90, 0), V3(1.f)});
        putKey(o.keys, Key{10, V3(10, 0, 0), V3(0, 45, 0), V3(1.f)});
        s.objs.push_back(o);
        Sky a = skyPresets()[0], b = a;
        b.sunAlt = 5;
        putKey(s.skyKeys, SkyKey{0, a});
        putKey(s.skyKeys, SkyKey{20, b});
        s.applyFrame(10);
        CHECK(std::fabs(s.objs[1].pos.x - 10) < 0.01f && std::fabs(s.objs[1].rot.y - 45) < 0.01f);
        s.applyFrame(5);
        CHECK(s.objs[1].pos.x > 1 && s.objs[1].pos.x < 10);
        CHECK(std::fabs(s.sky.sunAlt - lerpf(a.sunAlt, 5, 0.25f)) < 0.01f);
        printf("  frame 5 x=%.3f roty=%.3f sun=%.3f\n", s.objs[1].pos.x, s.objs[1].rot.y, s.sky.sunAlt);
        s.applyFrame(30);
        CHECK(std::fabs(s.objs[1].pos.x - 20) < 0.01f);
        CHECK(s.objs[1].keys.size() == 3 && s.objs[1].keys[1].frame == 10);
        printf("  frame 5 x=%.3f roty=%.3f sun=%.3f\n", s.objs[1].pos.x, s.objs[1].rot.y, s.sky.sunAlt);
    }
    printf("save/load round trip\n");
    for (int ex : {3, 4}) {
        Scene a = makeExample(ex);
        a.objs[1].keys.push_back(Key{12, V3(1, 2, 3), V3(4, 5, 6), V3(1.f)});
        a.camKeys.push_back(CamKey{7, a.cam});
        a.memorySet[2] = true;
        a.objs[1].name = "Name with spaces_and_underscores";
        std::string p = "build/t/rt" + std::to_string(ex) + ".c3d";
        CHECK(a.save(p));
        Scene b;
        CHECK(b.load(p));
        CHECK(a.objs.size() == b.objs.size());
        CHECK(b.objs[1].name == a.objs[1].name);
        CHECK(b.objs[1].keys.size() == 1 && b.camKeys.size() == 1 && b.memorySet[2]);
        CHECK(b.csg.size() == a.csg.size());
        for (size_t i = 0; i < a.objs.size() && i < b.objs.size(); i++) {
            CHECK(a.objs[i].group == b.objs[i].group && a.objs[i].boolMode == b.objs[i].boolMode);
            if (a.objs[i].mesh) CHECK(b.objs[i].mesh && b.objs[i].mesh->triCount() == a.objs[i].mesh->triCount());
        }
        auto pa = render(a, 320, 200, 1, ("rt_a" + std::to_string(ex) + ".bmp").c_str());
        auto pb = render(b, 320, 200, 1, ("rt_b" + std::to_string(ex) + ".bmp").c_str());
        size_t diff = 0;
        for (size_t i = 0; i < pa.size(); i++) diff += pa[i] != pb[i];
        printf("  example %d: %zu of %zu pixels differ after reload\n", ex, diff, pa.size());
        CHECK(diff < pa.size() / 200);
    }
    printf("v1 scene compatibility\n");
    {
        FILE* f = fopen("build/t/v1.c3d", "w");
        fprintf(f, "CANYON3D 1\ncamera 0 2 0 20 15 30 50\nsky Clear_Day 0.2 0.4 0.85 0.72 0.8 0.95 1 0.97 0.9 45 135 1.2 0.15 0.75 0.82 0.92 0 3 0.85 0.85 0.88 0.25 1 1 1 1 0.5 0.55 0.65 0.85 0 1\n"
                   "object 6 Ground_1 0 0 0 0 0 0 1 1 1 1 1 1 1\nmaterial Earth 5 0.5 0.43 0.34 0.33 0.28 0.22 0.9 0.3 0.05 10 0 0 1 0.2 0.4 0\n"
                   "object 0 Sphere_1 0 2 0 0 0 0 2 2 2 1 1 1 1\nmaterial Chrome 0 0.9 0.9 0.95 0 0 0 0.08 0.1 1 150 0.9 0 1 0 1 0\nend\n");
        fclose(f);
        Scene s;
        CHECK(s.load("build/t/v1.c3d"));
        CHECK(s.objs.size() == 2 && s.objs[1].name == "Sphere 1" && s.objs[1].mat.reflect > 0.8f && s.sky.name == "Clear Day");
    }
    printf("terrain tools\n");
    {
        Heightfield f;
        hfInit(f, 129);
        hfFractal(f, 3, 0.5f, false);
        hfTerrace(f, 5); hfSpikes(f, 1, 8); hfCrater(f, 0.5f, 0.5f, 0.3f, 0.3f); hfRoundEdges(f); hfMound(f); hfNoise(f, 2, 0.05f); hfSharpen(f);
        for (int m = 0; m < 4; m++) hfBrush(f, 0.3f, 0.3f, 0.1f, 0.2f, (BrushMode)m);
        auto img = loadBMP("build/t/ex1.bmp");
        CHECK(img && img->w == 640);
        if (img) hfFromImage(f, *img);
        bool ok = true;
        for (float v : f.h) ok &= v >= 0 && v <= 1;
        CHECK(ok);
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL CHECKS PASSED", fails);
    return fails ? 1 : 0;
}
