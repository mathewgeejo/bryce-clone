#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cstdio>
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
