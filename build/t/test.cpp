#define _CRT_SECURE_NO_WARNINGS
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../src/render.h"
#include "../../src/simulation.h"
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

static bool closedPolygonShell(const PolygonMesh& mesh) {
    std::map<std::pair<uint32_t, uint32_t>, std::pair<int, int>> uses;
    for (const auto& face : mesh.faces) {
        for (size_t i = 0; i < face.size(); ++i) {
            uint32_t a = face[i].vertex, b = face[(i + 1) % face.size()].vertex;
            auto& use = uses[std::minmax(a, b)];
            ++use.first;
            use.second += a < b ? 1 : -1;
        }
    }
    return !uses.empty() && std::all_of(uses.begin(), uses.end(), [](const auto& edge) {
        return edge.second.first == 2 && edge.second.second == 0;
    });
}

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
    printf("reusable rig actions, clip blending, and retargeting\n");
    {
        Rig rig;
        Joint root, child;
        root.name = "Shoulder";
        root.keys = {{10, {}, {}, V3(1), Ease::Linear}, {14, {4,0,0}, {}, V3(1), Ease::Linear}};
        child.name = "Elbow";
        child.parent = 0;
        child.head = root.tail;
        child.tail = {0,2,0};
        rig.joints = {root, child};
        RigIK chain;
        chain.end = 1;
        chain.settings = {0, {2,1,0}, {0,0,1}, 0, Ease::Linear};
        rig.ik.push_back(chain);
        auto mesh = std::make_shared<Mesh>(makeEditableBox());
        CHECK(bindRig(rig, *mesh));
        auto action = captureRigAction(rig, 10, 14, "Reach");
        CHECK(action && action->frames == 5 && action->joints[0].keys.size() == 5 &&
              action->joints[1].keys.size() == 1 && action->ik[0].keys.size() == 1);
        if (action) {
            rig.actions.push_back(action);
            rig.joints[0].keys.clear();
            rig.clips.push_back({0, 20, 2, 2.f});
            CHECK(validRig(rig, mesh->v.size()) && rigClipFrames(*action, rig.clips[0]) == 5);
            auto x = [&](int frame) { return poseRig(rig, frame)[0].head.x; };
            CHECK(std::fabs(x(21) - 2) < .001f && std::fabs(x(22) - 4) < .001f &&
                  std::fabs(x(23) - 1) < .001f && std::fabs(x(25)) < .001f);

            auto overlay = std::make_shared<RigAction>();
            overlay->name = "Offset";
            overlay->frames = 3;
            overlay->joints = {{0, {{0, {10,0,0}, {}, V3(1), Ease::Linear}}}};
            rig.actions.push_back(overlay);
            rig.clips.push_back({1, 22, 1, 1, .5f, 1, 1});
            CHECK(std::fabs(x(22) - 4) < .001f && std::fabs(x(23) - 5.5f) < .001f &&
                  std::fabs(x(24) - 3) < .001f);
            auto repeated = captureRigAction(rig, 22, 24, "Rendered overlap");
            CHECK(repeated && std::fabs(repeated->joints[0].keys[1].pos.x - 5.5f) < .001f);
            Rig baked = rig;
            CHECK(!bakeRigClips(baked, 24) && baked.clips.size() == 2);
            CHECK(bakeRigClips(baked, 26) && baked.clips.empty() && baked.actions.size() == 2);
            for (int frame = 0; frame < 26; ++frame)
                CHECK(std::fabs(poseRig(baked, frame)[0].head.x - x(frame)) < .001f);

            Rig target;
            target.joints = rig.joints;
            target.ik = rig.ik;
            for (auto& joint : target.joints) {
                joint.head = V3(4, 0, 0) + joint.head * 2;
                joint.tail = V3(4, 0, 0) + joint.tail * 2;
            }
            auto resized = transferRigAction(rig, 0, target);
            CHECK(resized && std::fabs(resized->joints[0].keys[2].pos.x - 4) < .001f &&
                  length(resized->ik[0].keys[0].target - V3(8,2,0)) < .001f);
            target.joints[1].name = target.joints[0].name;
            CHECK(!transferRigAction(rig, 0, target));

            Object actor = makeObject(ObjType::Mesh, 1);
            actor.mesh = mesh;
            actor.rig = std::make_shared<Rig>(rig);
            Scene scene;
            scene.frames = 26;
            scene.objs.push_back(actor);
            scene.applyFrame(23);
            CHECK(scene.save("build/t/rig-actions.c3d"));
            Scene reopened;
            CHECK(reopened.load("build/t/rig-actions.c3d"));
            if (!reopened.objs.empty() && reopened.objs[0].rig) {
                CHECK(reopened.objs[0].rig->actions.size() == 2 && reopened.objs[0].rig->clips.size() == 2);
                CHECK(length(reopened.objs[0].visibleMesh()->v[0] - scene.objs[0].visibleMesh()->v[0]) < .001f);
                Scene appended;
                appended.frames = 2;
                appended.append(reopened);
                CHECK(appended.frames >= 25);
            }
            rig.ik.clear();
            remapRigActionTracks(rig, -1, 1);
            CHECK(validRig(rig, mesh->v.size()) && rig.actions[0]->ik.empty());
            removeRigAction(rig, 0);
            CHECK(validRig(rig, mesh->v.size()) && rig.clips.size() == 1 && rig.clips[0].action == 0);
        }
    }
    printf("animated limb IK and topology cleanup\n");
    {
        Rig rig;
        Joint upper, lower, hand;
        upper.name = "Upper";
        lower.name = "Lower";
        lower.parent = 0;
        lower.head = {0,1,0};
        lower.tail = {0,2,0};
        hand.name = "Hand";
        hand.parent = 1;
        hand.head = lower.tail;
        hand.tail = {0,2.5f,0};
        rig.joints = {upper, lower, hand};
        RigIK chain;
        chain.end = 1;
        chain.settings = {0, {1,1,0}, {0,0,1}, 1, Ease::Linear};
        chain.keys = {{0, {1,1,0}, {0,0,1}, 0, Ease::Linear}, {10, {1,1,0}, {0,0,1}, 1, Ease::Linear},
                      {20, {10,0,0}, {0,0,1}, 1, Ease::Linear}};
        rig.ik.push_back(chain);
        auto base = std::make_shared<Mesh>(makeEditableBox());
        CHECK(bindRig(rig, *base) && validRig(rig, base->v.size()));
        auto fk = poseRig(rig, 0);
        auto half = poseRig(rig, 5);
        auto solved = poseRig(rig, 10);
        auto far = poseRig(rig, 20);
        CHECK(length(fk[1].tail - V3(0,2,0)) < .001f);
        CHECK(length(half[1].tail - chain.settings.target) < length(fk[1].tail - chain.settings.target));
        CHECK(length(solved[1].tail - chain.settings.target) < .001f);
        CHECK(solved[1].head.z > .5f && length(solved[2].head - solved[1].tail) < .001f);
        CHECK(length(far[1].tail - V3(2,0,0)) < .001f);
        Rig overlap = rig;
        overlap.ik.push_back({2, chain.settings, {}});
        CHECK(!validRig(overlap, base->v.size()));
        Scene scene;
        Object actor = makeObject(ObjType::Mesh, 1);
        actor.mesh = base;
        actor.rig = std::make_shared<Rig>(rig);
        scene.objs.push_back(actor);
        scene.applyFrame(0);
        auto before = scene.objs[0].posedMesh;
        scene.applyFrame(10);
        CHECK(before && scene.objs[0].posedMesh && length(before->v[6] - scene.objs[0].posedMesh->v[6]) > .1f);
        CHECK(scene.save("build/t/limb-ik.c3d"));
        Scene reopened;
        CHECK(reopened.load("build/t/limb-ik.c3d"));
        if (!reopened.objs.empty() && reopened.objs[0].rig) {
            reopened.applyFrame(20);
            auto after = poseRig(*reopened.objs[0].rig, 20);
            CHECK(after.size() == 3 && length(after[1].tail - V3(2,0,0)) < .001f);
        }

        Mesh hole = makeEditableBox();
        CHECK(deleteMeshFaces(hole, {6, 7}));
        std::vector<std::pair<uint32_t, uint32_t>> border;
        for (size_t i = 0; i < hole.tri.size(); i += 3) {
            for (int j = 0; j < 3; ++j) {
                uint32_t a = hole.tri[i + j], b = hole.tri[i + (j + 1) % 3];
                if (hole.v[a].y == 1 && hole.v[b].y == 1) border.emplace_back(a, b);
            }
        }
        CHECK(border.size() == 4 && fillMeshBoundary(hole, border) && hole.triCount() == 12);
        CHECK(!fillMeshBoundary(hole, border));
        float t;
        V3 n;
        CHECK(hole.intersect({0,3,0}, {0,-1,0}, 0, 10, t, n, true) && std::fabs(t - 2) < .001f && n.y > 0);
        Mesh concave;
        concave.v = {{0,0,1}, {2,0,1}, {2,2,1}, {1,1,1}, {0,2,1}};
        for (int i = 0; i < 5; ++i) {
            V3 top = concave.v[i];
            concave.v.push_back(top - V3(0,0,1));
        }
        border.clear();
        for (uint32_t i = 0; i < 5; ++i) {
            uint32_t next = (i + 1) % 5;
            concave.tri.insert(concave.tri.end(), {i, i+5, next+5, i, next+5, next});
            border.emplace_back(i, next);
        }
        CHECK(fillMeshBoundary(concave, border) && concave.triCount() == 13);
        CHECK(concave.intersect({.5f,.5f,4}, {0,0,-1}, 0, 10, t, n, true) && n.z > 0);
        CHECK(!concave.intersect({1,1.75f,4}, {0,0,-1}, 0, 10, t, n, true));
        Mesh seam = makeEditableBox();
        projectMeshUV(seam, 1);
        V3 split = seam.v[0], uv = seam.uv[0];
        seam.v.push_back(split + V3(.0001f,0,0));
        seam.uv.push_back(uv + V3(.2f,0,0));
        seam.tri[0] = 8;
        CHECK(!weldMeshVertices(seam, {8, 0}, .001f, true) && seam.v.size() == 9);
        CHECK(weldMeshVertices(seam, {8, 0}, .001f, false) && seam.v.size() == 8 &&
              seam.triCount() == 12 && seam.uv.size() == seam.v.size());
    }
    printf("rig bind pose, animation, linked meshes, and persistence\n");
    {
        auto base = std::make_shared<Mesh>(makeEditableBox());
        Rig rig;
        Joint root;
        root.name = "Pelvis";
        root.head = {0, -1, 0};
        root.tail = {};
        Joint child;
        child.name = "Spine";
        child.parent = 0;
        rig.joints = {root, child};
        CHECK(bindRig(rig, *base));
        SkinWeights full{{0,1,2,3}, {.1f,.2f,.3f,.4f}};
        CHECK(setSkinWeight(full, 4, .5f, 5));
        CHECK(full.joint[3] == 4 && full.weight[3] == .5f &&
              std::fabs(full.weight[0] + full.weight[1] + full.weight[2] - .5f) < .001f);
        for (size_t v = 0; v < base->v.size(); ++v)
            CHECK(setSkinWeight(rig.weights[v], base->v[v].y < 0 ? 0 : 1, 1, 2));
        CHECK(validRig(rig, base->v.size()));
        auto rest = deformRig(rig, *base, 0);
        CHECK(rest && length(rest->v[6] - base->v[6]) < .001f);
        rig.joints[0].keys = {{0, {}, {}, V3(1), Ease::Linear}, {20, {3,0,0}, {}, V3(1), Ease::Linear}};
        rig.joints[1].keys = {{0, {}, {}, V3(1), Ease::Linear}, {20, {}, {0,0,90}, V3(1), Ease::Linear}};
        auto pose = poseRig(rig, 20);
        CHECK(pose.size() == 2 && length(pose[1].tail - V3(2,0,0)) < .001f);
        Scene scene;
        Object actor = makeObject(ObjType::Mesh, 1);
        actor.name = "Actor";
        actor.mesh = base;
        actor.rig = std::make_shared<Rig>(rig);
        Object link = actor;
        link.rig.reset();
        link.instanceSource = 0;
        link.pos = {10, 0, 0};
        scene.objs = {actor, link};
        scene.applyFrame(10);
        const auto* geometry = scene.objs[0].visibleMesh();
        CHECK(geometry != base.get() && geometry == scene.objs[1].visibleMesh());
        CHECK(std::fabs(geometry->v[0].x - .5f) < .001f);
        Hit hit;
        CHECK(intersectScene(scene, {{2, -.8f, 5}, {0,0,-1}}, 20, hit) && hit.obj == 0);
        auto detached = scene.subset({1});
        CHECK(detached.objs[0].rig == actor.rig && detached.objs[0].instanceSource < 0);
        auto exported = meshSelection(scene, {0}, 24);
        CHECK(exported.size() == 1 && length(exported[0].mesh.v[0] - geometry->v[0]) < .001f);
        CHECK(scene.save("build/t/rigging.c3d"));
        Scene reopened;
        CHECK(reopened.load("build/t/rigging.c3d"));
        if (reopened.objs.size() == 2 && reopened.objs[0].rig) {
            CHECK(reopened.frame == 10 && reopened.objs[0].rig->joints[1].name == "Spine");
            CHECK(length(reopened.objs[0].visibleMesh()->v[6] - geometry->v[6]) < .001f);
            reopened.applyFrame(20);
            CHECK(length(reopened.objs[0].visibleMesh()->v[6] - V3(2,1,1)) < .001f);
        }
        rig.joints[0].parent = 1;
        CHECK(!validRig(rig, base->v.size()));
    }
    printf("editable mesh topology and curve sampling\n");
    {
        Mesh box = makeEditableBox();
        CHECK(box.v.size() == 8 && box.triCount() == 12);
        CHECK(extrudeMeshFaces(box, {6, 7}, .5f));
        CHECK(box.v.size() == 12 && box.triCount() == 20);
        CHECK(std::fabs(box.bmax.y - 1.5f) < .001f);
        projectMeshUV(box, 1);
        CHECK(box.uv.size() == box.v.size());
        CHECK(subdivideMesh(box) && box.triCount() == 80);
        CHECK(box.uv.size() == box.v.size() && box.n.size() == box.v.size());
        CHECK(deleteMeshFaces(box, {0, 3}) && box.triCount() == 78);
        CHECK(!deleteMeshFaces(box, {9000}) && box.triCount() == 78);
        auto mesh = std::make_shared<Mesh>(std::move(box));
        Object edited = makeObject(ObjType::Mesh, 1);
        edited.mesh = mesh;
        edited.keys = {{0, {0,0,0}, {}, V3(1), Ease::Linear}, {8, {8,0,0}, {}, V3(1), Ease::Hold},
                       {12, {20,0,0}, {}, V3(1), Ease::Linear}};
        CHECK(std::fabs(edited.sampleTransform(2.5f).pos.x - 2.5f) < .001f);
        CHECK(edited.sampleTransform(11.5f).pos.x == 8 && edited.transformAt(12).pos.x == 20);
        Scene scene;
        scene.objs.push_back(edited);
        scene.applyFrame(4);
        CHECK(scene.save("build/t/mesh-edit.c3d"));
        Scene reopened;
        CHECK(reopened.load("build/t/mesh-edit.c3d"));
        if (!reopened.objs.empty() && reopened.objs[0].mesh) {
            CHECK(reopened.objs[0].mesh->triCount() == 78 && reopened.objs[0].mesh->uv.size() == mesh->uv.size());
            CHECK(std::fabs(reopened.objs[0].sampleTransform(2.5f).pos.x - 2.5f) < .001f);
        }
    }
    printf("independent transform curves and Bezier handles\n");
    {
        CurveTrack bezier{{0, 0, CurveEase::Bezier, {}, {.2f, 6}}, {20, 10, CurveEase::Linear, {.4f, -1}}};
        CHECK(std::fabs(sampleCurve(bezier, 8.5f, 0) - 6.875f) < .0001f);
        CHECK(sampleCurve(bezier, -1, 5) == 0 && sampleCurve(bezier, 22, 5) == 10);
        auto divided = bezier;
        CHECK(insertCurveSample(divided, 8, 0) && !insertCurveSample(divided, 8, 0) && divided.size() == 3);
        for (float f = 0; f <= 20; f += .25f) CHECK(std::fabs(sampleCurve(bezier, f, 0) - sampleCurve(divided, f, 0)) < .0001f);

        Object legacy = makeObject(ObjType::Cube, 1);
        legacy.keys = {{0, {0,1,2}, {3,4,5}, {2,3,4}, Ease::Spline},
                       {9, {3,5,7}, {10,12,14}, {3,4,5}, Ease::Hold},
                       {16, {6,7,8}, {20,24,28}, {2,5,3}, Ease::Smooth},
                       {22, {8,9,10}, {5,10,15}, {1,2,3}, Ease::Linear}};
        Object object = legacy;
        CHECK(replaceTransformChannel(object.keys, object.curves, 0, divided) && object.keys.empty());
        for (float f = 0; f <= 22; f += .5f) {
            Key before = legacy.sampleTransform(f), after = object.sampleTransform(f);
            for (int channel = 1; channel < TRANSFORM_CHANNELS; ++channel)
                CHECK(std::fabs(transformChannelValue(before, channel) - transformChannelValue(after, channel)) < .0001f);
        }
        auto snapshot = object.curves;
        CHECK(!retimeTransformKeys(object.curves, 8, 20) && object.curves == snapshot);
        CHECK(retimeTransformKeys(object.curves, 8, 7) && object.curves->channels[0][1].frame == 7 &&
              object.curves->channels[1][1].frame == 9 && snapshot->channels[0][1].frame == 8);
        CHECK((transformKeyFrames(object.keys, object.curves.get()) == std::vector<int>{0,7,9,16,20,22}));
        CHECK(!replaceTransformChannel(object.keys, object.curves, 0, {{0,1}, {0,2}}));
        CHECK(!replaceTransformChannel(object.keys, object.curves, 0, {{0,1,CurveEase::Bezier, {1.1f, 0}}}));
        auto existing = object.curves->channels[0].front();
        CHECK(setTransformKey(object.keys, object.curves, Key{0, {2,1,2}, {3,4,5}, {2,3,4}}));
        CHECK(object.curves->channels[0].front().value == 2 &&
              object.curves->channels[0].front().out.offset == existing.out.offset &&
              object.curves->channels[0].front().ease == CurveEase::Bezier);
        CHECK(removeTransformKeys(object.keys, object.curves, 7) && object.curves->channels[0].size() == 2);

        Rig rig;
        Joint root;
        root.name = "Root";
        CHECK(replaceTransformChannel(root.keys, root.curves, 0, bezier, true));
        CHECK(setTransformKey(root.keys, root.curves, Key{3, {0,4,0}}, true, 1u << 1));
        CHECK(root.curves->channels[0].size() == 2 && root.curves->channels[1].size() == 1);
        rig.joints = {root};
        Object actor = makeObject(ObjType::Mesh, 1);
        actor.mesh = std::make_shared<Mesh>(makeEditableBox());
        CHECK(bindRig(rig, *actor.mesh));
        actor.rig = std::make_shared<Rig>(rig);
        Scene scene;
        scene.objs = {object, actor, legacy};
        scene.frames = 24;
        scene.applyFrame(2);
        auto first = scene.objs[1].posedMesh;
        scene.applyFrame(8);
        CHECK(first && scene.objs[1].posedMesh && length(first->v[0] - scene.objs[1].posedMesh->v[0]) > 2);
        const char* path = "build/t/curves-roundtrip.c3d";
        CHECK(scene.save(path));
        {
            std::ifstream file(path);
            std::string header;
            std::getline(file, header);
            CHECK(header == "CANYON3D 11");
        }
        Scene loaded;
        CHECK(loaded.load(path) && loaded.objs.size() == 3);
        if (loaded.objs.size() == 3) {
            CHECK(loaded.objs[0].curves && loaded.objs[0].keys.empty() && loaded.objs[1].rig &&
                  loaded.objs[1].rig->joints[0].curves && loaded.objs[2].keys.size() == 4);
            loaded.applyFrame(8);
            CHECK(length(loaded.objs[0].pos - scene.objs[0].pos) < .0001f);
            CHECK(loaded.objs[1].posedMesh && scene.objs[1].posedMesh &&
                  length(loaded.objs[1].posedMesh->v[0] - scene.objs[1].posedMesh->v[0]) < .0001f);
        }
        std::remove(path);
        auto action = captureRigAction(rig, 0, 4, "Curved motion");
        CHECK(action && action->joints.size() == 1 && action->joints[0].keys.back().pos.x > 1);
        if (action) {
            rig.actions.push_back(action);
            rig.clips.push_back({0, 6});
            CHECK(bakeRigClips(rig, 16) && rig.joints[0].curves && rig.joints[0].keys.empty());
            CHECK(rig.joints[0].curves->channels[0].back().frame == 20 &&
                  rig.joints[0].curves->channels[0].back().in.offset == -1);
        }
    }
    {
        Scene scene;
        Object ball = makeObject(ObjType::Sphere, 1);
        ball.pos = {0, 6, 0};
        CHECK(replaceTransformChannel(ball.keys, ball.curves, 1, {{0,6,CurveEase::Hold}, {60,3,CurveEase::Linear}}));
        scene.objs.push_back(ball);
        RigidSettings settings;
        settings.time.frames = 12;
        settings.time.startFrame = 8;
        std::string error;
        CHECK(bakeRigidBodies(scene, {0}, settings, error));
        const auto& track = scene.objs[0].curves->channels[1];
        CHECK(track.front().frame == 0 && track.front().ease == CurveEase::Hold &&
              track.back().frame == 60 && track.back().value == 3 &&
              scene.objs[0].transformAt(16).pos.y < 6 && scene.objs[0].keys.empty());
    }
    printf("rigid-body and liquid simulation bakes\n");
    {
        Scene scene;
        scene.fps = 30;
        scene.frames = 20;
        Object terrain = makeObject(ObjType::Terrain, 1);
        terrain.pos = {};
        terrain.scl = {10, 2, 10};
        hfInit(*terrain.hf, 9);
        hfAdd(*terrain.hf, .5f);
        Object lower = makeObject(ObjType::Cube, 1);
        lower.pos = {0, 2, 0};
        lower.scl = V3(.5f);
        Object upper = makeObject(ObjType::Sphere, 1);
        upper.pos = {0, 5, 0};
        upper.scl = V3(.5f);
        upper.keys.push_back({100, {0, 6, 0}, {}, upper.scl, Ease::Linear});
        scene.objs = {terrain, lower, upper};
        RigidSettings rigid;
        rigid.time.frames = 90;
        rigid.bounce = .2f;
        std::string error;
        bool baked = bakeRigidBodies(scene, {1, 2}, rigid, error);
        CHECK(baked);
        if (baked) {
            CHECK(scene.frames == 90 && scene.objs[2].keys.back().frame == 100);
            scene.applyFrame(12);
            CHECK(scene.objs[2].worldPos.y < 5 && scene.objs[2].worldPos.y > 3);
            scene.applyFrame(89);
            CHECK(std::fabs(scene.objs[1].worldPos.y - 1.5f) < .03f);
            CHECK(std::fabs(scene.objs[2].worldPos.y - 2.5f) < .05f);
        }
        Scene water;
        water.fps = 30;
        Object source = makeObject(ObjType::Cube, 1);
        source.pos = {0, 3, 0};
        source.scl = V3(1);
        water.objs.push_back(source);
        LiquidSettings liquid;
        liquid.time.frames = 48;
        liquid.time.velocity = {0, 0, .55f};
        liquid.resolution = 3;
        CHECK(bakeLiquid(water, 0, liquid, error) == 27);
        if (water.objs.size() == 28) {
            CHECK(water.objs[0].hidden && water.metaballs.size() == 1);
            Hit hit;
            CHECK(intersectScene(water, {{0, 3, 8}, {0, 0, -1}}, 20, hit) && hit.obj > 0);
            water.applyFrame(47);
            float peak = 0, width = 0;
            for (size_t i = 1; i < water.objs.size(); ++i) {
                V3 p = water.objs[i].worldPos;
                CHECK(p.y >= .47f);
                peak = std::max(peak, p.y);
                width = std::max(width, std::fabs(p.x));
            }
            CHECK(peak < 2.5f && width > .8f);
            const char* path = "build/t/liquid bake.c3d";
            CHECK(water.save(path));
            Scene reopened;
            CHECK(reopened.load(path) && reopened.objs.size() == water.objs.size());
            if (reopened.objs.size() == water.objs.size()) {
                reopened.applyFrame(47);
                CHECK(length(reopened.objs.back().worldPos - water.objs.back().worldPos) < 1e-4f);
                CHECK(reopened.metaballs.size() == 1);
            }
        }
    }
    printf("preset forces, rolling orientation, and ranged animation clips\n");
    {
        auto fall = [](GravityPreset preset) {
            Scene scene;
            scene.fps = 30;
            auto body = makeObject(ObjType::Sphere, 1);
            body.pos = {5, 10, 0};
            body.scl = V3(.5f);
            scene.objs.push_back(body);
            RigidSettings settings;
            applyGravityPreset(settings.time, preset);
            settings.time.frames = 31;
            settings.time.floor = false;
            settings.time.startFrame = 12;
            std::string error;
            CHECK(bakeRigidBodies(scene, {0}, settings, error));
            scene.applyFrame(11);
            CHECK(length(scene.objs[0].pos - body.pos) < 1e-5f);
            scene.applyFrame(42);
            return scene.objs[0].pos;
        };
        CHECK(std::fabs(fall(GravityPreset::Earth).y - 5.095f) < .001f);
        CHECK(std::fabs(fall(GravityPreset::Moon).y - 9.19f) < .001f);
        CHECK(std::fabs(fall(GravityPreset::Reverse).y - 14.905f) < .001f);
        CHECK(std::fabs(fall(GravityPreset::Zero).y - 10.f) < .001f);
        CHECK(fall(GravityPreset::Attract).x < 5.f && fall(GravityPreset::Repel).x > 5.f);

        Scene ranged;
        ranged.fps = 30;
        auto ball = makeObject(ObjType::Sphere, 1);
        ball.scl = V3(.5f);
        ball.keys = {{0, {0, .5f, 0}, {}, ball.scl, Ease::Linear},
                     {50, {10, .5f, 0}, {}, ball.scl, Ease::Linear}};
        ranged.objs.push_back(ball);
        ranged.applyFrame(10);
        RigidSettings rolling;
        rolling.time.startFrame = 10;
        rolling.time.frames = 20;
        rolling.time.velocity = {2, 0, 0};
        applyRigidPreset(rolling, RigidPreset::Rubber);
        std::string error;
        CHECK(bakeRigidBodies(ranged, {0}, rolling, error));
        CHECK(ranged.frame == 10);
        ranged.applyFrame(9);
        CHECK(std::fabs(ranged.objs[0].pos.x - 1.8f) < .001f);
        ranged.applyFrame(29);
        CHECK(std::fabs(ranged.objs[0].rot.z) > 10.f);
        ranged.applyFrame(30);
        CHECK(std::fabs(ranged.objs[0].pos.x - 6.f) < .001f);

        Scene liquid;
        auto source = makeObject(ObjType::Cube, 1);
        source.pos = {0, 4, 0};
        source.scl = V3(1);
        liquid.objs.push_back(source);
        LiquidSettings syrup;
        applyLiquidPreset(syrup, LiquidPreset::Syrup);
        CHECK(syrup.viscosity > .7f && syrup.cohesion > .5f);
        syrup.time.frames = 4;
        syrup.time.startFrame = 12;
        syrup.time.detail = 2;
        syrup.resolution = 8;
        CHECK(bakeLiquid(liquid, 0, syrup, error) == 512);
        if (liquid.objs.size() > 1) {
            CHECK(liquid.frame == 12 && liquid.objs[1].mat.name == "Syrup");
            liquid.applyFrame(11);
            CHECK(!liquid.objs[0].hidden && liquid.objs[1].hidden && liquid.metaballs.empty());
            liquid.applyFrame(12);
            CHECK(liquid.objs[0].hidden && !liquid.objs[1].hidden && liquid.metaballs.size() == 1);
            const char* path = "build/t/fine syrup.c3d";
            CHECK(liquid.save(path));
            Scene reopened;
            CHECK(reopened.load(path) && reopened.objs.size() == liquid.objs.size());
            if (reopened.objs.size() > 1) {
                reopened.applyFrame(11);
                CHECK(reopened.objs[1].hidden);
                reopened.applyFrame(15);
                CHECK(!reopened.objs[1].hidden && reopened.objs[1].pos.y < liquid.objs[1].pos.y);
            }
        }
    }
    printf("scene and terrain symmetry\n");
    {
        Scene scene;
        Object parent = makeObject(ObjType::Disc, 1);
        parent.pos = {5, 2, 3};
        parent.rot = {10, 20, 30};
        Object child = makeObject(ObjType::Terrain, 1);
        child.pos = {7, 1, 4};
        hfInit(*child.hf, 5);
        Object locked = makeObject(ObjType::Disc, 2);
        locked.locked = true;
        scene.objs = {parent, child, locked};
        scene.updateAll();
        CHECK(scene.setParent(1, 0));
        CHECK(scene.duplicateMirrored({0, 1, 1, 2}, 0, {2, 0, 0}) == 2);
        if (scene.objs.size() == 5) {
            CHECK(scene.objs[4].parent == 3 && scene.objs[4].hf != scene.objs[1].hf);
            for (int i = 0; i < 2; ++i) {
                V3 original = scene.objs[i].worldPos;
                CHECK(length(scene.objs[3 + i].worldPos - V3(4 - original.x, original.y, original.z)) < 1e-4f);
            }
            auto parts = meshSelection(scene, {3}, 24);
            CHECK(parts.size() == 1);
            if (!parts.empty()) {
                const Mesh& mesh = parts[0].mesh;
                V3 a = mesh.v[mesh.tri[0]], b = mesh.v[mesh.tri[1]], c = mesh.v[mesh.tri[2]];
                CHECK(dot(cross(b - a, c - a), scene.objs[3].nrm * V3(0, 1, 0)) > 0);
            }
            auto& heights = *scene.objs[4].hf;
            heights.h[5] = .25f;
            heights.h[9] = .75f;
            hfMirrorX(heights, true);
            CHECK(heights.at(4, 1) == .25f && scene.objs[1].hf->at(4, 1) == 0.f);
            heights.h[9] = .75f;
            heights.h[7] = .5f;
            hfMirrorX(heights, false);
            CHECK(heights.at(0, 1) == .75f && heights.at(2, 1) == .5f);
            const char* path = "build/t/mirrored.c3d";
            CHECK(scene.save(path));
            Scene reopened;
            CHECK(reopened.load(path) && reopened.objs.size() == 5);
            if (reopened.objs.size() == 5) {
                CHECK(reopened.objs[4].parent == 3);
                CHECK(length(reopened.objs[4].worldPos - scene.objs[4].worldPos) < 1e-4f);
            }
        }
    }
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
    printf("polygon topology, editing, interchange and skinning\n");
    {
        const char* source = "build/t/polygon-source.obj";
        const char* output = "build/t/polygon-export.obj";
        const char* sceneFile = "build/t/polygon-roundtrip.c3d";
        std::ofstream(source) <<
            "v 0 0 0\nv 2 0 0\nv 2 1 0\nv 1 1 0\nv 1 2 0\nv 0 2 0\nv 1 0 1\n"
            "vt 0 0\nvt 1 0\nvt 1 .5\nvt .5 .5\nvt .5 1\nvt 0 1\nvt .25 0\nvt .75 0\nvt .5 1\n"
            "vn 0 0 1\nf 1/1/1 2/2/1 3/3/1 4/4/1 5/5/1 6/6/1\nf -6/8 -7/7 -1/9\n";
        Mesh mesh, reopenedOBJ;
        std::string error;
        CHECK(loadOBJ(source, mesh, error) && mesh.polygons);
        if (mesh.polygons) {
            CHECK(mesh.polygons->faces.size() == 2 && mesh.polygons->faces[0].size() == 6 &&
                  mesh.triCount() == 5 && mesh.triangleFaces[3] == 0 && mesh.triangleFaces[4] == 1);
            CHECK(mesh.v.size() == 9 && mesh.skinVertices().size() == 7 &&
                  mesh.polygons->faces[1][1].vertex == mesh.polygons->faces[0][0].vertex &&
                  mesh.polygons->faces[1][1].uv != mesh.polygons->faces[0][0].uv);
            V3 normal;
            float distance = 0;
            CHECK(mesh.intersect({-.75f, .75f, 1}, {0,0,-1}, .001f, 5, distance, normal));
            CHECK(!mesh.intersect({.75f, .75f, 1}, {0,0,-1}, .001f, 5, distance, normal));
            Rig rig;
            rig.joints.push_back(Joint{});
            rig.joints.back().name = "Root";
            rig.joints.back().tail = {0, 1, 0};
            rig.joints.back().pose.pos = {.25f, 0, 0};
            CHECK(bindRig(rig, mesh) && rig.weights.size() == 7);
            auto posed = deformRig(rig, mesh, 0);
            CHECK(posed && length(posed->v[0] - mesh.v[0]) > .2f && length(posed->v[0] - posed->v[7]) < .001f);
            Scene scene;
            Object object = makeObject(ObjType::Mesh, 1);
            object.mesh = std::make_shared<Mesh>(mesh);
            object.rig = std::make_shared<Rig>(rig);
            object.scl = {-1, 1, 1};
            scene.objs.push_back(object);
            scene.applyFrame(0);
            CHECK(scene.save(sceneFile));
            std::ifstream file(sceneFile);
            std::string header;
            std::getline(file, header);
            CHECK(header == "CANYON3D 10");
            file.close();
            Scene reopened;
            CHECK(reopened.load(sceneFile) && reopened.objs.size() == 1 && reopened.objs[0].mesh->polygons &&
                  reopened.objs[0].rig && reopened.objs[0].rig->weights.size() == 7);
            auto layers = meshSelection(reopened, {0}, 24);
            CHECK(layers.size() == 1 && layers[0].mesh.polygons && layers[0].mesh.polygons->faces[0].size() == 6);
            CHECK(saveGeometry(output, layers, false) && loadOBJ(output, reopenedOBJ, error) &&
                  reopenedOBJ.polygons && reopenedOBJ.polygons->faces[0].size() == 6 && reopenedOBJ.v.size() == 9);
        }
        for (const char* file : {source, output, sceneFile}) std::remove(file);
    }
    {
        Mesh mesh;
        auto cube = makePolygonBox();
        projectPolygonUV(cube, 2);
        CHECK(!insetPolygonFace(cube, 0, 1.1f) && cube.faces.size() == 6);
        CHECK(insetPolygonFace(cube, 0, .25f) && cube.faces.size() == 10 && cube.positions.size() == 12);
        CHECK(length(cube.positions[8] - V3(-.75f, -.75f, -1)) < .001f);
        CHECK(length(cube.uv[cube.faces[0][0].uv] - V3(.125f,.125f,0)) < .001f);
        CHECK(buildPolygonMesh(cube, mesh) && mesh.triCount() == 20);
        CHECK(extrudePolygonFaces(cube, {0}, .5f) && buildPolygonMesh(cube, mesh) && mesh.triCount() == 28);

        auto loop = makePolygonBox();
        CHECK(cutQuadLoop(loop, {0, 1}, .25f) && loop.positions.size() == 12 && loop.faces.size() == 10);
        CHECK(buildPolygonMesh(loop, mesh) && mesh.triCount() == 20);
        for (size_t i = 8; i < loop.positions.size(); ++i) CHECK(std::fabs(loop.positions[i].x + .5f) < .001f);
        CHECK(subdividePolygons(loop) && buildPolygonMesh(loop, mesh) && mesh.triCount() == 80);

        PolygonMesh strip;
        strip.positions = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}, {.5f,2,0}};
        strip.faces = {{{0},{1},{2},{3}}, {{3},{2},{4}}};
        CHECK(cutQuadLoop(strip, {0, 1}, .5f) && strip.faces.size() == 3 && strip.faces[1].size() == 4);
        CHECK(buildPolygonMesh(strip, mesh) && mesh.triCount() == 6);
        CHECK(std::count_if(strip.faces[1].begin(), strip.faces[1].end(), [](auto corner) { return corner.vertex == 6; }) == 1);

        auto hole = makePolygonBox();
        CHECK(deletePolygonFaces(hole, {3}));
        CHECK(fillPolygonBoundary(hole, {{3,7}, {7,6}, {6,2}, {2,3}}));
        CHECK(buildPolygonMesh(hole, mesh) && mesh.triCount() == 12);
        auto board = makePolygonBox();
        projectPolygonUV(board, 0);
        board.positions.push_back(board.positions[0]);
        board.uv.push_back({.5f,.5f,0});
        board.faces[0][0] = {8, 8};
        CHECK(weldPolygonVertices(board, {0, 8}, .0001f) && board.positions.size() == 8);
        CHECK(board.faces[0][0].vertex == 0 && board.faces[0][0].uv == 8 &&
              buildPolygonMesh(board, mesh) && mesh.v.size() == 9);
    }
    printf("polygon boundary bridging and knife paths\n");
    {
        auto ends = makePolygonBox();
        ends.faces.resize(2);
        auto loops = expandPolygonBoundary(ends, {{0,1}, {4,5}});
        CHECK(loops.size() == 8);
        auto failed = ends;
        CHECK(!bridgePolygonBoundaries(failed, {{0,1}, {4,5}, {5,6}}, 2, 0) && failed.faces.size() == 2);
        Mesh mesh;
        auto twist = ends;
        CHECK(bridgePolygonBoundaries(twist, loops, 1, 1) && buildPolygonMesh(twist, mesh) && mesh.triCount() == 12);
        if (twist.faces.size() > 2) CHECK(twist.faces[2][3].vertex == 5);
        CHECK(bridgePolygonBoundaries(ends, loops, 3, 0) && ends.faces.size() == 14 && ends.positions.size() == 16);
        CHECK(buildPolygonMesh(ends, mesh) && mesh.triCount() == 28);
        CHECK(closedPolygonShell(ends));
        CHECK(ends.uv.size() == 20 && ends.uv[0].x == 0 && ends.uv[4].x == 1);

        PolygonMesh separate;
        separate.positions = {{0,0,0}, {1,0,0}, {0,1,0}, {2,0,0}, {3,0,0}, {2,1,0}};
        separate.faces = {{{0},{1},{2}}, {{3},{4},{5}}};
        CHECK(bridgePolygonBoundaries(separate, {{1,2}, {3,5}}, 2, 0) && buildPolygonMesh(separate, mesh) &&
              separate.positions.size() == 8 && mesh.triCount() == 6);
        auto single = makePolygonBox();
        single.faces.resize(1);
        CHECK(!bridgePolygonBoundaries(single, {{0,1}, {2,3}}, 1, 0) && single.faces.size() == 1);
    }
    {
        PolygonMesh strip;
        strip.positions = {{0,0,0}, {1,0,0}, {2,0,0}, {0,1,0}, {1,1,0}, {2,1,0}, {3,0,0}, {3,1,0}};
        strip.faces = {{{0},{1},{4},{3}}, {{1},{2},{5},{4}}, {{2},{6},{7},{5}}};
        projectPolygonUV(strip, 2);
        strip.uv.insert(strip.uv.end(), {{0,0,0}, {0,1,0}});
        strip.faces[1].front().uv = 8;
        strip.faces[1].back().uv = 9;
        strip.normals = {{0,0,1}, {0,.2f,1}};
        for (size_t i = 0; i < strip.faces.size(); ++i)
            for (auto& c : strip.faces[i]) c.normal = i == 0 ? 0 : 1;
        CHECK(cutPolygonPath(strip, {{{0,3}, .4f}, {{4,1}, .6f}, {{2,5}, .4f}}));
        CHECK(strip.positions.size() == 11 && strip.faces.size() == 5 && strip.faces[2].size() == 5);
        Mesh cut;
        CHECK(buildPolygonMesh(strip, cut) && cut.triCount() == 11);
        std::set<int> seamUVs, seamNormals;
        for (const auto& face : strip.faces)
            for (const auto& c : face) if (c.vertex == 9) {
                seamUVs.insert(c.uv);
                seamNormals.insert(c.normal);
            }
        CHECK(seamUVs.size() == 2 && !seamUVs.count(-1) && seamNormals.size() == 2 && !seamNormals.count(-1));

        auto single = makePolygonBox();
        single.faces.resize(1);
        auto diagonal = single;
        CHECK(cutPolygonPath(diagonal, {{{0,0}, 0}, {{2,2}, 0}}) && diagonal.faces.size() == 2 &&
              diagonal.positions.size() == 8 && buildPolygonMesh(diagonal, cut) && cut.triCount() == 2);
        CHECK(cutPolygonPath(single, {{{0,1}, .25f}, {{3,2}, .5f}, {{0,1}, .75f}}) &&
              single.positions.size() == 11 && single.faces.size() == 3 && buildPolygonMesh(single, cut));

        PolygonMesh concave;
        concave.positions = {{0,0,0}, {2,0,0}, {2,1,0}, {1,1,0}, {1,2,0}, {0,2,0}};
        concave.faces = {{{0},{1},{2},{3},{4},{5}}};
        CHECK(!cutPolygonPath(concave, {{{2,2}, 0}, {{4,4}, 0}}) &&
              concave.positions.size() == 6 && concave.faces.size() == 1);
    }
    printf("polygon bevels and symmetry\n");
    {
        using Edge = std::pair<uint32_t, uint32_t>;
        Mesh mesh;
        auto chamfer = makePolygonBox();
        CHECK(bevelPolygonEdges(chamfer, {{0,1}}, .25f, 1) && chamfer.faces.size() == 7 && chamfer.positions.size() == 10);
        CHECK(buildPolygonMesh(chamfer, mesh) && closedPolygonShell(chamfer));
        CHECK(mesh.triCount() == 16 && !mesh.uv.empty());
        auto rounded = makePolygonBox();
        CHECK(bevelPolygonEdges(rounded, {{0,1}}, .25f, 2) && rounded.faces.size() == 10 && rounded.positions.size() == 12);
        CHECK(buildPolygonMesh(rounded, mesh) && closedPolygonShell(rounded));
        float arc = -1 + .25f * (1 - std::sqrt(.5f));
        CHECK(std::any_of(rounded.positions.begin(), rounded.positions.end(), [&](V3 p) {
            return length(p - V3(-1, arc, arc)) < .001f;
        }));
        auto adjoining = makePolygonBox();
        CHECK(bevelPolygonEdges(adjoining, {{0,1}, {0,3}}, .12f, 3) && closedPolygonShell(adjoining) &&
              buildPolygonMesh(adjoining, mesh));
        auto cube = makePolygonBox();
        std::set<Edge> unique;
        for (const auto& face : cube.faces)
            for (size_t i = 0; i < face.size(); ++i)
                unique.insert(std::minmax(face[i].vertex, face[(i + 1) % face.size()].vertex));
        std::vector<Edge> all(unique.begin(), unique.end());
        CHECK(!bevelPolygonEdges(cube, all, 1.1f, 2) && cube.faces.size() == 6 && cube.positions.size() == 8);
        CHECK(bevelPolygonEdges(cube, all, .15f, 1) && cube.faces.size() == 26 && cube.positions.size() == 24);
        CHECK(buildPolygonMesh(cube, mesh) && closedPolygonShell(cube) && mesh.triCount() == 44);
        auto multi = makePolygonBox();
        projectPolygonUV(multi, 2);
        CHECK(bevelPolygonEdges(multi, {{0,1}, {0,3}, {0,4}}, .12f, 3));
        CHECK(buildPolygonMesh(multi, mesh) && closedPolygonShell(multi));
        CHECK(std::all_of(multi.faces.begin(), multi.faces.end(), [](const auto& face) {
            return std::all_of(face.begin(), face.end(), [](const auto& c) { return c.uv >= 0; });
        }));
        auto open = makePolygonBox();
        open.faces.resize(1);
        CHECK(!bevelPolygonEdges(open, {{0,1}}, .15f, 1) && open.faces.size() == 1);
    }
    {
        Mesh mesh;
        auto half = makePolygonBox();
        for (V3& p : half.positions) if (p.x > 0) p.x = .3502f;
        for (V3& p : half.positions) if (p.x < 0) p.x = -.65f;
        projectPolygonUV(half, 2);
        half.normals = {{1,0,0}};
        for (auto& corner : half.faces[4]) corner.normal = 0;
        CHECK(symmetrizePolygons(half, 0, .35f, false, .001f) && half.faces.size() == 10 && half.positions.size() == 12);
        CHECK(buildPolygonMesh(half, mesh) && closedPolygonShell(half));
        CHECK(std::count_if(half.positions.begin(), half.positions.end(), [](V3 p) { return p.x == .35f; }) == 4);
        CHECK(std::fabs(mesh.bmin.x + .65f) < .0001f && std::fabs(mesh.bmax.x - 1.35f) < .0001f);
        CHECK(half.faces[4].front().uv == half.faces[9].back().uv);
        CHECK(half.normals[half.faces[4].front().normal].x == 1 && half.normals[half.faces[9].back().normal].x == -1);
        auto pairs = matchPolygonSymmetry(half, 0, .35f, .0001f);
        for (uint32_t i = 0; i < pairs.size(); ++i)
            CHECK(pairs[i] != UINT32_MAX && pairs[pairs[i]] == i &&
                  std::fabs(half.positions[i].x + half.positions[pairs[i]].x - .7f) < .0001f);
        CHECK(!symmetrizePolygons(half, 2, 20, true, .0001f) && half.faces.size() == 10);
        PolygonMesh neighbors;
        neighbors.positions = {{-1,0,0}, {1,.0005f,0}, {1,2,0}, {0,3,0}};
        pairs = matchPolygonSymmetry(neighbors, 0, 0, .001f);
        CHECK(pairs == (std::vector<uint32_t>{1,0,UINT32_MAX,3}));
        neighbors.positions.push_back(neighbors.positions[1]);
        pairs = matchPolygonSymmetry(neighbors, 0, 0, .001f);
        CHECK(pairs[0] == UINT32_MAX && pairs[1] == UINT32_MAX && pairs[4] == UINT32_MAX);

        PolygonMesh islands;
        islands.positions = {{-2,-2,0}, {2,-2,0}, {2,2,0}, {1,2,0}, {1,0,0}, {-1,0,0}, {-1,2,0}, {-2,2,0}};
        islands.faces = {{{0},{1},{2},{3},{4},{5},{6},{7}}};
        CHECK(symmetrizePolygons(islands, 1, 1, true, 0) && buildPolygonMesh(islands, mesh));
        V3 normal;
        float distance = 0;
        CHECK(!mesh.intersect({0,1.25f,1}, {0,0,-1}, .001f, 5, distance, normal));
        CHECK(mesh.intersect({1.5f,.75f,1}, {0,0,-1}, .001f, 5, distance, normal));
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
