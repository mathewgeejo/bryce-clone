#define _CRT_SECURE_NO_WARNINGS

#include <algorithm>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <iomanip>
#include <functional>
#include <future>
#include <iterator>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "desktop.h"
#include "agent.h"
#include "render.h"
#include "scene.h"
#include "simulation.h"
#include "terrain.h"

namespace {

using namespace desktop;
using R = Rect;

const int TOP = 98, LEFT = 170, RIGHT = 236, STATUS = 24, TIMELINE = 34;
const int NANO_W = 150, NANO_H = 112, ICW = 48, ICH = 32, STW = 64, STH = 40;
const int VIEW_ID = 1000000;
constexpr char APP_NAME[] = "Brycescape";
const ObjType kCreate[] = {ObjType::Sphere, ObjType::Cube,    ObjType::Cylinder, ObjType::Cone,  ObjType::Pyramid, ObjType::Torus,
                           ObjType::Disc,   ObjType::Square,  ObjType::Stone,    ObjType::Terrain, ObjType::Lattice, ObjType::Ground,
                           ObjType::Water,  ObjType::Cloud,   ObjType::Light,    ObjType::Spotlight, ObjType::Parallel, ObjType::Metaball};

int gW = 1440, gH = 900;
uint32_t* gBits;
constexpr Font gFont = Font::Regular, gSmall = Font::Small, gBold = Font::Bold, gTitle = Font::Title;

struct Input {
    int mx = 0, my = 0, dx = 0, dy = 0;
    bool down = false, pressed = false, released = false, shift = false, ctrl = false, alt = false;
} gIn;
int gActive = 0, gIdc = 0;
bool gBlocked = false, gModal = false;
bool gResizeHovered = false;
std::vector<std::function<void()>> gDeferred;

Scene gScene;
std::vector<int> gSelSet;
int gSel = -1, gTab = 0, gView = 0, gTool = 0;
bool gShowRender = false;
enum ViewMode { Wire, DepthWire, BoundsOnly, Solid, Textured } gViewMode = Wire;
bool gDisplayPalette = false, gSafeFrame = false, gViewPreviewDirty = true;
bool gCameraPath = false;
float gPreviewScale = 0.45f;
std::string gHoverTip, gLastTip;
uint64_t gTipSince = 0;
Renderer gMainR, gNanoR, gLabR, gViewR;
enum Lab { LabNone, LabMat, LabSky, LabTerrain, LabTree, LabRender, LabTools, LabExamples, LabObject, LabAttributes, LabDxf, LabMotion, LabAssets, LabCameras, LabCommands, LabScatter, LabTransform, LabSimulation, LabMesh, LabCurves, LabRig } gLab = LabNone;
enum class MeshPick { Vertex, Edge, Face };
MeshPick gMeshPick = MeshPick::Face;
std::set<size_t> gMeshSelection;
int gMeshView = 0, gCurveChannel = 0, gCurveAxis = 0, gCurveKey = -1;
bool gCurveJointTrack = false;
int gCurveJoint = 0;
int gMeshTransform = 0;
float gMeshDistance = .25f;
V3 gMeshOffset;
std::string gMeshMessage;
int gRigMode = 0, gRigJoint = 0, gRigView = 0;
float gRigWeight = 1.f;
std::set<uint32_t> gRigVertices;
std::string gRigMessage;
Scene gLabBackup;
bool gLabChanged = false, gNanoDirty = true, gLabDirty = false, gMainWasBusy = false;
uint64_t gLastChange = 0, gRenderStart = 0;
std::string gStatus = "Open Examples or add objects from the Create palette, then press Render.", gPath;
V3 gOrthoC[4];
float gOrthoS[4] = {40, 40, 40, 40};
int gCounter[(int)ObjType::COUNT] = {}, gTreeCounter = 0;
R gVP{0, 0, 1, 1}, gListR{0, 0, 0, 0};
int gListScroll = 0;
std::string gFilter;
int gListDrag = -1, gListDragY = 0, gLastRow = -1;
uint64_t gLastRowTime = 0;
uint32_t gSeed = 100;
float gBrushSize = 0.06f, gBrushStr = 0.5f, gRough = 0.5f, gTerraces = 6;
BrushMode gBrush = BrushMode::Raise;
enum Drag { DragNone, DragObj, DragCam, DragSculpt, DragLightAim, DragScatter } gDrag = DragNone;
enum class Pivot { Median, Bounds, Active, World, Cursor };
enum class Snap { Off, Grid, Surface };
Pivot gPivotMode = Pivot::Median;
Snap gSnapMode = Snap::Off;
V3 gCursor;
bool gCursorVisible = false, gAutoAxis = false;
int gTransformAxis = -1, gMirrorAxis = 0;
float gGridStep = 1.f, gAngleStep = 15.f;
struct TransformObject {
    int index;
    V3 position, rotation, scale, world;
    M3 worldToLocal;
};
struct TransformDrag {
    std::vector<TransformObject> objects;
    std::unique_ptr<Scene> surfaces;
    V3 pivot, bottom;
    ViewBasis view;
    float dx = 0, dy = 0, unitsPerPixel = 1;
    int autoAxis = -1;
} gTransform;
bool gScatter = false, gScatterAlign = true;
bool gScatterPalette = false, gScatterErase = false;
int gScatterSeed = 1, gScatterDab = 0, gScatterX = 0, gScatterY = 0;
float gScatterRadius = 46, gScatterDensity = 8, gScatterMin = .6f, gScatterMax = 1.4f, gScatterYaw = 360;
const int TOOL_SCULPT = 3;
struct SculptHit { int obj = -1; float u = 0, v = 0; } gSculptHover, gSculpt;
uint64_t gSculptLast = 0;
bool gRDrag = false, gMDrag = false;
std::vector<uint32_t> gIcons[(int)ObjType::COUNT], gTreeIcon;
std::vector<std::vector<uint32_t>> gSkyThumbs;
struct History { Scene scene; std::string action; };
std::vector<History> gUndo, gRedo;
std::string gPreStatus;
Scene gPre;
bool gHavePre = false, gChanged = false, gNoPush = false, gDirty = false;
int gEditObj = -1;
std::string gEditBuf;
int gQuickCat = 0, gSkyCat = 0, gMatCat = 0, gLabSkyCat = 0;
bool gMatLayers = false;
int gLayerIndex = 0;
int gTerrainBakeLayer = -1;
Library gLib;
std::vector<Library> gLibraryUndo, gLibraryRedo;
std::string gLibPath;
uint64_t gAutosaveTime = 0;
std::string gDxfPath;
float gDxfScale = 1;
bool gDxfZUp = true, gDxfLayers = true;
bool gAppearanceTrack = false;
bool gTerrainTrack = false;
int gMotionKey = 0;
int gTerrainSeed = 77, gTerrainOctaves = 7;
float gTerrainFrequency = 4, gErosionDensity = 1;
bool gRegionTool = false, gRegionMask = false;
V3 gRegionStart, gRegionEnd;
TerrainPatch gTerrainClipboard;
std::shared_ptr<const Image> gTerrainBlendMask;
float gTerrainBlend = 1;
std::vector<Heightfield> gTerrainUndo, gTerrainRedo;
BoolMode gNestedMode = BoolMode::Positive;
struct AssetTile { std::string path, name; std::vector<uint32_t> thumbnail; };
std::vector<AssetTile> gAssets;
int gAssetPage = 0, gAssetSelection = -1;
TreeParams gTreeP;
bool gPlaying = false, gAutoKey = false, gAnimRender = false;
bool gLiquidSimulation = false;
bool gSimulationAdvanced = false, gSimulationStartHere = true;
RigidSettings gRigidSettings;
LiquidSettings gLiquidSettings;
RigidPreset gRigidPreset = RigidPreset::Balanced;
GravityPreset gSimulationGravity[2] = {GravityPreset::Earth, GravityPreset::Earth};
std::string gSimulationError;
struct SimulationBake {
    Scene scene;
    std::string error;
    int particles = 0, firstParticle = -1, last = 0;
    bool ok = false, play = false;
};
std::future<SimulationBake> gSimulationBake;
int gAnimFrame = -1;
uint64_t gPlayStart = 0;
int gPlayStartFrame = 0, gPlayEndFrame = -1;
std::string gAnimBase, gAnimExtension;
struct RenderJob { std::string scene, output; };
std::vector<RenderJob> gQueue;
int gQueueIndex = -1;
bool gResumeAllowed = false;
float gRepCount = 3, gRepScale = 1, gRandPos = 4, gRandRot = 45, gRandSize = 0.3f;
V3 gRepOff{3, 0, 0}, gRepRot{0, 0, 0};

// ---------------------------------------------------------------- drawing primitives

constexpr Color TXT = rgb(232, 232, 236), TXT_DIM = rgb(190, 194, 204);

uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void vgrad(R r, Color a, Color b) {
    beginPixelWrite();
    int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, gW), y0 = std::max(r.y, 0), y1 = std::min(r.y + r.h, gH);
    for (int y = y0; y < y1; y++) {
        float t = (y - r.y) / (float)std::max(1, r.h - 1);
        uint32_t v = ((uint32_t)lerpf(red(a), red(b), t) << 16) | ((uint32_t)lerpf(green(a), green(b), t) << 8) |
                     (uint32_t)lerpf(blue(a), blue(b), t);
        std::fill(gBits + (size_t)y * gW + x0, gBits + (size_t)y * gW + std::max(x0, x1), v);
    }
}

void bevel(R r, bool sunken) {
    Color hi = rgb(160, 160, 168), lo = rgb(34, 34, 38);
    if (sunken) std::swap(hi, lo);
    line(r.x, r.y + r.h - 1, r.x, r.y, hi);
    line(r.x, r.y, r.x + r.w - 1, r.y, hi);
    line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h - 1, lo);
    line(r.x + r.w - 1, r.y + r.h - 1, r.x - 1, r.y + r.h - 1, lo);
}

void label(int x, int y, const char* s, int w = 400) { textIn({x, y, w, 16}, s, TXT_DIM, gBold, TextLeft | TextMiddle); }
void note(R r, const char* s) { textIn(r, s, TXT_DIM, gSmall, TextLeft | TextWrap); }

void blit(R d, const uint32_t* src, int sw, int sh) {
    if (d.w <= 0 || d.h <= 0 || sw <= 0 || sh <= 0) return;
    beginPixelWrite();
    int x0 = std::max(d.x, 0), x1 = std::min(d.x + d.w, gW), y0 = std::max(d.y, 0), y1 = std::min(d.y + d.h, gH);
    for (int y = y0; y < y1; y++) {
        const uint32_t* row = src + (size_t)((y - d.y) * sh / d.h) * sw;
        uint32_t* dst = gBits + (size_t)y * gW;
        for (int x = x0; x < x1; x++) dst[x] = row[(x - d.x) * sw / d.w];
    }
}

void blitFit(const Renderer& r, R area) {
    static std::vector<uint32_t> tmp;
    if (!r.hasImage()) return;
    r.copyTo(tmp);
    float s = std::min(area.w / (float)r.width(), area.h / (float)r.height());
    int w = (int)(r.width() * s), h = (int)(r.height() * s);
    blit({area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h}, tmp.data(), r.width(), r.height());
}

void shadedBall(int cx, int cy, int r, V3 base, bool hot) {
    beginPixelWrite();
    V3 L = normalize(V3(-0.5f, 0.6f, 0.6f));
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            float d2 = (float)(x * x + y * y) / (r * r);
            int px = cx + x, py = cy + y;
            if (d2 > 1 || px < 0 || py < 0 || px >= gW || py >= gH) continue;
            V3 n(x / (float)r, -y / (float)r, std::sqrt(1 - d2));
            float d = std::max(0.f, dot(n, L)), s = std::pow(std::max(0.f, dot(n * (2 * dot(n, L)) - L, V3(0, 0, 1))), 24.f);
            V3 c = base * ((0.25f + 0.75f * d) * (hot ? 1.2f : 1.f)) + V3(s * 0.8f);
            gBits[(size_t)py * gW + px] = ((uint32_t)(clampf(c.x, 0, 1) * 255) << 16) | ((uint32_t)(clampf(c.y, 0, 1) * 255) << 8) | (uint32_t)(clampf(c.z, 0, 1) * 255);
        }
}

void dimAll() {
    beginPixelWrite();
    for (size_t i = 0, n = (size_t)gW * gH; i < n; i++) gBits[i] = (gBits[i] >> 1) & 0x7F7F7F;
}

// ---------------------------------------------------------------- immediate-mode widgets

void defer(std::function<void()> f) { gDeferred.push_back(std::move(f)); }
bool hov(R r) { return !gBlocked && r.in(gIn.mx, gIn.my); }
int newId() { return ++gIdc; }
void press(R r, int id) {
    if (hov(r) && gIn.pressed && gActive == 0) gActive = id;
}
bool clicked(R r, int id) { return gActive == id && gIn.released && hov(r); }

bool button(R r, const char* text, bool on = false, Font f = gFont) {
    int id = newId();
    press(r, id);
    bool h = hov(r), act = gActive == id && h;
    if (h) {
        static const std::map<std::string, const char*> tips = {
            {"Group", "Ctrl+G groups the selection. Alt-click selects an individual group member."},
            {"Ungroup", "Ctrl+U ungroups the selection."},
            {"Wire", "Wireframe displays object edges. Try Depth Wire for distant objects."},
            {"Move", "G selects Move. Press X, Y, or Z to constrain the drag."},
            {"Rotate", "R selects Rotate. Press X, Y, or Z for single-axis rotation."},
            {"Resize", "S selects Scale. Press X, Y, or Z for single-axis scaling."},
            {"Sculpt", "[ and ] resize the brush. Ctrl lowers height and Shift smooths."},
            {"Key", "Ctrl+K records a transform key for the selection or camera."},
            {"Frame Sel", "F frames the selection. With no selection, it frames the scene."},
            {"Motion", "Open the motion editor for transform, appearance and camera keys."},
            {"Commands", "F1 opens the command palette and shortcut list."},
        };
        auto tip = tips.find(text);
        if (tip != tips.end()) gHoverTip = tip->second;
    }
    if (on) vgrad(r, rgb(126, 142, 182), rgb(70, 82, 120));
    else if (act) vgrad(r, rgb(58, 58, 64), rgb(92, 92, 100));
    else if (h) vgrad(r, rgb(140, 140, 148), rgb(96, 96, 104));
    else vgrad(r, rgb(114, 114, 120), rgb(78, 78, 84));
    bevel(r, act);
    textIn(r, text, on ? rgb(255, 255, 255) : TXT, f);
    return clicked(r, id);
}

bool toggle(R r, const char* text, bool& v) {
    if (!button(r, text, v)) return false;
    v = !v;
    return true;
}

bool iconButton(R r, const std::vector<uint32_t>& img, int iw, int ih, const char* text, bool on = false, const char* glyph = nullptr) {
    int id = newId();
    press(r, id);
    bool h = hov(r), act = gActive == id && h;
    vgrad(r, on ? rgb(126, 142, 182) : (h ? rgb(134, 134, 142) : rgb(102, 102, 108)), on ? rgb(70, 82, 120) : rgb(66, 66, 72));
    bevel(r, act);
    if (!img.empty()) {
        int w = std::min(iw, r.w - 4), h = std::max(1, ih * w / iw);
        blit({r.x + (r.w - w) / 2, r.y + 4 + (ih - h) / 2, w, h}, img.data(), iw, ih);
    }
    else if (glyph) textIn({r.x, r.y + 4, r.w, ih}, glyph, rgb(255, 230, 150), gTitle);
    textIn({r.x, r.y + r.h - 16, r.w, 14}, text, TXT, gSmall);
    return clicked(r, id);
}

bool slider(R r, const char* text, float& v, float lo, float hi, const char* fmt = "%.2f", bool logScale = false) {
    int id = newId();
    press(r, id);
    auto toT = [&](float x) { return logScale ? std::log(x / lo) / std::log(hi / lo) : (x - lo) / (hi - lo); };
    bool ch = false;
    if (gActive == id && gIn.down) {
        float t = clampf((gIn.mx - r.x - 2) / (float)(r.w - 4), 0, 1);
        float nv = logScale ? lo * std::pow(hi / lo, t) : lo + t * (hi - lo);
        if (nv != v) { v = nv; ch = true; }
    }
    fill(r, rgb(44, 44, 50));
    bevel(r, true);
    int fw = (int)(clampf(toT(v), 0, 1) * (r.w - 4));
    if (fw > 0) vgrad({r.x + 2, r.y + 2, fw, r.h - 4}, hov(r) || gActive == id ? rgb(130, 150, 200) : rgb(108, 126, 172), rgb(64, 78, 118));
    char buf[64];
    snprintf(buf, sizeof buf, fmt, v);
    textIn({r.x + 6, r.y, r.w - 12, r.h}, text, TXT, gFont, TextLeft | TextMiddle);
    textIn({r.x + 6, r.y, r.w - 12, r.h}, buf, TXT, gFont, TextRight | TextMiddle);
    return ch;
}

bool sliderI(R r, const char* text, int& v, int lo, int hi) {
    float f = (float)v;
    if (!slider(r, text, f, (float)lo, (float)hi, "%.0f")) return false;
    int nv = (int)std::lround(f);
    if (nv == v) return false;
    v = nv;
    return true;
}

bool dragF(R r, const char* text, float& v, float speed, float minV = -1e9f, float maxV = 1e9f, const char* fmt = nullptr) {
    int id = newId();
    press(r, id);
    bool ch = false;
    if (gActive == id && gIn.down && gIn.dx) { v = clampf(v + gIn.dx * speed * (gIn.shift ? 0.1f : 1.f), minV, maxV); ch = true; }
    fill(r, gActive == id ? rgb(70, 82, 120) : (hov(r) ? rgb(60, 60, 68) : rgb(46, 46, 52)));
    bevel(r, true);
    char buf[32];
    snprintf(buf, sizeof buf, fmt ? fmt : (std::fabs(speed) >= 1 ? "%.0f" : "%.2f"), v);
    textIn({r.x + 4, r.y, r.w - 8, r.h}, text, TXT_DIM, gSmall, TextLeft | TextMiddle);
    textIn({r.x + 4, r.y, r.w - 8, r.h}, buf, TXT, gSmall, TextRight | TextMiddle);
    if (hov(r)) gResizeHovered = true;
    return ch;
}

bool dragI(R r, const char* text, int& v, float speed, int lo, int hi) {
    static std::map<int, float> acc;
    int id = gIdc + 1;
    float& f = acc[id];
    if (gActive != id) f = (float)v;
    if (!dragF(r, text, f, speed, (float)lo, (float)hi, "%.0f")) return false;
    int nv = (int)std::lround(f);
    if (nv == v) return false;
    v = nv;
    return true;
}

void sceneChanged() {
    gResumeAllowed = false;
    gViewPreviewDirty = true;
    gScene.updateAll();
    gNanoDirty = true;
    gLastChange = nowMs();
    gChanged = true;
    if (gLab != LabNone) { gLabDirty = true; gLabChanged = true; }
}

void finishDialog() {
    gModal = false;
    gIn.down = gRDrag = gMDrag = false;
    gActive = 0;
    capturePointer(false);
}

bool pickColor(V3& c) {
    Color color = rgb((int)(clampf(c.x, 0, 1) * 255), (int)(clampf(c.y, 0, 1) * 255), (int)(clampf(c.z, 0, 1) * 255));
    gModal = true;
    bool ok = chooseColor(color);
    finishDialog();
    if (ok) c = V3(red(color) / 255.f, green(color) / 255.f, blue(color) / 255.f);
    return ok;
}

void swatch(R r, const char* text, V3& c) {
    int id = newId();
    press(r, id);
    bool h = hov(r);
    vgrad(r, h ? rgb(130, 130, 138) : rgb(108, 108, 114), rgb(74, 74, 80));
    bevel(r, gActive == id && h);
    textIn({r.x + 6, r.y, r.w - 60, r.h}, text, TXT, gFont, TextLeft | TextMiddle);
    R sw{r.x + r.w - 52, r.y + 3, 46, r.h - 6};
    fill(sw, rgb((int)(clampf(c.x, 0, 1) * 255), (int)(clampf(c.y, 0, 1) * 255), (int)(clampf(c.z, 0, 1) * 255)));
    bevel(sw, true);
    if (clicked(r, id)) {
        V3* pc = &c;
        defer([pc] { if (pickColor(*pc)) sceneChanged(); });
    }
}

// ---------------------------------------------------------------- selection & undo

bool validSel() { return gSel >= 0 && gSel < (int)gScene.objs.size(); }
bool isSel(int i) { return std::find(gSelSet.begin(), gSelSet.end(), i) != gSelSet.end(); }
void clearSel() { gSelSet.clear(); gSel = -1; gEditObj = -1; }

std::vector<int> groupOf(int i, bool single) {
    int g = gScene.objs[i].group;
    if (g == 0 || single) return {i};
    std::vector<int> r;
    for (int k = 0; k < (int)gScene.objs.size(); k++)
        if (gScene.objs[k].group == g) r.push_back(k);
    return r;
}

void selectObj(int i, bool add, bool single = false) {
    if (i < 0) {
        if (!add) clearSel();
        return;
    }
    auto g = groupOf(i, single);
    if (add && isSel(i)) {
        for (int k : g) gSelSet.erase(std::remove(gSelSet.begin(), gSelSet.end(), k), gSelSet.end());
        gSel = gSelSet.empty() ? -1 : gSelSet.back();
        return;
    }
    if (!add) gSelSet.clear();
    for (int k : g)
        if (!isSel(k)) gSelSet.push_back(k);
    gSel = i;
}

static void trimHistory(std::vector<History>& history) {
    size_t bytes = 0, first = history.size();
    while (first > 0 && history.size() - first < 40) {
        std::set<const Heightfield*> counted;
        for (const auto& object : history[first - 1].scene.objs) {
            if (object.hf && counted.insert(object.hf.get()).second)
                bytes += object.hf->h.size() * sizeof(float) + object.hf->n.size() * sizeof(V3);
        }
        if (bytes > 256ull * 1024 * 1024 && first < history.size()) break;
        --first;
    }
    history.erase(history.begin(), history.begin() + first);
}

void pushUndo(Scene s, std::string action = "Edit scene") {
    gUndo.push_back({std::move(s), std::move(action)});
    gDirty = true;
    trimHistory(gUndo);
    gRedo.clear();
}

void beginInteraction() {
    if (gHavePre || gLab != LabNone) return;
    gPre = gScene.snapshot();
    gPreStatus = gStatus;
    gHavePre = true;
    gChanged = false;
}

void autoKey();

void endInteraction() {
    if (gHavePre && gChanged && !gNoPush) {
        std::string action = gStatus != gPreStatus ? gStatus : "Edit scene";
        if (gDrag == DragSculpt && gActive == 0 && action == "Edit scene") action = "Terrain stroke";
        else if (gDrag == DragObj && action == "Edit scene") action = gTool == 0 ? "Move objects" : (gTool == 1 ? "Rotate objects" : "Scale objects");
        pushUndo(std::move(gPre), std::move(action));
        if (gAutoKey) autoKey();
    }
    gHavePre = gNoPush = false;
}

void sceneReplaced() {
    gMainR.cancel();
    clearSel();
    gScatter = false;
    gShowRender = gPlaying = false;
    for (int& c : gCounter) c = 0;
    for (auto& o : gScene.objs) gCounter[(int)o.type]++;
    gTreeCounter = gScene.maxGroup();
    sceneChanged();
    gDirty = false;
}

void undoRedo(bool redo) {
    auto& from = redo ? gRedo : gUndo;
    auto& to = redo ? gUndo : gRedo;
    if (from.empty()) { gStatus = redo ? "Nothing to redo." : "Nothing to undo."; return; }
    auto action = from.back().action;
    to.push_back({gScene.snapshot(), action});
    trimHistory(to);
    gScene = std::move(from.back().scene);
    from.pop_back();
    sceneReplaced();
    gNoPush = gDirty = true;
    gStatus = (redo ? "Redo " : "Undo ") + action;
}

void commitRename() {
    if (gEditObj == -2) { gFilter = gEditBuf; gListScroll = 0; }
    if (gEditObj == -3 && gLab == LabCameras && gScene.activeCamera >= 0 && !gEditBuf.empty()) {
        auto& name = gScene.cameras[gScene.activeCamera].name;
        if (name != gEditBuf) { name = gEditBuf; sceneChanged(); }
    }
    if (gEditObj == -4 && gLab == LabRig && validSel() && !gEditBuf.empty()) {
        auto& object = gScene.objs[gSel];
        if (object.rig && gRigJoint >= 0 && gRigJoint < (int)object.rig->joints.size() &&
            object.rig->joints[gRigJoint].name != gEditBuf) {
            auto rig = std::make_shared<Rig>(*object.rig);
            rig->joints[gRigJoint].name = gEditBuf;
            object.rig = std::move(rig);
            sceneChanged();
        }
    }
    if (gEditObj >= 0 && gEditObj < (int)gScene.objs.size() && !gEditBuf.empty() && gScene.objs[gEditObj].name != gEditBuf) {
        if (gLab == LabNone && !gHavePre) pushUndo(gScene.snapshot(), "Rename object");
        gScene.objs[gEditObj].name = gEditBuf;
        gDirty = true;
    }
    gEditObj = -1;
}

// ---------------------------------------------------------------- views & scene actions

ViewBasis curBasis() { return gView == 0 ? cameraBasis(gScene.viewCamera()) : orthoBasis(gView, gOrthoC[gView], gOrthoS[gView]); }

void orbitCam(float dx, float dy) {
    gScene.cam.yaw -= dx * 0.4f;
    gScene.cam.pitch = clampf(gScene.cam.pitch + dy * 0.3f, -89.f, 89.f);
    sceneChanged();
}

void panView(float dx, float dy) {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f;
    if (gView == 0) gScene.cam.target += (vb.r * -dx + vb.u * dy) * ((vb.ortho ? 1.f : gScene.cam.dist) * vb.k / hh);
    else gOrthoC[gView] += (vb.r * -dx + vb.u * dy) * (vb.k / hh);
    sceneChanged();
}

void zoomView(float steps) {
    float f = std::pow(0.88f, steps);
    if (gView == 0 && gScene.cam.orthographic) gScene.cam.orthoScale = clampf(gScene.cam.orthoScale * f, 0.1f, 5000.f);
    else if (gView == 0) gScene.cam.dist = clampf(gScene.cam.dist * f, 1.f, 5000.f);
    else gOrthoS[gView] = clampf(gOrthoS[gView] * f, 1.f, 5000.f);
    sceneChanged();
}

bool sceneBounds(bool selected, V3& lo, V3& hi) {
    bool any = false;
    for (int i = 0; i < (int)gScene.objs.size(); i++) {
        const Object& o = gScene.objs[i];
        V3 a, b;
        if (o.hidden || (selected && !isSel(i)) || !worldBounds(o, a, b)) continue;
        if (!any) { lo = a; hi = b; any = true; continue; }
        lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
        hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
    }
    return any;
}

V3 axisVector(int axis) { return {axis == 0 ? 1.f : 0.f, axis == 1 ? 1.f : 0.f, axis == 2 ? 1.f : 0.f}; }

void expandBounds(V3 low, V3 high, V3& totalLow, V3& totalHigh) {
    totalLow = {std::min(totalLow.x, low.x), std::min(totalLow.y, low.y), std::min(totalLow.z, low.z)};
    totalHigh = {std::max(totalHigh.x, high.x), std::max(totalHigh.y, high.y), std::max(totalHigh.z, high.z)};
}

std::vector<int> transformRoots() {
    std::vector<int> roots;
    for (int i : gSelSet) {
        const auto& obj = gScene.objs[i];
        if (obj.locked || obj.hidden) continue;
        int parent = obj.parent;
        while (parent >= 0 && (!isSel(parent) || gScene.objs[parent].locked || gScene.objs[parent].hidden))
            parent = gScene.objs[parent].parent;
        if (parent < 0) roots.push_back(i);
    }
    return roots;
}

V3 selectionPivot(Pivot mode, const std::vector<int>& roots) {
    if (mode == Pivot::World) return {};
    if (mode == Pivot::Cursor) return gCursor;
    if (mode == Pivot::Active && validSel()) return gScene.objs[gSel].worldPos;
    if (mode == Pivot::Bounds) {
        V3 low(1e30f), high(-1e30f);
        for (int i = 0; i < (int)gScene.objs.size(); ++i) {
            const auto& obj = gScene.objs[i];
            if (obj.hidden) continue;
            int ancestor = i;
            while (ancestor >= 0 && std::find(roots.begin(), roots.end(), ancestor) == roots.end())
                ancestor = gScene.objs[ancestor].parent;
            V3 a, b;
            if (ancestor >= 0 && worldBounds(obj, a, b)) expandBounds(a, b, low, high);
        }
        if (low.x <= high.x) return (low + high) * .5f;
    }
    V3 p;
    for (int i : roots) p += gScene.objs[i].worldPos;
    return roots.empty() ? p : p / (float)roots.size();
}

float snapValue(float value, float step) { return std::round(value / step) * step; }

V3 snapPoint(V3 p, float step) {
    return {snapValue(p.x, step), snapValue(p.y, step), snapValue(p.z, step)};
}

enum class Placement { Grid, Origin, Cursor };

void placeSelection(Placement target) {
    auto roots = transformRoots();
    if (roots.empty()) { gStatus = "Select an unlocked object to place."; return; }
    Pivot mode = gPivotMode == Pivot::World || gPivotMode == Pivot::Cursor ? Pivot::Median : gPivotMode;
    V3 center = selectionPivot(target == Placement::Grid ? mode : Pivot::Bounds, roots);
    V3 destination;
    if (target == Placement::Grid) destination = snapPoint(center, gGridStep);
    else if (target == Placement::Cursor) destination = gCursor;
    V3 delta = destination - center;
    if (length(delta) < 1e-6f) return;
    for (int i : roots) gScene.objs[i].moveWorld(delta);
    sceneChanged();
    if (target == Placement::Grid) gStatus = "Snapped the selection center to the grid.";
    else if (target == Placement::Cursor) gStatus = "Centered the selection at the 3D cursor.";
    else gStatus = "Centered the selection at the world origin.";
}

void mirrorSelection() {
    auto roots = transformRoots();
    int start = (int)gScene.objs.size();
    int count = gScene.duplicateMirrored(gSelSet, gMirrorAxis, selectionPivot(gPivotMode, roots));
    if (!count) { gStatus = "Select an unlocked object to mirror."; return; }
    gSelSet.clear();
    for (int i = start; i < start + count; ++i) gSelSet.push_back(i);
    gSel = start;
    sceneChanged();
    gStatus = "Created " + std::to_string(count) + " mirrored objects.";
}

void frameView(bool selection) {
    V3 lo, hi;
    if (!sceneBounds(selection, lo, hi)) {
        if (!selection || !validSel() || gScene.objs[gSel].hidden) {
            gStatus = selection ? "Select a visible object to frame it." : "There are no finite objects to frame.";
            return;
        }
        lo = hi = gScene.objs[gSel].worldPos;
    }
    V3 center = (lo + hi) * 0.5f;
    float radius = std::max(0.75f, length(hi - center));
    float aspect = std::min(1.f, (float)gVP.w / gVP.h);
    if (gView == 0) {
        gScene.cam.target = center;
        gScene.cam.lookAt = -1;
        float fov = std::atan(std::tan(rad(gScene.cam.fov * 0.5f)) * aspect);
        gScene.cam.dist = clampf(radius * 1.18f / std::sin(fov), 1.f, 5000.f);
        if (gScene.cam.orthographic) gScene.cam.orthoScale = clampf(radius * 1.18f / aspect, 0.1f, 5000.f);
        sceneChanged();
    } else {
        gOrthoC[gView] = center;
        gOrthoS[gView] = clampf(radius * 1.18f / aspect, 1.f, 5000.f);
        gViewPreviewDirty = true;
    }
    gShowRender = false;
}

void addObject(Object o) {
    o.update();
    gScene.objs.push_back(std::move(o));
    selectObj((int)gScene.objs.size() - 1, false);
    gShowRender = false;
    sceneChanged();
}

void createObject(ObjType t) {
    Object o = makeObject(t, ++gCounter[(int)t]);
    if (!isInfinite(t)) { o.pos.x += gScene.cam.target.x; o.pos.z += gScene.cam.target.z; }
    gStatus = "Created " + o.name + ".";
    addObject(o);
}

void openLab(Lab l);

void createTree() {
    TreeParams p;
    p.seed = ++gSeed;
    addTree(gScene, p, {gScene.cam.target.x, 0, gScene.cam.target.z}, 6, findMaterial("Bark"), findMaterial("Leaves"), ++gTreeCounter);
    selectObj((int)gScene.objs.size() - 1, false);
    gShowRender = false;
    sceneChanged();
    gStatus = "Created a tree. Shape it in the Tree Lab.";
    openLab(LabTree);
}

std::vector<int> sortedSel() {
    std::vector<int> s = gSelSet;
    std::sort(s.begin(), s.end());
    s.erase(std::remove_if(s.begin(), s.end(), [](int i) { return i < 0 || i >= (int)gScene.objs.size(); }), s.end());
    return s;
}

void actDelete() {
    auto s = sortedSel();
    if (s.empty()) return;
    std::vector<int> remaining;
    for (int i = 0; i < (int)gScene.objs.size(); ++i)
        if (!std::binary_search(s.begin(), s.end(), i)) remaining.push_back(i);
    gScene = gScene.subset(remaining);
    clearSel();
    sceneChanged();
    gStatus = "Deleted " + std::to_string(s.size()) + " object(s).";
}

void replicate(int times, V3 off, V3 rot, float scl, bool linked = false) {
    auto s = sortedSel();
    if (s.empty()) return;
    std::vector<int> made;
    for (int k = 1; k <= times; ++k) {
        Scene copy = gScene.subset(s);
        for (Object& o : copy.objs) {
            if (o.hf && !linked) o.hf = std::make_shared<Heightfield>(*o.hf);
            o.name += " copy";
            if (o.parent < 0) {
                o.moveWorld(off * (float)k);
                o.rot += rot * (float)k;
                o.scl *= std::pow(scl, (float)k);
            }
            o.keys.clear();
            o.appearances.clear();
        }
        int begin = (int)gScene.objs.size(), end = begin + (int)copy.objs.size();
        gScene.append(std::move(copy));
        for (int i = begin; i < end; ++i) made.push_back(i);
    }
    gSelSet = made;
    gSel = made.back();
    sceneChanged();
    gStatus = "Created " + std::to_string(made.size()) + " copies.";
}

void actDuplicate() { replicate(1, V3(2, 0, 2), V3(0.f), 1); }

void actLinked() { replicate(1, V3(2, 0, 2), V3(0.f), 1, true); }

void actSelectMatching(int field) {
    if (!validSel()) return;
    const auto& selected = gScene.objs[gSel];
    gSelSet.clear();
    for (int i = 0; i < (int)gScene.objs.size(); ++i) {
        const auto& o = gScene.objs[i];
        if ((field == 0 && o.type == selected.type) || (field == 1 && o.mat.name == selected.mat.name) ||
            (field == 2 && selected.group > 0 && o.group == selected.group)) gSelSet.push_back(i);
    }
    if (gSelSet.empty()) gSelSet.push_back(gSel);
    gStatus = "Selected " + std::to_string(gSelSet.size()) + " matching objects.";
}

void actIsolate(bool showAll) {
    if (!showAll && !validSel()) return;
    for (int i = 0; i < (int)gScene.objs.size(); ++i) gScene.objs[i].hidden = !showAll && !isSel(i);
    sceneChanged();
    gStatus = showAll ? "Show all objects" : "Isolate selection";
}

void reorderSceneObject(int from, int target) {
    if (from < 0 || target < 0 || from == target) return;
    int n = (int)gScene.objs.size();
    std::vector<int> order;
    for (int i = 0; i < n; ++i) if (i != from) order.push_back(i);
    order.insert(order.begin() + target, from);
    std::vector<int> remap(n);
    for (int i = 0; i < n; ++i) remap[order[i]] = i;
    gScene = gScene.subset(order);
    for (int& i : gSelSet) i = remap[i];
    if (gSel >= 0) gSel = remap[gSel];
    sceneChanged();
    gStatus = "Reorder scene object";
}

void actLand() {
    auto s = sortedSel();
    if (s.empty()) return;
    Scene tmp = gScene;
    for (int i : s) tmp.objs[i].hidden = true;
    tmp.updateAll();
    std::map<int, std::vector<int>> units;
    for (int i : s) {
        const Object& o = gScene.objs[i];
        if (isInfinite(o.type) || isLight(o.type) || o.locked) continue;
        units[o.group > 0 ? -o.group : i].push_back(i);
    }
    for (auto& u : units) {
        V3 lo(1e30f), hi(-1e30f);
        for (int i : u.second) {
            V3 a, b;
            worldBounds(gScene.objs[i], a, b);
            lo = V3(std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z));
            hi = V3(std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z));
        }
        Hit h;
        V3 c = (lo + hi) * 0.5f;
        float ground = intersectScene(tmp, {V3(c.x, hi.y, c.z), V3(0, -1, 0)}, 1e30f, h) ? h.p.y : 0.f;
        for (int i : u.second) gScene.objs[i].moveWorld({0, ground - lo.y, 0});
    }
    sceneChanged();
    gStatus = "Landed the selection on the surface below.";
}

void actGroup() {
    if (gSelSet.size() < 2) { gStatus = "Select two or more objects (Shift+click) to group them."; return; }
    int g = gScene.maxGroup() + 1;
    for (int i : gSelSet) gScene.objs[i].group = g;
    sceneChanged();
    gStatus = "Grouped " + std::to_string(gSelSet.size()) + " objects as group " + std::to_string(g) + ".";
}

void actUngroup() {
    for (int i : gSelSet) gScene.objs[i].group = 0;
    sceneChanged();
    gStatus = "Ungrouped the selection.";
}

void actParent(bool detach) {
    if (!validSel() || (!detach && gSelSet.size() < 2)) {
        gStatus = "Select the children, then Shift-click the parent last.";
        return;
    }
    int changed = 0;
    for (int i : gSelSet) {
        if ((!detach && i == gSel) || gScene.objs[i].locked) continue;
        int parent = detach ? -1 : gSel;
        if (gScene.objs[i].parent != parent && gScene.setParent(i, parent)) ++changed;
    }
    if (changed) sceneChanged();
    gStatus = changed ? std::to_string(changed) + (detach ? " object(s) detached." : " object(s) parented.") :
                         "No hierarchy links changed. Check locks or circular links.";
}

void actNestBoolean(bool detach) {
    int active = validSel() ? gScene.objs[gSel].group : 0;
    if (!active) { gStatus = "Choose a Boolean group as the active selection."; return; }
    int count = 0;
    if (detach) {
        count = gScene.setBooleanParent(active, 0, gNestedMode);
    } else {
        std::vector<int> selected;
        for (int i : gSelSet) {
            int group = gScene.objs[i].group;
            if (group > 0 && group != active && std::find(selected.begin(), selected.end(), group) == selected.end()) selected.push_back(group);
        }
        for (int group : selected) if (gScene.setBooleanParent(group, active, gNestedMode)) ++count;
    }
    if (count) sceneChanged();
    gStatus = count ? (detach ? "Detached Boolean group." : "Nested Boolean groups under the active group.") :
                      "Select another group and avoid circular links.";
}

void actBakeGeometry() {
    auto parts = meshSelection(gScene, gSelSet, 48);
    if (parts.empty()) { gStatus = "No surface to bake. A Boolean group needs a positive volume and overlapping intersection parts."; return; }
    Scene baked;
    for (auto& part : parts) {
        part.mesh.build();
        Object o = makeObject(ObjType::Mesh, ++gCounter[(int)ObjType::Mesh]);
        o.name = part.name + " (baked)";
        if (validSel()) o.mat = gScene.objs[gSel].mat;
        o.mesh = std::make_shared<Mesh>(std::move(part.mesh));
        baked.objs.push_back(std::move(o));
    }
    int first = (int)gScene.objs.size();
    gScene.append(std::move(baked));
    gSelSet.clear();
    for (int i = first; i < (int)gScene.objs.size(); ++i) gSelSet.push_back(i);
    gSel = first;
    sceneChanged();
    gStatus = "Created baked mesh copies.";
}

void setBoolMode(BoolMode b) {
    bool loose = false;
    for (int i : gSelSet) {
        Object& o = gScene.objs[i];
        if (!isCsgCapable(o.type)) continue;
        o.boolMode = b;
        loose |= o.group == 0;
    }
    sceneChanged();
    gStatus = loose && b != BoolMode::Neutral ? "Boolean modes only combine objects that share a group. Select the parts and press Group."
                                              : std::string("Boolean mode set to ") + boolName(b) + ".";
}

bool fileDialog(bool save, const char* filter, const char* ext, std::string& out) {
    gModal = true;
    bool ok = chooseFile(save, filter, ext, out);
    finishDialog();
    return ok;
}

const char* SCENE_FILTER = "Landscape Scene (*.c3d)\0*.c3d\0All Files (*.*)\0*.*\0";
const char* BMP_FILTER = "Bitmap Image (*.bmp)\0*.bmp\0";
const char* IMAGE_FILTER = "Images\0*.bmp;*.png;*.jpg;*.jpeg;*.tga;*.tif;*.tiff;*.pgm;*.hdr;*.rgbe;*.pic\0All Files (*.*)\0*.*\0";
const char* HDR_FILTER = "Radiance HDR\0*.hdr;*.rgbe;*.pic\0All Files (*.*)\0*.*\0";
const char* OUTPUT_FILTER = "PNG Image (*.png)\0*.png\0TIFF Image (*.tiff)\0*.tiff;*.tif\0Bitmap Image (*.bmp)\0*.bmp\0";

std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

std::string extension(const std::string& p) {
    std::string name = baseName(p);
    auto dot = name.find_last_of('.');
    if (dot == std::string::npos) return {};
    name.erase(0, dot + 1);
    for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return name;
}

void actImport() {
    std::string p, err;
    if (!fileDialog(false, "Wavefront OBJ (*.obj)\0*.obj\0ASCII DXF (*.dxf)\0*.dxf\0All Files (*.*)\0*.*\0", "obj", p)) return;
    if (extension(p) == "dxf") {
        gDxfPath = std::move(p);
        openLab(LabDxf);
        return;
    }
    auto m = std::make_shared<Mesh>();
    if (!loadOBJ(p, *m, err)) { gStatus = "Could not import " + p + ": " + err; return; }
    Object o = makeObject(ObjType::Mesh, ++gCounter[(int)ObjType::Mesh]);
    std::string n = baseName(p);
    o.name = n.substr(0, n.find_last_of('.'));
    o.mesh = m;
    o.pos = {gScene.cam.target.x, 3, gScene.cam.target.z};
    o.scl = V3(3.f);
    gStatus = "Imported " + o.name + " (" + std::to_string(m->triCount()) + " triangles).";
    addObject(o);
}

void actExportGeometry() {
    if (gSelSet.empty()) { gStatus = "Select the objects to export."; return; }
    std::string p;
    if (!fileDialog(true, "Wavefront OBJ (*.obj)\0*.obj\0ASCII DXF (*.dxf)\0*.dxf\0", "obj", p)) return;
    auto format = extension(p);
    if (format != "obj" && format != "dxf") { gStatus = "Use an .obj or .dxf filename."; return; }
    auto parts = meshSelection(gScene, gSelSet, 48);
    if (parts.empty()) { gStatus = "The selection contains no exportable surfaces."; return; }
    gStatus = saveGeometry(p, parts, format == "dxf") ? "Exported geometry to " + p : "Could not write " + p;
}

bool loadPicture(Picture& picture) {
    std::string path;
    if (!fileDialog(false, IMAGE_FILTER, "png", path)) return false;
    auto image = loadImageCached(path, true);
    if (!image) { gStatus = "Could not read picture " + path + "."; return false; }
    picture.path = path;
    picture.aspect = (float)image->w / image->h;
    picture.image = std::move(image);
    return true;
}

void actCreatePicture() {
    Picture picture;
    if (!loadPicture(picture)) return;
    Object o = makeObject(ObjType::Picture, ++gCounter[(int)ObjType::Picture]);
    o.name = baseName(picture.path);
    o.picture = std::move(picture);
    o.pos.x = gScene.cam.target.x;
    o.pos.z = gScene.cam.target.z;
    o.rot.y = gScene.cam.yaw;
    gStatus = "Imported picture " + o.name + ".";
    addObject(std::move(o));
}

bool confirmDiscard() {
    if (!gDirty) return true;
    gModal = true;
    bool discard = confirm("Discard unsaved changes to the current scene?");
    finishDialog();
    return discard;
}

std::string recoveryPath() {
    return (std::filesystem::path(gLibPath).parent_path() / "recovery.c3d").string();
}

void clearRecovery() {
    std::error_code error;
    std::filesystem::remove(recoveryPath(), error);
}

void actNew() {
    if (!confirmDiscard()) return;
    gScene = makeEmptyScene();
    gPath.clear();
    sceneReplaced();
    clearRecovery();
    gStatus = "New scene.";
}

void actOpen() {
    std::string p;
    if (!confirmDiscard() || !fileDialog(false, SCENE_FILTER, "c3d", p)) return;
    Scene s;
    if (!s.load(p)) { gStatus = "Could not read scene file: " + p; return; }
    gScene = std::move(s);
    gPath = p;
    sceneReplaced();
    clearRecovery();
    auto missing = gScene.missingAssets();
    gStatus = missing.empty() ? "Opened " + p :
        "Opened scene with " + std::to_string(missing.size()) + " missing assets. Use Commands to relink.";
}

void actMerge() {
    std::string p;
    if (!fileDialog(false, SCENE_FILTER, "c3d", p)) return;
    size_t before = gScene.objs.size();
    if (!gScene.merge(p)) { gStatus = "Could not read scene file: " + p; return; }
    gSelSet.clear();
    for (size_t i = before; i < gScene.objs.size(); i++) gSelSet.push_back((int)i);
    gSel = gSelSet.empty() ? -1 : gSelSet.back();
    sceneChanged();
    gStatus = "Merged " + std::to_string(gScene.objs.size() - before) + " objects from " + p;
}

void actSave(bool as) {
    std::string p = gPath;
    if ((as || p.empty()) && !fileDialog(true, SCENE_FILTER, "c3d", p)) return;
    std::string oldTitle = gScene.title;
    if (gScene.title.empty()) gScene.title = std::filesystem::path(p).stem().string();
    if (!gScene.save(p)) { gScene.title = oldTitle; gStatus = "Could not write " + p; return; }
    gPath = p;
    gDirty = false;
    clearRecovery();
    std::vector<uint32_t> thumbnail(96 * 64);
    Scene preview = gScene;
    preview.render = {};
    renderImage(preview, 96, 64, thumbnail.data(), 0);
    saveRaster(p + ".thumb.png", 96, 64, thumbnail);
    gStatus = "Saved " + p;
}

void actPackage() {
    std::string path, error;
    if (!fileDialog(true, SCENE_FILTER, "c3d", path)) return;
    gStatus = gScene.package(path, error) ? "Saved the scene and collected assets." : "Could not package the scene: " + error;
}

void actEmbed() {
    std::string path, error;
    if (!fileDialog(true, SCENE_FILTER, "c3d", path)) return;
    Scene output = gScene;
    if (!output.embedAssets(error)) { gStatus = "Could not embed images: " + error; return; }
    gStatus = output.save(path) ? "Saved a scene with embedded images." : "Could not write the embedded scene.";
}

void actRelink() {
    auto missing = gScene.missingAssets();
    if (missing.empty()) { gStatus = "All scene assets are present."; return; }
    gStatus = "Choose a replacement for " + baseName(missing.front()) + ".";
    std::string replacement;
    if (!fileDialog(false, IMAGE_FILTER, "png", replacement)) return;
    if (!gScene.relinkAsset(missing.front(), replacement)) { gStatus = "Could not decode the replacement image."; return; }
    sceneChanged();
    gStatus = "Relinked " + baseName(replacement) + ".";
}

void actRecover() {
    Scene saved;
    if (!saved.load(recoveryPath())) { gStatus = "No usable autosave was found."; return; }
    if (!confirmDiscard()) return;
    gScene = std::move(saved);
    gPath.clear();
    sceneReplaced();
    gDirty = true;
    gStatus = "Recovered the autosave. Save As to keep it.";
}

void loadExample(int i) {
    if (!confirmDiscard()) return;
    gScene = makeExample(i);
    gPath.clear();
    sceneReplaced();
    clearRecovery();
    gStatus = "Loaded example scene \"" + exampleNames()[i] + "\". Press Render to see it.";
}

bool saveBMP(const std::string& p, int w, int h, const std::vector<uint32_t>& px) {
    FILE* f = fopen(p.c_str(), "wb");
    if (!f) return false;
    int rowB = (w * 3 + 3) & ~3;
    uint8_t hd[54] = {'B', 'M'};
    auto put32 = [&](int o, uint32_t v) { hd[o] = (uint8_t)v; hd[o + 1] = (uint8_t)(v >> 8); hd[o + 2] = (uint8_t)(v >> 16); hd[o + 3] = (uint8_t)(v >> 24); };
    put32(2, 54 + rowB * h); put32(10, 54); put32(14, 40); put32(18, w); put32(22, h); put32(34, rowB * h);
    hd[26] = 1; hd[28] = 24;
    fwrite(hd, 1, 54, f);
    std::vector<uint8_t> row(rowB, 0);
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; x++) {
            uint32_t c = px[(size_t)y * w + x];
            row[x * 3] = c & 255; row[x * 3 + 1] = (c >> 8) & 255; row[x * 3 + 2] = (c >> 16) & 255;
        }
        fwrite(row.data(), 1, rowB, f);
    }
    return fclose(f) == 0;
}

bool saveRenderOutput(const std::string& path, const Scene& rendered, int w, int h, std::vector<uint32_t> pixels) {
    bool bmp = extension(path) == "bmp";
    const auto& settings = rendered.render;
    if (settings.alpha && !bmp) renderOutputPass(rendered, w, h, pixels, RenderPass::Alpha);
    if (!(bmp ? saveBMP(path, w, h, pixels) : saveRaster(path, w, h, pixels, settings.alpha))) return false;
    auto stem = std::filesystem::path(path);
    stem.replace_extension();
    if (settings.depthOutput) {
        renderOutputPass(rendered, w, h, pixels, RenderPass::Depth);
        if (!saveRaster(stem.string() + "_depth.png", w, h, pixels)) return false;
    }
    if (settings.idOutput) {
        renderOutputPass(rendered, w, h, pixels, RenderPass::ObjectId);
        if (!saveRaster(stem.string() + "_id.png", w, h, pixels)) return false;
    }
    return true;
}

bool saveRenderer(const std::string& path) {
    std::vector<uint32_t> px;
    gMainR.copyTo(px);
    return saveRenderOutput(path, gMainR.scene(), gMainR.width(), gMainR.height(), std::move(px));
}

void actSaveImage() {
    if (!gMainR.hasImage()) { gStatus = "Render the scene first."; return; }
    std::string p;
    if (!fileDialog(true, OUTPUT_FILTER, "png", p)) return;
    gStatus = saveRenderer(p) ? "Saved image " + p : "Could not write image or passes for " + p;
}

void renderSize(int& w, int& h) {
    const RenderSettings& r = gScene.render;
    w = r.width > 0 && r.height > 0 ? r.width : gVP.w;
    h = r.width > 0 && r.height > 0 ? r.height : gVP.h;
}

const char* qualityName(int q) { return q == 0 ? "Draft" : (q == 1 ? "Normal" : "Premium"); }

void actRender() {
    if (gMainR.busy() || gAnimRender || !gQueue.empty()) {
        if (gAnimRender || !gQueue.empty()) gResumeAllowed = false;
        gQueue.clear();
        gMainWasBusy = gAnimRender = false;
        gMainR.cancel();
        gStatus = "Render stopped.";
        return;
    }
    gView = 0;
    gShowRender = true;
    gRenderStart = nowMs();
    if (gResumeAllowed && gMainR.resume()) { gStatus = "Resumed render."; return; }
    int w, h;
    renderSize(w, h);
    gMainR.start(gScene, w, h);
    gResumeAllowed = true;
    char b[96];
    snprintf(b, sizeof b, "Rendering %dx%d (%s)...", w, h, qualityName(gScene.render.quality));
    gStatus = b;
}

void actRenderAnim() {
    std::string p;
    if (!fileDialog(true, OUTPUT_FILTER, "png", p)) return;
    auto file = std::filesystem::path(p);
    gAnimExtension = file.extension().string();
    gAnimBase = file.replace_extension().string();
    gMainR.cancel();
    gQueue.clear();
    gResumeAllowed = false;
    gAnimRender = true;
    gAnimFrame = -1;
    gPlaying = false;
    gView = 0;
    gShowRender = true;
    gRenderStart = nowMs();
}

std::string animationName(int frame) {
    char number[24];
    snprintf(number, sizeof number, "%04d", frame);
    std::string name = gAnimBase;
    auto placeholder = name.find("{frame}");
    if (placeholder == std::string::npos) name += std::string("_") + number;
    else name.replace(placeholder, 7, number);
    return name + gAnimExtension;
}

bool readQueue(const std::string& path, std::vector<RenderJob>& jobs) {
    std::ifstream source(path);
    if (!source) return false;
    std::string line;
    auto root = std::filesystem::path(path).parent_path();
    std::set<std::string> outputs;
    while (std::getline(source, line)) {
        auto start = line.find_first_not_of(" \r\t");
        if (start == std::string::npos || line[start] == '#') continue;
        std::istringstream record(line);
        RenderJob job;
        if (!(record >> std::quoted(job.scene) >> std::quoted(job.output)) || job.scene.empty() || job.output.empty() || jobs.size() >= 10000)
            return false;
        record >> std::ws;
        if (!record.eof()) return false;
        job.scene = (root / job.scene).lexically_normal().string();
        job.output = (root / job.output).lexically_normal().string();
        auto ext = extension(job.output);
        if ((ext != "bmp" && ext != "png" && ext != "tif" && ext != "tiff") || !outputs.insert(job.output).second) return false;
        jobs.push_back(std::move(job));
    }
    return !source.bad() && !jobs.empty();
}

void actQueue() {
    std::string path;
    if (!fileDialog(false, "Render Queue (*.txt)\0*.txt\0", "txt", path)) return;
    std::vector<RenderJob> jobs;
    if (!readQueue(path, jobs)) { gStatus = "Invalid queue. Each line needs a scene path and a unique output path."; return; }
    gMainR.cancel();
    gAnimRender = gPlaying = gResumeAllowed = false;
    gQueue = std::move(jobs);
    gQueueIndex = -1;
    gShowRender = true;
}

void setFrame(int f) {
    gScene.applyFrame(std::min(std::max(f, 0), gScene.frames - 1));
    if (gPlaying) {
        gPlayStart = nowMs();
        gPlayStartFrame = gScene.frame;
        gPlayEndFrame = -1;
    }
    gShowRender = false;
    sceneChanged();
}

void startPlayback(int end = -1) {
    gPlaying = true;
    gPlayStart = nowMs();
    gPlayStartFrame = gScene.frame;
    gPlayEndFrame = end;
}

void togglePlayback() {
    if (gPlaying) gPlaying = false;
    else startPlayback();
}

void addKey() {
    int f = gScene.frame;
    if (gSelSet.empty()) putKey(gScene.camKeys, CamKey{f, gScene.cam});
    for (int i : gSelSet) { Object& o = gScene.objs[i]; putKey(o.keys, Key{f, o.pos, o.rot, o.scl}); }
    gChanged = true;
    gStatus = "Key set at frame " + std::to_string(f) + (gSelSet.empty() ? " for the camera." : " for the selection.");
}

void autoKey() { addKey(); }

void delKey() {
    int f = gScene.frame;
    bool any = false;
    if (gSelSet.empty()) any = eraseKey(gScene.camKeys, f);
    for (int i : gSelSet) any |= eraseKey(gScene.objs[i].keys, f);
    gChanged = true;
    gStatus = any ? "Removed key at frame " + std::to_string(f) + "." : "No key at this frame.";
}

std::vector<int> keyFrames(bool sky) {
    std::vector<int> r;
    if (sky) for (auto& k : gScene.skyKeys) r.push_back(k.frame);
    else if (gSelSet.empty()) for (auto& k : gScene.camKeys) r.push_back(k.frame);
    else
        for (int i : gSelSet) {
            const auto& object = gScene.objs[i];
            for (const auto& key : object.keys) r.push_back(key.frame);
            const auto& source = object.instanceSource >= 0 ? gScene.objs[object.instanceSource] : object;
            if (source.rig)
                for (const auto& joint : source.rig->joints)
                    for (const auto& key : joint.keys) r.push_back(key.frame);
        }
    std::sort(r.begin(), r.end());
    r.erase(std::unique(r.begin(), r.end()), r.end());
    return r;
}

void jumpKey(int dir) {
    auto k = keyFrames(false), s = keyFrames(true);
    k.insert(k.end(), s.begin(), s.end());
    int best = dir > 0 ? INT_MAX : INT_MIN;
    for (int f : k)
        if (dir > 0 ? (f > gScene.frame && f < best) : (f < gScene.frame && f > best)) best = f;
    if (best != INT_MAX && best != INT_MIN) setFrame(best);
}

void saveLibrary() {
    if (!gLib.save(gLibPath)) gStatus = "Could not write library " + gLibPath;
}

void rememberLibrary() {
    gLibraryUndo.push_back(gLib);
    if (gLibraryUndo.size() > 24) gLibraryUndo.erase(gLibraryUndo.begin());
    gLibraryRedo.clear();
}

void libraryHistory(bool redo) {
    auto& from = redo ? gLibraryRedo : gLibraryUndo;
    auto& to = redo ? gLibraryUndo : gLibraryRedo;
    if (from.empty()) return;
    if (!from.back().save(gLibPath)) { gStatus = "Could not save the library history change."; return; }
    to.push_back(std::move(gLib));
    gLib = std::move(from.back());
    from.pop_back();
    gStatus = redo ? "Redo library edit." : "Undo library edit.";
}

std::filesystem::path assetDirectory() {
    return std::filesystem::path(gLibPath).parent_path() / "assets";
}

void refreshAssets() {
    gAssets.clear();
    gAssetSelection = -1;
    gAssetPage = 0;
    std::error_code error;
    std::filesystem::directory_iterator it(assetDirectory(), error), end;
    for (; !error && it != end; it.increment(error)) {
        if (it->path().extension() != ".c3d") continue;
        AssetTile tile{it->path().string(), it->path().stem().string(), {}};
        auto bmp = it->path();
        auto image = loadBMP(bmp.replace_extension(".bmp").string());
        if (image && image->w == 96 && image->h == 64) {
            for (V3 p : image->px) tile.thumbnail.push_back(rgb((unsigned)(p.x * 255), (unsigned)(p.y * 255), (unsigned)(p.z * 255)));
        }
        gAssets.push_back(std::move(tile));
    }
    std::sort(gAssets.begin(), gAssets.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}

void storeAsset(const Scene& asset, std::string name) {
    if (asset.objs.empty()) { gStatus = "There are no objects to save."; return; }
    for (char& c : name)
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) c = '_';
    if (name.empty()) name = "Scene";
    std::error_code error;
    auto dir = assetDirectory();
    std::filesystem::create_directories(dir, error);
    if (error) { gStatus = "Could not open the asset library."; return; }
    auto path = dir / (name + ".c3d");
    for (int suffix = 2; std::filesystem::exists(path, error) && !error; ++suffix)
        path = dir / (name + "_" + std::to_string(suffix) + ".c3d");
    if (error || !asset.save(path.string())) { gStatus = "Could not write the asset."; return; }
    Scene previewScene = asset;
    V3 lo(1e30f), hi(-1e30f);
    bool finite = false;
    for (const Object& o : asset.objs) {
        V3 a, b;
        if (o.hidden || !worldBounds(o, a, b)) continue;
        lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
        hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
        finite = true;
    }
    if (finite) {
        previewScene.cam.target = (lo + hi) * 0.5f;
        previewScene.cam.dist = std::max(2.f, length(hi - lo) * 1.4f);
    }
    previewScene.render.panorama = false;
    std::vector<uint32_t> pixels(96 * 64);
    renderImage(previewScene, 96, 64, pixels.data(), 0);
    saveBMP(path.replace_extension(".bmp").string(), 96, 64, pixels);
    refreshAssets();
    gStatus = "Saved asset " + name + ".";
}

// ---------------------------------------------------------------- labs: open / close / preview

std::vector<int> treeMembers() {
    std::vector<int> r;
    for (int i : sortedSel())
        if (gScene.objs[i].treePart) r.push_back(i);
    return r;
}

Lab objectEditor(const Object& o) {
    if (isHeightfield(o.type)) return LabTerrain;
    if (o.treePart) return LabTree;
    if (o.type == ObjType::Metaball || o.type == ObjType::Picture || o.type == ObjType::Stone ||
        o.type == ObjType::Torus || o.type == ObjType::Mesh || o.type == ObjType::Water || isLight(o.type)) return LabObject;
    return LabNone;
}

void openLab(Lab l) {
    if (l == LabNone) return;
    if (l == LabMesh || l == LabCurves || l == LabRig) {
        if (!validSel() || gScene.objs[gSel].locked) { gStatus = "Select an unlocked object first."; return; }
        gPlaying = false;
    }
    if (l == LabMat || l == LabTerrain || l == LabObject || l == LabAttributes) {
        if (!validSel()) { gStatus = "Select an object first."; return; }
        Object& o = gScene.objs[gSel];
        if (l == LabMat && isLight(o.type)) { gStatus = "Press E to open the selected light's editor."; return; }
        if (l == LabTerrain && (!isHeightfield(o.type) || !o.hf)) { gStatus = "Select a terrain or lattice to open the Terrain Editor."; return; }
    }
    if (l == LabTree) {
        auto t = treeMembers();
        if (t.empty()) { gStatus = "Select a tree (or create one with the Tree button) to open the Tree Lab."; return; }
        gTreeP = gScene.objs[t[0]].tree;
    }
    if (l == LabTools && gSelSet.empty()) { gStatus = "Select objects to use the Edit Tools."; return; }
    commitRename();
    if (l == LabMat) { gMatLayers = false; gLayerIndex = 0; }
    if (l == LabAssets) refreshAssets();
    if (l == LabMesh) {
        gMeshSelection.clear();
        gMeshOffset = gMeshTransform == 2 ? V3(1) : V3{};
        gMeshMessage.clear();
    }
    if (l == LabCurves) {
        gCurveKey = -1;
        gCurveJointTrack = false;
        gCurveJoint = 0;
    }
    if (l == LabRig) {
        gRigJoint = 0;
        gRigMode = 0;
        gRigVertices.clear();
        gRigMessage.clear();
    }
    if (l == LabSimulation) {
        gPlaying = false;
        gSimulationError.clear();
    }
    if (l == LabTerrain) {
        gRegionTool = gRegionMask = false;
        gTerrainUndo.clear();
        gTerrainRedo.clear();
        gTerrainBlendMask.reset();
        gTerrainBlend = 1;
    }
    gLabBackup = gScene.snapshot();
    gLab = l;
    gLabChanged = false;
    gLabDirty = true;
    gLastChange = 0;
}

void closeLab(bool cancel) {
    if (cancel) gScene = gLabBackup;
    else {
        if (gLab == LabMat && validSel())
            for (int i : gSelSet)
                if (!isLight(gScene.objs[i].type)) gScene.objs[i].mat = gScene.objs[gSel].mat;
        if (gLabChanged) {
            const char* name = "Edit scene";
            if (gLab == LabMat) name = "Edit materials";
            else if (gLab == LabSky) name = "Edit sky";
            else if (gLab == LabTerrain) name = "Edit terrain";
            else if (gLab == LabMotion) name = "Edit motion";
            else if (gLab == LabMesh) name = "Edit mesh";
            else if (gLab == LabCurves) name = "Edit curves";
            else if (gLab == LabRig) name = "Edit rig";
            else if (gLab == LabDxf) name = "Import DXF";
            else if (gLab == LabAssets) name = "Insert library asset";
            else if (gLab == LabSimulation) name = gLiquidSimulation ? "Bake liquid" : "Bake rigid bodies";
            pushUndo(std::move(gLabBackup), name);
        }
    }
    gLabR.cancel();
    gLab = LabNone;
    gNoPush = true;
    sceneChanged();
}

void startLabPreview() {
    if (gLab == LabMat && validSel()) {
        Scene s;
        s.sky = gScene.sky;
        Object g = makeObject(ObjType::Ground, 0);
        g.mat = findMaterial("Checker");
        Object sp = makeObject(ObjType::Sphere, 0);
        sp.pos = {0, 1, 0};
        sp.scl = V3(1.f);
        sp.mat = gScene.objs[gSel].mat;
        s.objs = {g, sp};
        s.cam.target = {0, 1, 0};
        s.cam.dist = 4.2f; s.cam.pitch = 14; s.cam.yaw = 30; s.cam.fov = 42;
        gLabR.start(s, 320, 320, 1);
    } else if (gLab == LabTree) {
        Scene s;
        s.sky = gScene.sky;
        s.objs.push_back(makeObject(ObjType::Ground, 0));
        for (int i : treeMembers()) {
            Object o = gScene.objs[i];
            o.pos = V3(0.f); o.rot = V3(0.f); o.scl = V3(4.f); o.group = 0;
            o.parent = -1; o.bind = {}; o.bindOffset = {}; o.hasBind = false;
            s.objs.push_back(o);
        }
        s.cam.target = {0, 2.4f, 0};
        s.cam.dist = 10; s.cam.pitch = 8; s.cam.yaw = 30; s.cam.fov = 45;
        gLabR.start(s, 320, 320, 1);
    } else if (gLab == LabSky) gLabR.start(gScene, 340, 255, 0);
    else if (gLab == LabTerrain || gLab == LabObject) gLabR.start(gScene, 320, 240, 0);
}

// ---------------------------------------------------------------- panels

std::vector<std::string> categories(bool sky) {
    std::vector<std::string> c;
    auto add = [&](const std::string& s) { if (std::find(c.begin(), c.end(), s) == c.end()) c.push_back(s); };
    if (sky) for (auto& k : skyPresets()) add(k.category);
    else for (auto& m : materialPresets()) add(m.category);
    return c;
}

void applyQuickMaterial(const Material& m) {
    for (int i : gSelSet)
        if (!isLight(gScene.objs[i].type)) gScene.objs[i].mat = m;
    sceneChanged();
}

void drawTop() {
    R bar{0, 0, gW, TOP};
    vgrad(bar, rgb(108, 108, 116), rgb(60, 60, 66));
    line(0, TOP - 1, gW, TOP - 1, rgb(28, 28, 32));
    const char* tabs[3] = {"Create", "Edit", "Sky & Fog"};
    for (int i = 0; i < 3; i++)
        if (button({10 + i * 96, 4, 92, 22}, tabs[i], gTab == i, gBold)) gTab = i;
    textIn({310, 4, 260, 22}, APP_NAME, rgb(240, 240, 245), gTitle, TextLeft | TextMiddle);
    const char* fb[6] = {"New", "Open...", "Merge...", "Save", "Save As...", "Examples..."};
    int fx = gW - 6 * 76 - 6;
    if (button({fx - 88, 4, 82, 22}, "Commands")) openLab(LabCommands);
    for (int i = 0; i < 6; i++)
        if (button({fx + i * 76, 4, 72, 22}, fb[i])) {
            if (i == 0) defer(actNew);
            else if (i == 1) defer(actOpen);
            else if (i == 2) defer(actMerge);
            else if (i == 3) defer([] { actSave(false); });
            else if (i == 4) defer([] { actSave(true); });
            else openLab(LabExamples);
        }

    if (gTab == 0) {
        int x = 10;
        int step = std::clamp((gW - 16) / ((int)std::size(kCreate) + 3), 38, 60), width = step - 4;
        for (ObjType t : kCreate) {
            if (iconButton({x, 30, width, 62}, gIcons[(int)t], ICW, ICH, objTypeName(t))) createObject(t);
            x += step;
        }
        if (iconButton({x, 30, width, 62}, gTreeIcon, ICW, ICH, "Tree")) createTree();
        x += step;
        if (iconButton({x, 30, width, 62}, {}, ICW, ICH, "Picture", false, "IMG")) defer(actCreatePicture);
        x += step;
        if (iconButton({x, 30, width, 62}, {}, ICW, ICH, "Import", false, "3D")) defer(actImport);
    } else if (gTab == 1) {
        const char* tools[4] = {"Move", "Rotate", "Resize", "Sculpt"};
        for (int i = 0; i < 4; i++)
            if (button({10 + i * 64, 32, 60, 24}, tools[i], gTool == i)) {
                gTool = i;
                if (i == TOOL_SCULPT) gStatus = "Sculpt: click and hold on a terrain to raise it. Ctrl lowers, Shift smooths, [ and ] change brush size.";
            }
        struct B { const char* n; int w; std::function<void()> fn; } bs[] = {
            {"Materials Lab", 100, [] { openLab(LabMat); }}, {"Terrain Editor", 100, [] { openLab(LabTerrain); }},
            {"Tree Lab", 72, [] { openLab(LabTree); }},       {"Edit Tools", 80, [] { openLab(LabTools); }},
            {"Land", 50, actLand},                            {"Duplicate", 74, actDuplicate},
            {"Delete", 60, actDelete},                        {"Group", 56, actGroup},
            {"Ungroup", 66, actUngroup},                      {"Undo", 50, [] { undoRedo(false); }},
            {"Redo", 50, [] { undoRedo(true); }},
            {"Export Mesh", 84, [] { defer(actExportGeometry); }},
        };
        int x = 270;
        for (auto& b : bs) {
            if (button({x, 32, b.w, 24}, b.n)) b.fn();
            x += b.w + 4;
        }
        if (gTool == TOOL_SCULPT) {
            textIn({10, 64, 60, 24}, "Brush:", TXT_DIM, gSmall, TextLeft | TextMiddle);
            int bx = 56;
            for (int i = 0; i < (int)BrushMode::COUNT; i++, bx += 70)
                if (button({bx, 64, 66, 24}, brushName((BrushMode)i), gBrush == (BrushMode)i, gSmall)) gBrush = (BrushMode)i;
            slider({bx + 6, 64, 190, 24}, "Size", gBrushSize, 0.01f, 0.3f, "%.2f", true);
            slider({bx + 202, 64, 190, 24}, "Strength", gBrushStr, 0.05f, 1);
            textIn({bx + 400, 64, gW - bx - 410, 24}, "Hold to build up  |  Ctrl invert  |  Shift smooth", TXT_DIM, gSmall, TextLeft | TextMiddle);
            return;
        }
        auto cats = categories(false);
        gQuickCat = (gQuickCat + (int)cats.size()) % (int)cats.size();
        textIn({10, 64, 100, 24}, "Quick materials:", TXT_DIM, gSmall, TextLeft | TextMiddle);
        if (button({112, 64, 22, 24}, "<")) gQuickCat--;
        textIn({136, 64, 92, 24}, cats[gQuickCat].c_str(), rgb(255, 255, 255), gBold);
        if (button({230, 64, 22, 24}, ">")) gQuickCat++;
        if (button({260, 64, 58, 24}, "Assets")) openLab(LabAssets);
        if (button({324, 64, 96, 24}, "Transform...")) openLab(LabTransform);
        if (button({426, 64, 98, 24}, "Simulation...")) openLab(LabSimulation);
        int qx = 530;
        for (auto& m : materialPresets()) {
            if (m.category != cats[gQuickCat]) continue;
            if (qx + 92 > gW - 10) break;
            if (button({qx, 64, 88, 24}, m.name.c_str(), validSel() && gScene.objs[gSel].mat.name == m.name, gSmall)) applyQuickMaterial(m);
            qx += 92;
        }
    } else {
        Sky& k = gScene.sky;
        auto cats = categories(true);
        gSkyCat = (gSkyCat + (int)cats.size()) % (int)cats.size();
        textIn({10, 32, 140, 24}, cats[gSkyCat].c_str(), rgb(255, 255, 255), gBold);
        if (button({10, 62, 68, 24}, "<")) gSkyCat--;
        if (button({82, 62, 68, 24}, ">")) gSkyCat++;
        auto& sp = skyPresets();
        int x = 160;
        for (size_t i = 0; i < sp.size(); i++) {
            if (sp[i].category != cats[gSkyCat]) continue;
            if (iconButton({x, 30, STW + 6, 62}, gSkyThumbs[i], STW, STH, sp[i].name.c_str(), k.name == sp[i].name)) { k = sp[i]; sceneChanged(); }
            x += STW + 10;
        }
        int sx = std::max(x + 10, 688);
        bool ch = false;
        ch |= slider({sx, 32, 126, 24}, "Sun Alt", k.sunAlt, -10, 90, "%.0f");
        ch |= slider({sx + 130, 32, 126, 24}, "Sun Az", k.sunAz, 0, 360, "%.0f");
        ch |= slider({sx + 260, 32, 126, 24}, "Haze", k.haze, 0, 1);
        ch |= slider({sx, 62, 126, 24}, "Fog", k.fog, 0, 1);
        ch |= slider({sx + 130, 62, 126, 24}, "Fog Ht", k.fogHeight, 0, 40, "%.1f");
        ch |= slider({sx + 260, 62, 126, 24}, "Clouds", k.clouds, 0, 1);
        if (ch) sceneChanged();
        if (button({sx + 396, 32, 90, 54}, "Sky Lab", false, gBold)) openLab(LabSky);
    }
}

void drawLeft() {
    R p{0, TOP, LEFT, gH - TOP - STATUS - TIMELINE};
    vgrad(p, rgb(90, 90, 96), rgb(56, 56, 62));
    int x = 10, y = TOP + 8;
    label(x, y, "Nano Preview");
    y += 18;
    R nano{x, y, NANO_W, NANO_H};
    fill(nano, rgb(20, 20, 24));
    blitFit(gNanoR, nano);
    bevel(nano, true);
    y += NANO_H + 8;
    const char* vn[4] = {"Camera", "Top", "Front", "Side"};
    for (int i = 0; i < 4; i++)
        if (button({x + (i % 2) * 76, y + (i / 2) * 24, 72, 22}, vn[i], gView == i)) { gView = i; gShowRender = false; }
    y += 52;
    for (int i = 0; i < 6; i++) {
        int id = newId();
        R d{x + i * 25, y, 22, 22};
        press(d, id);
        bool set = gScene.memorySet[i];
        shadedBall(d.x + 11, d.y + 11, 9, set ? V3(0.95f, 0.75f, 0.2f) : V3(0.35f, 0.35f, 0.4f), hov(d));
        if (clicked(d, id)) {
            if (gIn.ctrl) { gScene.memorySet[i] = false; gStatus = "Cleared view memory."; }
            else if (set && !gIn.shift) { gScene.cam = gScene.memory[i]; gView = 0; gShowRender = false; sceneChanged(); gStatus = "Recalled view memory."; }
            else { gScene.memory[i] = gScene.cam; gScene.memorySet[i] = true; gStatus = "Stored the camera in view memory (click to recall, Shift+click to overwrite, Ctrl+click to clear)."; }
        }
    }
    y += 30;
    if (button({x, y, 72, 22}, "Frame Sel", false, gSmall)) frameView(true);
    if (button({x + 76, y, 72, 22}, "Frame All", false, gSmall)) frameView(false);
    y += 30;

    int tid = newId();
    R tb{x + 2, y, 84, 84};
    press(tb, tid);
    shadedBall(tb.x + 42, tb.y + 42, 40, V3(0.45f, 0.55f, 0.8f), hov(tb) || gActive == tid);
    textIn({tb.x, tb.y + 30, tb.w, 22}, "Orbit", rgb(255, 255, 255), gBold);
    if (gActive == tid && gIn.down && (gIn.dx || gIn.dy)) { gView = 0; gShowRender = false; orbitCam((float)gIn.dx, (float)gIn.dy); }
    auto pad = [&](R r, const char* t, std::function<void()> fn) {
        int id = newId();
        press(r, id);
        vgrad(r, rgb(120, 120, 128), rgb(70, 70, 78));
        bevel(r, gActive == id);
        textIn(r, t, TXT, gBold);
        if (gActive == id && gIn.down && (gIn.dx || gIn.dy)) { gShowRender = false; fn(); }
    };
    pad({x + 96, y, 54, 40}, "Pan", [] { panView((float)gIn.dx, (float)gIn.dy); });
    pad({x + 96, y + 44, 54, 40}, "Zoom", [] { zoomView(-gIn.dy * 0.05f); });
    y += 92;

    int rid = newId();
    R rb{x + 75 - 34, y, 68, 68};
    press(rb, rid);
    bool busy = gMainR.busy() || gAnimRender || !gQueue.empty();
    shadedBall(rb.x + 34, rb.y + 34, 33, busy ? V3(0.8f, 0.25f, 0.2f) : V3(0.25f, 0.65f, 0.3f), hov(rb));
    textIn({x, y + 70, 150, 18}, busy ? "STOP" : "RENDER", rgb(255, 255, 255), gBold);
    if (clicked(rb, rid)) actRender();
    y += 92;
    int& q = gScene.render.quality;
    const int qx[] = {0, 44, 94}, qw[] = {42, 48, 54};
    for (int i = 0; i < 3; i++)
        if (button({x + qx[i], y, qw[i], 22}, qualityName(i), q == i, gSmall)) { q = i; gChanged = true; }
    y += 26;
    if (button({x, y, 148, 22}, "Render Options...")) openLab(LabRender);
    y += 26;
    if (button({x, y, 148, 22}, "Cameras...")) openLab(LabCameras);
    y += 26;
    if (button({x, y, 148, 22}, gShowRender ? "Show Wireframe" : "Show Render") && gMainR.hasImage()) gShowRender = !gShowRender;
    y += 26;
    if (button({x, y, 148, 22}, "Save Image...")) defer(actSaveImage);
}

bool row3(int x, int& y, int w, const char* title, V3& v, float speed, float minV = -1e9f) {
    int cw = (w - 8) / 3;
    label(x, y, title);
    y += 18;
    bool ch = dragF({x, y, cw, 20}, "X", v.x, speed, minV);
    ch |= dragF({x + cw + 4, y, cw, 20}, "Y", v.y, speed, minV);
    ch |= dragF({x + 2 * (cw + 4), y, cw, 20}, "Z", v.z, speed, minV);
    y += 26;
    if (ch) sceneChanged();
    return ch;
}

void lightControls(Object& light, int x, int& y, int w) {
    swatch({x, y, w, 24}, "Light Color", light.lightColor);
    y += 28;
    bool ch = slider({x, y, w, 22}, "Power", light.lightPower, 0, 5); y += 26;
    if (light.type == ObjType::Spotlight) {
        ch |= slider({x, y, w, 22}, "Cone Angle", light.spotAngle, 2, 85, "%.0f"); y += 26;
        ch |= slider({x, y, w, 22}, "Softness", light.spotSoft, 0, 1); y += 26;
    }
    ch |= slider({x, y, w, 22}, "Soft Shadow Size", light.lightSize, 0, 3); y += 28;
    if (ch) sceneChanged();
}

void drawRight() {
    int x0 = gW - RIGHT;
    vgrad({x0, TOP, RIGHT, gH - TOP - STATUS - TIMELINE}, rgb(90, 90, 96), rgb(56, 56, 62));
    int x = x0 + 10, w = RIGHT - 20, y = TOP + 8;
    label(x, y, "Scene Hierarchy");
    y += 18;
    std::string filter = gEditObj == -2 ? gEditBuf : gFilter;
    std::string filterLabel = gEditObj == -2 ? gEditBuf + "|" : (gFilter.empty() ? "Filter objects..." : gFilter);
    if (button({x, y, w - 32, 21}, filterLabel.c_str(), gEditObj == -2, gSmall)) {
        gEditObj = -2;
        gEditBuf = gFilter;
    }
    if (button({x + w - 28, y, 28, 21}, "X", false, gSmall)) { gFilter.clear(); filter.clear(); gListScroll = 0; }
    y += 25;
    const int rows = 8, n = (int)gScene.objs.size();
    std::vector<std::vector<int>> children(n + 1);
    for (int i = 0; i < n; ++i) children[gScene.objs[i].parent + 1].push_back(i);
    std::vector<std::pair<int, int>> ordered, pending;
    for (auto it = children[0].rbegin(); it != children[0].rend(); ++it) pending.emplace_back(*it, 0);
    while (!pending.empty()) {
        auto node = pending.back();
        pending.pop_back();
        ordered.push_back(node);
        auto& list = children[node.first + 1];
        for (auto it = list.rbegin(); it != list.rend(); ++it) pending.emplace_back(*it, node.second + 1);
    }
    auto lowercase = [](std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return s;
    };
    if (!filter.empty()) {
        filter = lowercase(std::move(filter));
        ordered.erase(std::remove_if(ordered.begin(), ordered.end(), [&](const auto& row) {
            const auto& o = gScene.objs[row.first];
            return lowercase(o.name + " " + objTypeName(o.type) + " " + o.mat.name).find(filter) == std::string::npos;
        }), ordered.end());
    }
    gListR = {x, y, w, rows * 18 + 4};
    fill(gListR, rgb(38, 38, 44));
    bevel(gListR, true);
    int visible = (int)ordered.size(), drop = -1;
    gListScroll = std::max(0, std::min(gListScroll, visible - rows));
    for (int k = 0; k < rows && gListScroll + k < visible; k++) {
        auto [idx, depth] = ordered[gListScroll + k];
        R row{x + 2, y + 2 + k * 18, w - 4, 18};
        const Object& o = gScene.objs[idx];
        if (isSel(idx)) fill(row, idx == gSel ? rgb(92, 108, 156) : rgb(72, 84, 120));
        else if (hov(row)) fill(row, rgb(60, 60, 70));
        std::string nm = (o.group ? "[" + std::to_string(o.group) + "] " : "") +
                         (gEditObj == idx ? gEditBuf + "|" : o.name);
        int indent = std::min(depth, 6) * 10;
        textIn({row.x + 6 + indent, row.y, row.w - 74 - indent, row.h}, nm.c_str(), o.hidden ? TXT_DIM : TXT, gFont, TextLeft | TextMiddle);
        std::string tn = std::string(o.hidden ? "H " : "") + (o.locked ? "L " : "") +
                         (o.boolMode != BoolMode::Neutral ? boolName(o.boolMode) : objTypeName(o.type));
        textIn({row.x, row.y, row.w - 6, row.h}, tn.c_str(), TXT_DIM, gSmall, TextRight | TextMiddle);
        int id = newId();
        press(row, id);
        if (gActive == id && gIn.pressed) {
            if (gLastRow == idx && nowMs() - gLastRowTime < 380 && !gIn.shift && !gIn.ctrl) {
                gEditObj = idx; gEditBuf = o.name;
                gListDrag = -1;
                gLastRow = -1;
            } else {
                selectObj(idx, gIn.shift, gIn.alt || gIn.ctrl);
                gLastRow = idx; gLastRowTime = nowMs();
                gListDrag = idx; gListDragY = gIn.my;
            }
        }
        if (hov(row)) drop = idx;
    }
    if (gIn.released && gListDrag >= 0) {
        if (drop >= 0 && std::abs(gIn.my - gListDragY) > 5) {
            int from = gListDrag;
            bool parent = gIn.alt;
            defer([from, drop, parent] {
                if (parent) {
                    if (gScene.setParent(from, drop)) { sceneChanged(); gStatus = "Reparent scene object"; }
                    else gStatus = "That hierarchy link would create a cycle.";
                } else reorderSceneObject(from, drop);
            });
        }
        gListDrag = -1;
    }
    y += gListR.h + 8;
    if (validSel()) {
        Object& o = gScene.objs[gSel];
        if (gSelSet.size() > 1) {
            char b[64];
            snprintf(b, sizeof b, "%d objects selected", (int)gSelSet.size());
            textIn({x, y, w, 16}, b, TXT_DIM, gSmall, TextLeft | TextMiddle);
            y += 16;
        }
        int id = newId();
        R nr{x, y, w, 24};
        press(nr, id);
        bool editing = gEditObj == gSel;
        fill(nr, editing ? rgb(28, 28, 34) : (hov(nr) ? rgb(70, 70, 80) : rgb(56, 56, 64)));
        bevel(nr, true);
        std::string shown = editing ? gEditBuf + "|" : o.name;
        textIn({nr.x + 6, nr.y, nr.w - 12, nr.h}, shown.c_str(), rgb(255, 255, 255), gBold, TextLeft | TextMiddle);
        if (gActive == id && gIn.pressed && !editing) { gEditObj = gSel; gEditBuf = o.name; }
        y += 28;
        row3(x, y, w, "Position", o.pos, 0.05f);
        if (o.type != ObjType::Light) row3(x, y, w, isLight(o.type) ? "Aim (rotation)" : "Rotation", o.rot, 0.5f);
        if (!isLight(o.type)) {
            row3(x, y, w, "Size", o.scl, 0.02f, 0.01f);
            label(x, y, "Material");
            textIn({x + 66, y, w - 66, 16}, o.mat.name.c_str(), TXT, gFont, TextLeft | TextMiddle);
            y += 20;
            int bw = (w - 4) / 2;
            Lab editor = objectEditor(o);
            if (button({x, y, editor != LabNone ? bw : w, 22}, "Materials Lab...")) openLab(LabMat);
            const char* editLabel = editor == LabTerrain ? "Terrain Editor..." : (editor == LabTree ? "Tree Lab..." : "Object Editor...");
            if (editor != LabNone && button({x + bw + 4, y, bw, 22}, editLabel)) openLab(editor);
            y += 28;
            if (isCsgCapable(o.type)) {
                label(x, y, "Boolean");
                y += 18;
                int qw = (w - 12) / 4;
                const char* bn[4] = {"Neutral", "Pos", "Neg", "Inter"};
                for (int b = 0; b < 4; b++)
                    if (button({x + b * (qw + 4), y, qw, 22}, bn[b], o.boolMode == (BoolMode)b, gSmall)) setBoolMode((BoolMode)b);
                y += 28;
            }
        } else {
            if (button({x, y, w, 22}, "Light Editor...")) openLab(LabObject);
            y += 28;
            lightControls(o, x, y, w);
        }
        int tw = (w - 8) / 3;
        std::string gl = o.group ? "Group " + std::to_string(o.group) : "No group";
        textIn({x, y, tw, 22}, gl.c_str(), TXT_DIM, gSmall, TextLeft | TextMiddle);
        bool hid = o.hidden, lock = o.locked;
        if (toggle({x + tw + 4, y, tw, 22}, "Hidden", hid)) { for (int i : gSelSet) gScene.objs[i].hidden = hid; sceneChanged(); }
        if (toggle({x + 2 * (tw + 4), y, tw, 22}, "Locked", lock)) { for (int i : gSelSet) gScene.objs[i].locked = lock; gChanged = true; }
        y += 28;
        if (button({x, y, tw, 24}, "Land")) actLand();
        if (button({x + tw + 4, y, tw, 24}, "Duplicate")) actDuplicate();
        if (button({x + 2 * (tw + 4), y, tw, 24}, "Delete")) actDelete();
    } else {
        Camera& c = gScene.cam;
        textIn({x, y, w, 20}, "Camera", rgb(255, 255, 255), gTitle, TextLeft | TextMiddle);
        y += 26;
        if (slider({x, y, w, 22}, "Field of View", c.fov, 15, 120, "%.0f")) sceneChanged();
        y += 28;
        row3(x, y, w, "Target", c.target, 0.05f);
        int cw = (w - 8) / 3;
        label(x, y, "Orbit");
        y += 18;
        bool ch = dragF({x, y, cw, 20}, "Yaw", c.yaw, 0.5f);
        ch |= dragF({x + cw + 4, y, cw, 20}, "Pitch", c.pitch, 0.3f, -89, 89);
        ch |= dragF({x + 2 * (cw + 4), y, cw, 20}, "Dist", c.dist, 0.2f, 1.f);
        y += 28;
        if (ch) sceneChanged();
        if (button({x, y, w, 24}, "Reset Camera")) { c = Camera(); sceneChanged(); }
        y += 34;
        note({x, y, w, 100}, "Click an object to select it. Shift+click adds to the selection; Alt+click picks a single member of a group. "
                             "Right-drag orbits, middle-drag pans, the wheel zooms.");
    }
}

int pick(int mx, int my) {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f, cx = gVP.x + gVP.w * 0.5f, cy = gVP.y + hh;
    for (int i = 0; i < (int)gScene.objs.size(); i++) {
        const Object& o = gScene.objs[i];
        if (o.hidden || (!isLight(o.type) && !(gIn.alt && o.type == ObjType::Metaball))) continue;
        V3 c = vb.toCam(o.worldPos);
        if (!vb.ortho && c.z < 0.1f) continue;
        float d = vb.ortho ? 1.f : c.z;
        float sx = cx + c.x / (d * vb.k) * hh, sy = cy - c.y / (d * vb.k) * hh;
        if (std::fabs(sx - mx) < 10 && std::fabs(sy - my) < 10) return i;
    }
    Hit h;
    return intersectScene(gScene, vb.ray((mx - cx) / hh, -(my - cy) / hh), 1e30f, h) ? h.obj : -1;
}

// Nearest terrain/lattice under the cursor, ignoring water and other objects in front of it.
SculptHit sculptPick(int mx, int my) {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f, cx = gVP.x + gVP.w * 0.5f, cy = gVP.y + hh;
    Ray r = vb.ray((mx - cx) / hh, -(my - cy) / hh);
    SculptHit best;
    float bt = 1e30f;
    for (int i = 0; i < (int)gScene.objs.size(); i++) {
        const Object& o = gScene.objs[i];
        if (!isHeightfield(o.type) || !o.hf || o.hidden || o.locked) continue;
        Hit h;
        if (intersectObject(o, r, bt, h)) { bt = h.t; best = {i, clampf((h.lr.x + 1) * 0.5f, 0, 1), clampf((h.lr.z + 1) * 0.5f, 0, 1)}; }
    }
    return best;
}

BrushMode sculptMode() {
    if (gIn.shift) return BrushMode::Smooth;
    if (gIn.ctrl && (gBrush == BrushMode::Raise || gBrush == BrushMode::Peak || gBrush == BrushMode::Plateau)) return BrushMode::Lower;
    if (gIn.ctrl && gBrush == BrushMode::Lower) return BrushMode::Raise;
    return gBrush;
}

void sculptStep() {
    if (gSculpt.obj < 0 || gSculpt.obj >= (int)gScene.objs.size() || !gScene.objs[gSculpt.obj].hf) return;
    hfBrush(*gScene.objs[gSculpt.obj].hf, gSculpt.u, gSculpt.v, gBrushSize, gBrushStr * 0.03f, sculptMode());
    gShowRender = false;
    sceneChanged();
}

Ray viewportRay(const ViewBasis& view, int x, int y) {
    float half = gVP.h * .5f;
    return view.ray((x - gVP.x - gVP.w * .5f) / half, (gVP.y + half - y) / half);
}

void beginObjectDrag() {
    gDrag = DragObj;
    gTransform = {};
    auto roots = transformRoots();
    if (roots.empty()) { gStatus = "The selection is locked."; return; }
    gPlaying = false;
    gTransform.pivot = selectionPivot(gPivotMode, roots);
    gTransform.bottom = gTransform.pivot;
    gTransform.view = curBasis();
    const auto& view = gTransform.view;
    float depth = view.ortho ? 1.f : std::max(.5f, view.toCam(gScene.objs[gSel].worldPos).z);
    gTransform.unitsPerPixel = depth * view.k / (gVP.h * .5f);
    std::vector<bool> moving(gScene.objs.size(), false);
    for (int i : roots) {
        const auto& obj = gScene.objs[i];
        gTransform.objects.push_back({i, obj.pos, obj.rot, obj.scl, obj.worldPos, obj.parentInv});
        moving[i] = true;
    }
    if (gTool != 0 || gSnapMode != Snap::Surface) return;
    gTransform.surfaces = std::make_unique<Scene>(gScene);
    V3 totalLow(1e30f), totalHigh(-1e30f);
    for (int i = 0; i < (int)gScene.objs.size(); ++i) {
        for (int parent = gScene.objs[i].parent; !moving[i] && parent >= 0; parent = gScene.objs[parent].parent)
            if (moving[parent]) moving[i] = true;
        if (!moving[i]) continue;
        gTransform.surfaces->objs[i].hidden = true;
        V3 low, high;
        if (!gScene.objs[i].hidden && !isLight(gScene.objs[i].type) && worldBounds(gScene.objs[i], low, high))
            expandBounds(low, high, totalLow, totalHigh);
    }
    if (totalLow.x <= totalHigh.x)
        gTransform.bottom = {(totalLow.x + totalHigh.x) * .5f, totalLow.y, (totalLow.z + totalHigh.z) * .5f};
    gTransform.surfaces->updateAll();
}

void dragObject() {
    auto& drag = gTransform;
    if (drag.objects.empty()) return;
    float precision = gIn.shift ? .1f : 1.f;
    drag.dx += gIn.dx * precision;
    drag.dy += gIn.dy * precision;
    const auto& view = drag.view;
    V3 screenDelta = view.r * drag.dx - view.u * drag.dy;
    if (gTool == 0 && gAutoAxis && gTransformAxis < 0 && drag.autoAxis < 0 && length(screenDelta) >= 8.f) {
        float best = 0;
        for (int axis = 0; axis < 3; ++axis) {
            V3 unit = axisVector(axis);
            float projected = std::hypot(dot(unit, view.r), dot(unit, view.u));
            if (projected < .15f) continue;
            float score = std::fabs(dot(screenDelta, unit)) / projected;
            if (score > best) { best = score; drag.autoAxis = axis; }
        }
    }
    int axis = gTransformAxis >= 0 ? gTransformAxis : (gTool == 0 && gAutoAxis ? drag.autoAxis : -1);
    V3 unit = axisVector(axis);
    bool grid = gSnapMode == Snap::Grid || (gIn.ctrl && gSnapMode == Snap::Off);
    V3 move, angles, factors(1.f);
    M3 orbit;
    if (gTool == 0) {
        move = screenDelta * drag.unitsPerPixel;
        if (axis >= 0) {
            float projected = 1 - std::pow(dot(unit, view.f), 2.f);
            move = unit * (projected > .025f ? dot(move, unit) / projected : -drag.dy * drag.unitsPerPixel);
        }
        if (drag.surfaces && gVP.in(gIn.mx, gIn.my) && (drag.dx || drag.dy)) {
            Hit hit;
            if (intersectScene(*drag.surfaces, viewportRay(view, gIn.mx, gIn.my), 1e30f, hit))
                move = hit.p - drag.bottom;
        }
        if (grid && length(move) > 1e-6f) {
            V3 snapped = snapPoint(drag.pivot + move, gGridStep) - drag.pivot;
            move = snapped * V3(std::fabs(move.x) > 1e-6f ? 1.f : 0.f,
                                std::fabs(move.y) > 1e-6f ? 1.f : 0.f,
                                std::fabs(move.z) > 1e-6f ? 1.f : 0.f);
        }
        if (axis >= 0) move = unit * dot(move, unit);
    } else if (gTool == 1) {
        angles = axis < 0 ? V3(drag.dy * .5f, drag.dx * .5f, 0) :
                 unit * ((axis == 0 ? drag.dy : drag.dx) * .5f);
        if (grid) angles = snapPoint(angles, gAngleStep);
        orbit = rotY(rad(angles.y)) * rotX(rad(angles.x)) * rotZ(rad(angles.z));
    } else {
        float scale = std::exp(clampf(-drag.dy * .01f + drag.dx * .005f, -4.6f, 6.9f));
        if (grid) scale = std::max(.1f, snapValue(scale, .1f));
        factors = axis < 0 ? V3(scale) : V3(1.f) + unit * (scale - 1);
    }
    bool changed = false;
    for (const auto& start : drag.objects) {
        auto& obj = gScene.objs[start.index];
        V3 offset = start.world - drag.pivot;
        V3 delta = move, rotation = start.rotation, scale = start.scale;
        if (gTool == 1) {
            delta = orbit * offset - offset;
            if (obj.type == ObjType::Picture && obj.picture.billboard && axis < 0) rotation.z += angles.y;
            else rotation += angles;
        } else if (gTool == 2 && !isLight(obj.type)) {
            delta = offset * factors - offset;
            scale = scale * factors;
        }
        V3 position = start.position + start.worldToLocal * delta;
        changed |= length(position - obj.pos) > 1e-6f || length(rotation - obj.rot) > 1e-6f ||
                   length(scale - obj.scl) > 1e-6f;
        obj.pos = position;
        obj.rot = rotation;
        obj.scl = scale;
    }
    if (changed) sceneChanged();
}

void finishObjectDrag(bool cancel) {
    if (cancel) {
        for (const auto& start : gTransform.objects) {
            auto& obj = gScene.objs[start.index];
            obj.pos = start.position;
            obj.rot = start.rotation;
            obj.scl = start.scale;
        }
        if (gChanged) sceneChanged();
        gChanged = false;
        gStatus = "Transform canceled.";
    }
    endInteraction();
    gIn.down = false;
    gActive = 0;
    gDrag = DragNone;
    gTransform = {};
    capturePointer(gRDrag || gMDrag);
}

V3 lightAimTip(const Object& o) { return o.worldPos + o.lightAxis() * (o.type == ObjType::Spotlight ? 4.f : 5.f); }

void dragLightAim() {
    Object& o = gScene.objs[gSel];
    ViewBasis vb = curBasis();
    V3 tip = lightAimTip(o);
    float wpp = (vb.ortho ? 1.f : std::max(0.1f, vb.toCam(tip).z)) * vb.k / (gVP.h * 0.5f);
    V3 aim = normalize(o.parentInv * (tip - o.worldPos + (vb.r * (float)gIn.dx - vb.u * (float)gIn.dy) * wpp));
    if (length(aim) < 0.5f) return;
    o.rot.x = std::acos(clampf(-aim.y, -1, 1)) * 180 / PI;
    if (std::fabs(aim.x) + std::fabs(aim.z) > 1e-5f) o.rot.y = std::atan2(-aim.x, -aim.z) * 180 / PI;
    o.rot.z = 0;
    sceneChanged();
}

void drawViewport() {
    R vp = gVP;
    if (gShowRender && gMainR.hasImage()) {
        fill(vp, rgb(16, 16, 18));
        blitFit(gMainR, vp);
    } else {
        bool previewMode = gViewMode == Solid || gViewMode == Textured;
        vgrad(vp, rgb(53, 66, 75), rgb(127, 146, 150));
        if (previewMode) blitFit(gViewR, vp);
        ViewBasis vb = curBasis();
        float hh = vp.h * 0.5f, cx = vp.x + vp.w * 0.5f, cy = vp.y + hh;
        setClip(vp);
        Color pen = rgb(112, 114, 122);
        auto seg = [&](V3 a, V3 b) {
            V3 ca = vb.toCam(a), cb = vb.toCam(b);
            if (!vb.ortho) {
                const float nz = 0.1f;
                if (ca.z < nz && cb.z < nz) return;
                if (ca.z < nz) ca = lerp(ca, cb, (nz - ca.z) / (cb.z - ca.z));
                else if (cb.z < nz) cb = lerp(cb, ca, (nz - cb.z) / (ca.z - cb.z));
            }
            auto P = [&](V3 c, int& sx, int& sy) {
                float d = vb.ortho ? 1.f : c.z;
                sx = (int)clampf(cx + c.x / (d * vb.k) * hh, -30000, 30000);
                sy = (int)clampf(cy - c.y / (d * vb.k) * hh, -30000, 30000);
            };
            int x0, y0, x1, y1;
            P(ca, x0, y0);
            P(cb, x1, y1);
            float fade = gViewMode == DepthWire ? lerpf(1, 0.28f, clampf((ca.z + cb.z) / (vb.ortho ? 4400.f : 320.f), 0, 1)) : 1.f;
            line(x0, y0, x1, y1, rgb((unsigned)(red(pen) * fade), (unsigned)(green(pen) * fade), (unsigned)(blue(pen) * fade)));
        };
        if (!previewMode)
            for (int k = -10; k <= 10; k++) { seg(V3(k * 5.f, 0, -50), V3(k * 5.f, 0, 50)); seg(V3(-50, 0, k * 5.f), V3(50, 0, k * 5.f)); }
        bool transforming = gIn.down && gDrag == DragObj && !gTransform.objects.empty();
        int lockedAxis = gTransformAxis;
        if (gTool == 0 && gAutoAxis && transforming && lockedAxis < 0) lockedAxis = gTransform.autoAxis;
        std::vector<V3> s;
        auto drawObj = [&](int i) {
            const Object& o = gScene.objs[i];
            bool sel = isSel(i);
            Color c = isInfinite(o.type) ? rgb(58, 78, 122) : rgb(24, 42, 140);
            if (isLight(o.type)) c = sel ? rgb(255, 140, 0) : rgb(240, 220, 60);
            else if (sel) c = i == gSel ? rgb(225, 28, 28) : rgb(235, 90, 70);
            else if (o.boolMode == BoolMode::Negative || (o.type == ObjType::Metaball && o.metaNegative)) c = rgb(20, 110, 40);
            else if (o.boolMode == BoolMode::Intersect) c = rgb(120, 40, 140);
            else if (o.boolMode == BoolMode::Positive) c = rgb(50, 202, 194);
            else if (o.locked) c = rgb(70, 70, 76);
            pen = c;
            if (gViewMode == BoundsOnly || (o.instanceSource >= 0 && !sel)) {
                V3 lo, hi;
                if (!worldBounds(o, lo, hi)) return;
                auto corner = [&](int i) { return V3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z); };
                for (int k = 0; k < 8; ++k) for (int bit : {1, 2, 4})
                    if (!(k & bit)) seg(corner(k), corner(k | bit));
                return;
            }
            s.clear();
            wireframe(o, s);
            for (size_t j = 0; j + 1 < s.size(); j += 2) seg(s[j], s[j + 1]);
        };
        for (int i = 0; i < (int)gScene.objs.size(); i++)
            if (!previewMode && !isSel(i) && !gScene.objs[i].hidden) drawObj(i);
        for (int i : gSelSet)
            if (i < (int)gScene.objs.size()) drawObj(i);
        if (validSel()) {
            const Object& o = gScene.objs[gSel];
            pen = rgb(240, 180, 70);
            auto at = [&](const Key& k) { return o.worldPos + o.parentSpace * (k.pos - o.pos); };
            for (size_t i = 1; i < o.keys.size(); ++i) seg(at(o.keys[i - 1]), at(o.keys[i]));
        }
        if (gCameraPath && gScene.camKeys.size() >= 2) {
            pen = rgb(115, 224, 213);
            int first = gScene.camKeys.front().frame, last = gScene.camKeys.back().frame;
            V3 previous = gScene.cameraAt((float)first).eye();
            int count = std::clamp(last - first, 1, 128);
            for (int i = 1; i <= count; ++i) {
                V3 next = gScene.cameraAt(lerpf((float)first, (float)last, (float)i / count)).eye();
                seg(previous, next);
                previous = next;
            }
        }
        if (validSel() && gTool != TOOL_SCULPT && !gScatter) {
            const Object& selected = gScene.objs[gSel];
            R aim{};
            if (!selected.hidden && !selected.locked && (selected.type == ObjType::Spotlight || selected.type == ObjType::Parallel)) {
                V3 tip = lightAimTip(selected), c = vb.toCam(tip);
                float d = vb.ortho ? 1.f : c.z;
                if (d > 0.1f) {
                    int x = (int)clampf(cx + c.x / (d * vb.k) * hh, -30000, 30000);
                    int y = (int)clampf(cy - c.y / (d * vb.k) * hh, -30000, 30000);
                    pen = rgb(255, 218, 120);
                    seg(selected.worldPos, tip);
                    if (vp.in(x, y) && vp.w >= 38 && vp.h >= 20) {
                        aim = {std::clamp(x - 19, vp.x, vp.x + vp.w - 38), std::clamp(y - 10, vp.y, vp.y + vp.h - 20), 38, 20};
                        bool hover = hov(aim);
                        if (hover && gIn.pressed && gActive == 0) { gActive = VIEW_ID; gDrag = DragLightAim; }
                        fill(aim, hover ? rgb(120, 88, 37) : rgb(63, 52, 34));
                        bevel(aim, gActive == VIEW_ID && gDrag == DragLightAim);
                        textIn(aim, "Aim", TXT, gSmall);
                    }
                }
            }
            V3 lo, hi;
            bool bounded = sceneBounds(true, lo, hi);
            V3 center = bounded ? (lo + hi) * 0.5f : gScene.objs[gSel].worldPos;
            V3 c = vb.toCam(center);
            float depth = vb.ortho ? 1.f : c.z;
            if (depth > 0.1f) {
                V3 corner[8];
                pen = rgb(255, 218, 120);
                if (bounded) {
                    for (int i = 0; i < 8; i++) corner[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
                    for (int i = 0; i < 8; i++)
                        for (int bit : {1, 2, 4})
                            if (!(i & bit)) seg(corner[i], corner[i | bit]);
                }
                float screenX = cx + c.x / (depth * vb.k) * hh, screenY = cy - c.y / (depth * vb.k) * hh;
                float bottom = screenY;
                if (bounded) for (V3 p : corner) {
                    V3 q = vb.toCam(p);
                    float d = vb.ortho ? 1.f : q.z;
                    if (d > 0.1f) bottom = std::max(bottom, cy - q.y / (d * vb.k) * hh);
                }
                if (screenX >= vp.x && screenX < vp.x + vp.w && screenY >= vp.y && screenY < vp.y + vp.h) {
                    const int w = 74, gap = 4;
                    int left = std::clamp((int)screenX - (w * 3 + gap * 2) / 2, vp.x + 4, std::max(vp.x + 4, vp.x + vp.w - 3 * (w + gap)));
                    int y = std::clamp((int)clampf(bottom, 0, (float)(vp.y + vp.h)) + 9,
                                       vp.y + 30, std::max(vp.y + 30, vp.y + vp.h - 53));
                    if (aim.w && left < aim.x + aim.w && left + 3 * (w + gap) > aim.x && y < aim.y + aim.h && y + 47 > aim.y) {
                        if (aim.y - 51 >= vp.y + 30) y = aim.y - 51;
                        else if (aim.y + 24 <= vp.y + vp.h - 53) y = aim.y + 24;
                    }
                    const char* tools[] = {"Move", "Rotate", "Scale"};
                    bool lightsOnly = std::all_of(gSelSet.begin(), gSelSet.end(), [](int i) { return isLight(gScene.objs[i].type); });
                    for (int i = 0; i < 3; i++) {
                        if (i == 2 && lightsOnly) continue;
                        R handle{left + i * (w + gap), y, w, 22};
                        if (hov(handle) && gIn.pressed && gActive == 0) {
                            gActive = VIEW_ID;
                            gTool = i;
                            beginObjectDrag();
                        }
                        bool active = gActive == VIEW_ID && gDrag == DragObj && gTool == i;
                        vgrad(handle, hov(handle) || active ? rgb(103, 105, 137) : rgb(78, 80, 103), rgb(43, 45, 59));
                        bevel(handle, active);
                        textIn(handle, tools[i], TXT, gSmall);
                    }
                    const Object& o = gScene.objs[gSel];
                    y += 25;
                    if (button({left, y, w, 22}, "A Attributes", false, gSmall)) openLab(LabAttributes);
                    if (!isLight(o.type) && button({left + w + gap, y, w, 22}, "M Material", false, gSmall)) openLab(LabMat);
                    Lab editor = objectEditor(o);
                    if (editor != LabNone && button({left + 2 * (w + gap), y, w, 22}, "E Edit", false, gSmall)) openLab(editor);
                }
            }
        }
        if (gTool == TOOL_SCULPT) {
            gSculptHover = gDrag == DragSculpt && gIn.down ? gSculpt : (hov(vp) ? sculptPick(gIn.mx, gIn.my) : SculptHit());
            if (gSculptHover.obj >= 0) {
                const Object& o = gScene.objs[gSculptHover.obj];
                const Heightfield& f = *o.hf;
                auto H = [&](float x, float z) {
                    float sx = clampf((x + 1) * 0.5f, 0, 1) * (f.res - 1), sz = clampf((z + 1) * 0.5f, 0, 1) * (f.res - 1);
                    int ix = std::min((int)sx, f.res - 2), iz = std::min((int)sz, f.res - 2);
                    float fx = sx - ix, fz = sz - iz;
                    return lerpf(lerpf(f.at(ix, iz), f.at(ix + 1, iz), fx), lerpf(f.at(ix, iz + 1), f.at(ix + 1, iz + 1), fx), fz);
                };
                BrushMode bm = sculptMode();
                pen = bm == BrushMode::Lower ? rgb(80, 200, 255) : (bm == BrushMode::Smooth ? rgb(140, 255, 140) : rgb(255, 150, 30));
                float cxl = gSculptHover.u * 2 - 1, czl = gSculptHover.v * 2 - 1;
                for (float rr : {1.f, 0.5f}) {
                    float R2 = gBrushSize * 2 * rr;
                    V3 prev;
                    for (int k = 0; k <= 40; k++) {
                        float a = 2 * PI * k / 40, x = cxl + std::cos(a) * R2, z = czl + std::sin(a) * R2;
                        V3 p = o.worldPos + o.fwd * V3(x, H(x, z) + 0.004f, z);
                        if (k) seg(prev, p);
                        prev = p;
                    }
                }
                V3 c0 = o.worldPos + o.fwd * V3(cxl, H(cxl, czl), czl), up = normalize(o.fwd * V3(0, 1, 0));
                seg(c0, c0 + up * (o.scl.y * 0.25f + 0.5f));
            }
        }
        auto marker = [&](V3 world, Color color, int size) {
            V3 q = vb.toCam(world);
            float depth = vb.ortho ? 1.f : q.z;
            if (depth < .1f) return;
            float x = cx + q.x * hh / (depth * vb.k), y = cy - q.y * hh / (depth * vb.k);
            if (x < vp.x || x >= vp.x + vp.w || y < vp.y || y >= vp.y + vp.h) return;
            circle((int)x, (int)y, size - 2, color);
            line((int)x - size, (int)y, (int)x + size, (int)y, color);
            line((int)x, (int)y - size, (int)x, (int)y + size, color);
        };
        if (validSel() && gTool != TOOL_SCULPT && (gTab == 1 || transforming)) {
            V3 pivot = transforming ? gTransform.pivot : selectionPivot(gPivotMode, transformRoots());
            if (lockedAxis >= 0 && gTool == 0) {
                const Color colors[] = {rgb(255, 122, 116), rgb(120, 241, 150), rgb(119, 185, 255)};
                pen = colors[lockedAxis];
                V3 span = axisVector(lockedAxis) * (vb.k * (vb.ortho ? 1.f : std::max(.5f, vb.toCam(pivot).z)) * 2.f);
                seg(pivot - span, pivot + span);
            }
            marker(pivot, rgb(254, 223, 116), 6);
        }
        if (gCursorVisible) marker(gCursor, rgb(142, 245, 255), 10);
        clearClip();
        const char* vn[4] = {"Camera View", "Top View", "Front View", "Side View"};
        const char* tn[4] = {"Move", "Rotate", "Resize", "Sculpt Terrain"};
        char buf[160];
        const char* snap = gSnapMode == Snap::Grid ? "   |   Grid snap" : gSnapMode == Snap::Surface ? "   |   Surface snap" : "";
        char axisText[24] = {};
        if (gTool != TOOL_SCULPT && lockedAxis >= 0) snprintf(axisText, sizeof axisText, "   |   Axis %c", "XYZ"[lockedAxis]);
        else if (gTool == 0 && gAutoAxis) snprintf(axisText, sizeof axisText, "   |   Auto axis");
        snprintf(buf, sizeof buf, "%s   |   Tool: %s   |   Frame %d%s%s", vn[gView], tn[gTool], gScene.frame, axisText, gTool == TOOL_SCULPT ? "" : snap);
        textIn({vp.x + 10, vp.y + 6, vp.w - 20, 18}, buf, rgb(240, 240, 245), gBold, TextLeft | TextMiddle);
    }
    if (gSafeFrame) {
        float ratio = gScene.render.width > 0 && gScene.render.height > 0 ? gScene.render.width / (float)gScene.render.height : 16.f / 9;
        int w = std::min(vp.w - 20, (int)((vp.h - 20) * ratio)), h = (int)(w / ratio);
        int x = vp.x + (vp.w - w) / 2, y = vp.y + (vp.h - h) / 2;
        Color c = rgb(240, 190, 106);
        line(x, y, x + w, y, c); line(x + w, y, x + w, y + h, c);
        line(x + w, y + h, x, y + h, c); line(x, y + h, x, y, c);
        if (gScene.render.region) {
            const auto& crop = gScene.render;
            int x0 = x + (int)(w * crop.cropX), x1 = std::min(x + w, x + (int)(w * (crop.cropX + crop.cropW)));
            int y0 = y + (int)(h * crop.cropY), y1 = std::min(y + h, y + (int)(h * (crop.cropY + crop.cropH)));
            Color region = rgb(111, 228, 197);
            line(x0, y0, x1, y0, region); line(x1, y0, x1, y1, region);
            line(x1, y1, x0, y1, region); line(x0, y1, x0, y0, region);
        }
    }
    if (button({vp.x + vp.w - 100, vp.y + 5, 94, 22}, "Display", gDisplayPalette, gSmall)) gDisplayPalette = !gDisplayPalette;
    R palette{vp.x + vp.w - 198, vp.y + 34, 190, 142};
    if (gDisplayPalette) {
        int x = palette.x, y = palette.y;
        vgrad(palette, rgb(55, 75, 82), rgb(36, 51, 59));
        bevel(palette, false);
        const char* modes[] = {"Wire", "Depth Wire", "Bounds", "Solid", "Texture", "Render"};
        for (int i = 0; i < 6; ++i) {
            if (!button({x + 5 + (i % 2) * 92, y + 6 + (i / 2) * 26, 88, 22}, modes[i],
                        i == 5 ? gShowRender : (!gShowRender && gViewMode == i), gSmall)) continue;
            if (i == 5) {
                if (gMainR.hasImage()) gShowRender = true;
                else actRender();
            } else {
                gViewMode = (ViewMode)i;
                gShowRender = false;
                gViewPreviewDirty = true;
            }
        }
        toggle({x + 5, y + 84, 180, 22}, "Safe Frame", gSafeFrame);
        if (slider({x + 5, y + 112, 180, 22}, "Preview Scale", gPreviewScale, 0.2f, 0.8f)) gViewPreviewDirty = true;
    }
    line(vp.x, vp.y, vp.x + vp.w, vp.y, rgb(28, 28, 32));

    bool overCanvas = hov(vp) && !(gDisplayPalette && hov(palette));
    if (gScatter && overCanvas && !gBlocked && !gShowRender) {
        setClip(vp);
        circle(gIn.mx, gIn.my, (int)gScatterRadius, rgb(245, 208, 120));
        clearClip();
    }
    if (overCanvas && gIn.pressed && gActive == 0) {
        gActive = VIEW_ID;
        SculptHit sh = gTool == TOOL_SCULPT && !gShowRender ? sculptPick(gIn.mx, gIn.my) : SculptHit();
        if (gShowRender) { gShowRender = false; gDrag = DragNone; }
        else if (gScatter) {
            gDrag = DragScatter;
            gScatterX = gIn.mx;
            gScatterY = gIn.my;
        } else if (sh.obj >= 0) {
            if (!isSel(sh.obj) || gSel != sh.obj) selectObj(sh.obj, false);
            gSculpt = sh;
            gDrag = DragSculpt;
            gSculptLast = nowMs();
            sculptStep();
            gStatus = "Sculpting " + gScene.objs[sh.obj].name + ": hold to build up, Ctrl inverts, Shift smooths, [ ] resize brush.";
        } else {
            int p = pick(gIn.mx, gIn.my);
            if (p >= 0 && !gIn.shift && (gIn.alt || !isSel(p))) selectObj(p, false, gIn.alt);
            else if (p >= 0 && gIn.shift) selectObj(p, true, gIn.alt);
            else if (p >= 0) gSel = p;
            else if (!gIn.shift) clearSel();
            if (p >= 0) gStatus = "Selected " + gScene.objs[p].name + (gSelSet.size() > 1 ? " (+" + std::to_string(gSelSet.size() - 1) + " more)." : ".");
            if (p >= 0 && isSel(p) && gTool != TOOL_SCULPT) beginObjectDrag();
            else gDrag = DragCam;
        }
    }
    if (gActive == VIEW_ID && gIn.down && gDrag == DragScatter && overCanvas &&
        (gIn.pressed || std::hypot((float)(gIn.mx - gScatterX), (float)(gIn.my - gScatterY)) >= gScatterRadius * .45f)) {
        if (!validSel()) { gStatus = "Select a receiving surface."; return; }
        gScatterX = gIn.mx;
        gScatterY = gIn.my;
        ViewBasis view = curBasis();
        float half = vp.h * .5f, cx = vp.x + vp.w * .5f, cy = vp.y + half;
        Rng rng((uint32_t)gScatterSeed + 2654435761u * (uint32_t)gScatterDab++);
        std::vector<Ray> rays;
        for (int i = 0; i < (int)gScatterDensity; ++i) {
            float radius = i ? gScatterRadius * std::sqrt(rng.f()) : 0;
            float angle = rng.f() * 2 * PI;
            float x = gIn.mx + radius * std::cos(angle), y = gIn.my + radius * std::sin(angle);
            if (vp.in((int)x, (int)y)) rays.push_back(view.ray((x - cx) / half, (cy - y) / half));
        }
        if (gScatterErase || gIn.shift) {
            std::vector<int> keep;
            std::set<int> groups;
            std::vector<bool> erase(gScene.objs.size());
            for (size_t i = 0; i < gScene.objs.size(); ++i) {
                const auto& obj = gScene.objs[i];
                if (obj.instanceSource < 0 || obj.parent != gSel || obj.locked || isSel((int)i)) continue;
                V3 p = view.toCam(obj.worldPos);
                if (!view.ortho && p.z <= 0) continue;
                float scale = half / (view.k * (view.ortho ? 1 : p.z));
                if (std::hypot(cx + p.x * scale - gIn.mx, cy - p.y * scale - gIn.my) <= gScatterRadius) {
                    erase[i] = true;
                    if (obj.group > 0) groups.insert(obj.group);
                }
            }
            std::vector<int> remap(erase.size(), -1);
            for (size_t i = 0; i < erase.size(); ++i) {
                const auto& obj = gScene.objs[i];
                if (erase[i] || (obj.instanceSource >= 0 && obj.parent == gSel && !obj.locked &&
                                 !isSel((int)i) && groups.count(obj.group))) continue;
                remap[i] = (int)keep.size();
                keep.push_back((int)i);
            }
            if (keep.size() != erase.size()) {
                gScene = gScene.subset(keep);
                gSel = remap[gSel];
                for (int& i : gSelSet) i = remap[i];
                sceneChanged();
            }
            gStatus = "Erased " + std::to_string(erase.size() - keep.size()) + " scatter instances.";
        } else {
            auto paint = [&](const auto& sources, const auto& sampleRays) {
                return gScene.scatter(sources, gSel, sampleRays, std::min(gScatterMin, gScatterMax),
                                      std::max(gScatterMin, gScatterMax), gScatterYaw, gScatterAlign, rng);
            };
            int made = 0;
            if (gScatterPalette) {
                std::map<int, std::vector<int>> species;
                for (int i : sortedSel()) if (i != gSel) {
                    int group = gScene.objs[i].group;
                    species[group > 0 ? group : -i - 1].push_back(i);
                }
                std::vector<std::vector<Ray>> samples(species.size());
                if (!samples.empty()) for (const Ray& ray : rays) samples[rng.u() % samples.size()].push_back(ray);
                size_t i = 0;
                for (const auto& entry : species) {
                    if (!samples[i].empty()) made += paint(entry.second, samples[i]);
                    ++i;
                }
            } else made = paint(sortedSel(), rays);
            if (made) sceneChanged();
            gStatus = made ? "Painted " + std::to_string(made) + " instances. Shift erases. Esc stops." :
                            "No instances placed. Select sources, then Shift-click the receiving surface last.";
        }
    } else if (gActive == VIEW_ID && gIn.down && gDrag == DragSculpt && (gIn.dx || gIn.dy)) {
        SculptHit sh = sculptPick(gIn.mx, gIn.my);
        if (sh.obj == gSculpt.obj) gSculpt = sh;
        uint64_t now = nowMs();
        if (now - gSculptLast >= 30) { gSculptLast = now; sculptStep(); }
    } else if (gActive == VIEW_ID && gIn.down && (gIn.dx || gIn.dy)) {
        if (gDrag == DragObj && validSel()) dragObject();
        else if (gDrag == DragLightAim && validSel()) dragLightAim();
        else if (gDrag == DragCam) {
            if (gView == 0) orbitCam((float)gIn.dx, (float)gIn.dy);
            else panView((float)gIn.dx, (float)gIn.dy);
        }
    }
}

void drawTimeline() {
    R tl{0, gH - STATUS - TIMELINE, gW, TIMELINE};
    vgrad(tl, rgb(78, 78, 86), rgb(52, 52, 58));
    line(0, tl.y, gW, tl.y, rgb(28, 28, 32));
    int y = tl.y + 5, x = 8, last = gScene.frames - 1;
    if (button({x, y, 28, 24}, "|<")) setFrame(0);
    if (button({x + 30, y, 28, 24}, "<")) jumpKey(-1);
    if (button({x + 60, y, 50, 24}, gPlaying ? "Stop" : "Play", gPlaying)) togglePlayback();
    if (button({x + 112, y, 28, 24}, ">")) jumpKey(1);
    if (button({x + 142, y, 28, 24}, ">|")) setFrame(last);
    if (button({x + 176, y, 62, 24}, "Motion")) openLab(LabMotion);
    int rx = gW - 600;
    R tr{x + 244, y, rx - x - 250, 24};
    int id = newId();
    press(tr, id);
    fill(tr, rgb(36, 36, 42));
    bevel(tr, true);
    auto fx = [&](int f) { return tr.x + 4 + (int)((tr.w - 8) * (f / (float)std::max(1, last))); };
    for (int f = 0; f <= last; f += std::max(1, gScene.fps)) line(fx(f), tr.y + 16, fx(f), tr.y + 22, rgb(110, 110, 120));
    for (int f : keyFrames(true)) fill({fx(f) - 2, tr.y + 2, 5, 6}, rgb(80, 200, 230));
    for (int f : keyFrames(false)) fill({fx(f) - 3, tr.y + 8, 7, 8}, rgb(250, 210, 60));
    fill({fx(gScene.frame) - 1, tr.y + 1, 3, tr.h - 2}, rgb(230, 60, 50));
    if (gActive == id && gIn.down) {
        int f = (int)std::lround((gIn.mx - tr.x - 4) / (float)std::max(1, tr.w - 8) * last);
        if (f != gScene.frame) setFrame(f);
    }
    char b[48];
    snprintf(b, sizeof b, "Frame %d / %d", gScene.frame, last);
    textIn({rx, y, 96, 24}, b, TXT, gSmall);
    rx += 100;
    if (button({rx, y, 56, 24}, "Key")) addKey();
    if (button({rx + 58, y, 64, 24}, "Del Key")) delKey();
    if (button({rx + 124, y, 64, 24}, "Key Sky")) { putKey(gScene.skyKeys, SkyKey{gScene.frame, gScene.sky}); gChanged = true; gStatus = "Sky key set at frame " + std::to_string(gScene.frame) + "."; }
    toggle({rx + 190, y, 70, 24}, "Auto Key", gAutoKey);
    if (dragI({rx + 262, y, 84, 24}, "Frames", gScene.frames, 0.5f, 2, 10000)) { gChanged = true; if (gScene.frame > gScene.frames - 1) setFrame(gScene.frames - 1); }
    if (dragI({rx + 348, y, 56, 24}, "FPS", gScene.fps, 0.2f, 1, 60)) gChanged = true;
    if (button({rx + 406, y, 86, 24}, "Render Anim")) defer(actRenderAnim);
}

void drawStatus() {
    R sb{0, gH - STATUS, gW, STATUS};
    vgrad(sb, rgb(70, 70, 76), rgb(44, 44, 48));
    line(0, sb.y, gW, sb.y, rgb(28, 28, 32));
    if (gHoverTip != gLastTip) { gLastTip = gHoverTip; gTipSince = nowMs(); }
    bool tip = !gHoverTip.empty() && !gIn.down && nowMs() - gTipSince > 450;
    textIn({10, sb.y, gW - 360, sb.h}, tip ? gHoverTip.c_str() : gStatus.c_str(), TXT, gFont, TextLeft | TextMiddle);
    if (gMainR.busy() || gAnimRender || !gQueue.empty()) {
        R pb{gW - 330, sb.y + 5, 320, 14};
        fill(pb, rgb(30, 30, 34));
        vgrad({pb.x, pb.y, (int)(pb.w * gMainR.progress()), pb.h}, rgb(120, 200, 120), rgb(60, 130, 60));
        char buf[96];
        if (gAnimRender) snprintf(buf, sizeof buf, "Frame %d/%d  %d%%", std::max(gAnimFrame, 0), gScene.frames - 1, (int)(gMainR.progress() * 100));
        else {
            float progress = gMainR.progress();
            if (progress > 0.02f && progress < 1)
                snprintf(buf, sizeof buf, "Rendering %d%%   ~%.0f s left", (int)(progress * 100),
                         (nowMs() - gRenderStart) / 1000.f * (1 - progress) / progress);
            else snprintf(buf, sizeof buf, "Rendering %d%%", (int)(progress * 100));
        }
        textIn(pb, buf, rgb(255, 255, 255), gSmall);
    } else
        textIn({gW - 600, sb.y, 590, sb.h}, "Shift+click multi-select   RMB orbit   MMB pan   Wheel zoom   Ctrl+Z undo   Ctrl+R render", TXT_DIM, gSmall, TextRight | TextMiddle);
}

// ---------------------------------------------------------------- labs

R panel(int w, int h, const std::string& title) {
    R P{(gW - w) / 2, (gH - h) / 2, w, h};
    vgrad(P, rgb(96, 96, 102), rgb(62, 62, 68));
    bevel(P, false);
    R t{P.x + 2, P.y + 2, P.w - 4, 30};
    vgrad(t, rgb(74, 86, 126), rgb(44, 52, 82));
    textIn({t.x + 12, t.y, t.w - 24, t.h}, title.c_str(), rgb(255, 255, 255), gTitle, TextLeft | TextMiddle);
    return P;
}

void labFooter(R P, bool cancelOnly = false) {
    if (!cancelOnly && button({P.x + P.w - 196, P.y + P.h - 40, 88, 28}, "OK", false, gBold)) closeLab(false);
    if (button({P.x + P.w - 100, P.y + P.h - 40, 88, 28}, cancelOnly ? "Close" : "Cancel", false, gBold)) closeLab(true);
}

void drawDxfLab() {
    R p = panel(420, 246, "Import DXF");
    int x = p.x + 20, y = p.y + 46, w = p.w - 40;
    label(x, y, baseName(gDxfPath).c_str(), w);
    slider({x, y + 24, w, 24}, "World Units per DXF Unit", gDxfScale, 0.001f, 1000, "%.3f", true);
    toggle({x, y + 56, w, 24}, "Convert Z-Up to Y-Up", gDxfZUp);
    toggle({x, y + 88, w, 24}, "One Object per Layer", gDxfLayers);
    if (button({x + w - 198, p.y + p.h - 40, 88, 28}, "Import")) {
        std::vector<MeshLayer> layers;
        std::string error;
        if (!loadDXF(gDxfPath, layers, gDxfScale, gDxfZUp, gDxfLayers, error)) {
            gStatus = "Could not import DXF: " + error;
            return;
        }
        int first = (int)gScene.objs.size(), group = layers.size() > 1 ? gScene.maxGroup() + 1 : 0;
        for (auto& part : layers) {
            Object o = makeObject(ObjType::Mesh, ++gCounter[(int)ObjType::Mesh]);
            o.name = gDxfLayers ? part.name : baseName(gDxfPath);
            o.group = group;
            o.mesh = std::make_shared<Mesh>(std::move(part.mesh));
            gScene.objs.push_back(std::move(o));
        }
        selectObj(first, false);
        gShowRender = false;
        sceneChanged();
        closeLab(false);
        gStatus = "Imported " + std::to_string(layers.size()) + " DXF object(s).";
        return;
    }
    if (button({x + w - 88, p.y + p.h - 40, 88, 28}, "Cancel")) closeLab(true);
}

void drawMotionLab() {
    Object* object = validSel() ? &gScene.objs[gSel] : nullptr;
    R p = panel(548, 416, std::string("Motion Editor  -  ") + (object ? object->name : "Camera"));
    int x = p.x + 16, y = p.y + 48, w = p.w - 32;
    int frame = gScene.frame;
    if (sliderI({x, y, w, 24}, "Frame", frame, 0, gScene.frames - 1)) setFrame(frame);
    y += 32;
    bool terrain = object && isHeightfield(object->type) && object->hf;
    if (object) {
        const char* label = terrain && gTerrainTrack ? "Terrain Shape Track" : (gAppearanceTrack ? "Appearance Track" : "Transform Track");
        if (button({x, y, 174, 24}, label)) {
            if (terrain && gTerrainTrack) gTerrainTrack = gAppearanceTrack = false;
            else if (terrain && gAppearanceTrack) gTerrainTrack = true;
            else gAppearanceTrack = !gAppearanceTrack;
            gMotionKey = 0;
        }
    }
    bool terrainTrack = terrain && gTerrainTrack;
    bool appearance = object && gAppearanceTrack;
    if (button({x + 184, y, 132, 24}, "Set Key Here")) {
        if (terrainTrack) {
            auto& keys = object->terrainKeys;
            bool replace = std::any_of(keys.begin(), keys.end(), [frame](const auto& key) { return key.frame == frame; });
            if (keys.size() < 32 || replace) {
                auto height = std::make_shared<Heightfield>(*object->hf);
                if (height->res > 1025) hfResample(*height, 1025);
                height->n.clear();
                putKey(keys, TerrainKey{frame, std::move(height)});
                gStatus = "Captured the base heightfield at this frame.";
            } else gStatus = "Remove a terrain shape key before adding another.";
        } else if (appearance) putKey(object->appearances, object->appearance(frame));
        else if (object) putKey(object->keys, Key{frame, object->pos, object->rot, object->scl});
        else putKey(gScene.camKeys, CamKey{frame, gScene.cam});
        sceneChanged();
    }
    if (terrainTrack && button({x + 322, y, 190, 24}, "Load Heightmap Here...")) {
        int selected = gSel;
        defer([selected, frame] {
            auto& obj = gScene.objs[selected];
            auto& keys = obj.terrainKeys;
            bool replace = std::any_of(keys.begin(), keys.end(), [frame](const auto& key) { return key.frame == frame; });
            if (keys.size() >= 32 && !replace) { gStatus = "Remove a terrain shape key before adding another."; return; }
            std::string path;
            if (!fileDialog(false, IMAGE_FILTER, "pgm", path)) return;
            auto source = loadImageCached(path, true);
            if (!source) { gStatus = "Could not load the heightmap."; return; }
            auto field = std::make_shared<Heightfield>();
            hfInit(*field, std::min(obj.hf->res, 1025));
            hfFromImage(*field, *source);
            field->n.clear();
            putKey(keys, TerrainKey{frame, std::move(field)});
            sceneChanged();
        });
    }
    y += 34;
    auto track = [&](auto& keys, const auto& edit) {
        if (keys.empty()) return;
        gMotionKey = std::clamp(gMotionKey, 0, (int)keys.size() - 1);
        if (button({x, y, 30, 24}, "<") && gMotionKey > 0) --gMotionKey;
        if (button({x + 34, y, 30, 24}, ">") && gMotionKey + 1 < (int)keys.size()) ++gMotionKey;
        auto& k = keys[gMotionKey];
        std::string at = "Go to Key " + std::to_string(k.frame);
        if (button({x + 68, y, 130, 24}, at.c_str())) setFrame(k.frame);
        std::string mode = std::string("Ease ") + easeName(k.ease);
        if (button({x + 202, y, 142, 24}, mode.c_str())) {
            k.ease = (Ease)(((int)k.ease + 1) % (int)Ease::COUNT);
            gScene.applyFrame(gScene.frame);
            sceneChanged();
        }
        if (button({x + 348, y, 126, 24}, "Remove Key")) {
            keys.erase(keys.begin() + gMotionKey);
            gScene.applyFrame(gScene.frame);
            sceneChanged();
            return;
        }
        y += 36;
        int newFrame = k.frame;
        if (sliderI({x, y, w, 24}, "Key Timing", newFrame, 0, gScene.frames - 1) &&
            std::none_of(keys.begin(), keys.end(), [newFrame](const auto& other) { return other.frame == newFrame; })) {
            auto moved = k;
            moved.frame = newFrame;
            keys.erase(keys.begin() + gMotionKey);
            putKey(keys, moved);
            gMotionKey = (int)(std::lower_bound(keys.begin(), keys.end(), newFrame,
                 [](const auto& point, int f) { return point.frame < f; }) - keys.begin());
            gScene.applyFrame(gScene.frame);
            sceneChanged();
            return;
        }
        y += 29;
        if (edit(k)) { gScene.applyFrame(gScene.frame); sceneChanged(); }
    };
    if (terrainTrack) {
        track(object->terrainKeys, [&](TerrainKey&) {
            note({x, y, w, 58}, "Capture the base heightfield or load a heightmap at two or more frames to animate the terrain.");
            return false;
        });
    } else if (appearance) {
        track(object->appearances, [&](AppearanceKey& k) {
            bool changed = toggle({x, y, 128, 24}, "Hidden", k.hidden);
            if (isLight(object->type)) changed |= slider({x + 134, y, 240, 24}, "Light Power", k.light[0], 0, 5);
            else if (isHeightfield(object->type)) changed |= slider({x + 134, y, 240, 24}, "Terrain Height", k.terrain.height, 0.05f, 5);
            else changed |= slider({x + 134, y, 240, 24}, "Transparency", k.material.transparency, 0, 1);
            note({x, y + 34, w, 58}, "Edit materials, pictures, terrain or lights at the target frame, then set an appearance key here.");
            return changed;
        });
    } else if (object) {
        track(object->keys, [&](Key& k) {
            bool ch = row3(x, y, w, "Path Point", k.pos, 0.05f);
            ch |= row3(x, y, w, "Rotation", k.rot, 0.5f);
            ch |= row3(x, y, w, "Scale", k.scl, 0.03f, 0.01f);
            return ch;
        });
    } else {
        track(gScene.camKeys, [&](CamKey& k) {
            bool ch = row3(x, y, w, "Camera Target", k.cam.target, 0.05f);
            ch |= slider({x, y, w, 24}, "Orbit Distance", k.cam.dist, 1, 500, "%.1f", true);
            return ch;
        });
    }
    labFooter(p);
}

void drawCamerasLab() {
    R p = panel(700, 448, "Cameras");
    int x = p.x + 20, y = p.y + 46;
    if (button({x, y, 206, 24}, "Add Camera") && gScene.cameras.size() < 128) {
        std::string name = "Camera " + std::to_string(gScene.cameras.size() + 1);
        gScene.cameras.push_back({name, gScene.cam, gScene.camKeys});
        gScene.selectCamera((int)gScene.cameras.size() - 1);
        sceneChanged();
    }
    y += 29;
    int active = gScene.activeCamera, n = (int)gScene.cameras.size();
    if (n > 0) {
        if (button({x, y, 98, 22}, "Previous")) { gScene.selectCamera((active + n - 1) % n); sceneChanged(); }
        if (button({x + 106, y, 100, 22}, "Next")) { gScene.selectCamera((active + 1) % n); sceneChanged(); }
    }
    y += 30;
    int first = std::max(0, active - 6);
    for (int i = first; i < std::min(n, first + 7); ++i)
        if (button({x, y + (i - first) * 26, 206, 22}, gScene.cameras[i].name.c_str(), active == i)) {
            gScene.selectCamera(i);
            gView = 0;
            gShowRender = false;
            sceneChanged();
        }
    if (active >= 0 && button({x, p.y + 340, 206, 24}, "Remove Active Camera")) {
        gScene.cameras.erase(gScene.cameras.begin() + active);
        gScene.activeCamera = -1;
        if (!gScene.cameras.empty()) gScene.selectCamera(std::min(active, (int)gScene.cameras.size() - 1));
        sceneChanged();
    }
    if (gScene.activeCamera >= 0) {
        const auto& name = gScene.cameras[gScene.activeCamera].name;
        auto caption = gEditObj == -3 ? gEditBuf + "|" : "Rename Camera";
        if (button({x, p.y + 368, 206, 24}, caption.c_str(), gEditObj == -3)) { gEditObj = -3; gEditBuf = name; }
    }
    Camera& c = gScene.cam;
    int cx = p.x + 248, w = 430;
    y = p.y + 46;
    bool ch = toggle({cx, y, 212, 24}, "Orthographic Camera", c.orthographic);
    toggle({cx + 216, y, 212, 24}, "Show Safe Frame", gSafeFrame);
    y += 30;
    if (c.orthographic) ch |= slider({cx, y, w, 24}, "Ortho Scale", c.orthoScale, 0.1f, 500, "%.2f", true);
    else ch |= slider({cx, y, w, 24}, "Field of View", c.fov, 15, 120, "%.0f");
    y += 28;
    const int fovs[] = {24, 35, 50, 75};
    for (int i = 0; i < 4; ++i) {
        auto text = std::to_string(fovs[i]) + " deg";
        if (button({cx + i * 107, y, 104, 22}, text.c_str(), c.fov == fovs[i])) { c.fov = (float)fovs[i]; c.orthographic = false; ch = true; }
    }
    y += 30;
    row3(cx, y, w, "Camera Target", c.target, 0.08f);
    ch |= slider({cx, y, 212, 24}, "Yaw", c.yaw, -180, 180, "%.0f");
    ch |= slider({cx + 216, y, 212, 24}, "Pitch", c.pitch, -89, 89, "%.0f");
    y += 28;
    ch |= slider({cx, y, w, 24}, "Distance", c.dist, 1, 5000, "%.1f", true); y += 28;
    if (button({cx, y, 212, 24}, "Look at Selected Object") && validSel()) { c.lookAt = gSel; ch = true; }
    if (button({cx + 216, y, 212, 24}, "Clear Look-at")) { c.lookAt = -1; ch = true; }
    y += 28;
    if (gView != 0 && button({cx, y, w, 24}, "Match Editor View")) {
        c.target = gOrthoC[gView];
        c.dist = 2000;
        c.yaw = gView == 3 ? 90.f : 0.f;
        c.pitch = gView == 1 ? 90.f : 0.f;
        c.orthographic = true;
        c.orthoScale = gOrthoS[gView];
        c.lookAt = -1;
        ch = true;
    }
    y += 28;
    if (c.lookAt >= 0 && c.lookAt < (int)gScene.objs.size())
        label(cx, y, ("Looking at " + gScene.objs[c.lookAt].name).c_str(), w);
    toggle({cx, y + 28, 212, 24}, "Show Camera Path", gCameraPath);
    if (ch) { gView = 0; gShowRender = false; sceneChanged(); }
    labFooter(p);
}

void drawScatterLab() {
    R p = panel(490, 404, "Scatter Brush");
    int x = p.x + 16, y = p.y + 46, w = p.w - 32;
    label(x, y, "Select sources, then Shift-click the surface last.", w); y += 24;
    slider({x, y, w, 24}, "Brush Radius (px)", gScatterRadius, 8, 180, "%.0f"); y += 30;
    slider({x, y, w, 24}, "Instances per Dab", gScatterDensity, 1, 64, "%.0f"); y += 30;
    slider({x, y, w / 2 - 4, 24}, "Minimum Scale", gScatterMin, .05f, 5);
    slider({x + w / 2 + 4, y, w / 2 - 4, 24}, "Maximum Scale", gScatterMax, .05f, 5); y += 30;
    slider({x, y, w, 24}, "Rotation Range", gScatterYaw, 0, 360, "%.0f"); y += 30;
    sliderI({x, y, w, 24}, "Random Seed", gScatterSeed, 1, 65535); y += 30;
    toggle({x, y, w, 24}, "Align to Surface", gScatterAlign); y += 34;
    toggle({x, y, w / 2 - 4, 24}, "Mix Selected Species", gScatterPalette);
    toggle({x + w / 2 + 4, y, w / 2 - 4, 24}, "Erase Instances", gScatterErase); y += 32;
    if (button({x, y, w, 28}, "Start Painting", false, gBold)) {
        if ((!gScatterErase && gSelSet.size() < 2) || gSel < 0) {
            gStatus = "Select at least one source object and one receiving surface.";
            closeLab(true);
            return;
        }
        gScatter = true;
        gScatterDab = 0;
        gShowRender = false;
        closeLab(false);
        gStatus = "Paint over the selected surface. Press Esc to stop painting.";
        return;
    }
    labFooter(p, true);
}

bool convertMeshForEditing(Object& object) {
    if (object.type == ObjType::Mesh) return object.mesh && object.treePart == 0;
    for (const auto& other : gScene.objs) {
        if (other.instanceSource == gSel || (other.parent == gSel && other.terrainAnchor)) return false;
    }
    if (object.type == ObjType::Cube && !object.csg) {
        object.mesh = std::make_shared<Mesh>(makeEditableBox());
        object.type = ObjType::Mesh;
        object.instanceSource = -1;
        object.flatMesh = true;
        return true;
    }
    if (isLight(object.type) || isInfinite(object.type) || object.csg || object.type == ObjType::Picture ||
        !object.terrainKeys.empty()) return false;
    auto parts = meshSelection(gScene, {gSel}, 24);
    if (parts.size() != 1) return false;
    Mesh& mesh = parts.front().mesh;
    for (auto& point : mesh.v) point = object.inv * (point - object.worldPos);
    for (auto& normal : mesh.n) normal = normalize(object.fwd.transpose() * normal);
    if (dot(cross(object.fwd.col(0), object.fwd.col(1)), object.fwd.col(2)) < 0) {
        for (size_t i = 0; i < mesh.tri.size(); i += 3) std::swap(mesh.tri[i + 1], mesh.tri[i + 2]);
    }
    mesh.build();
    object.type = ObjType::Mesh;
    object.instanceSource = -1;
    object.hf.reset();
    object.mesh = std::make_shared<Mesh>(std::move(mesh));
    return true;
}

void drawMeshLab() {
    auto& object = gScene.objs[gSel];
    R panelRect = panel(830, 560, "Mesh Editor  -  " + object.name);
    R view{panelRect.x + 16, panelRect.y + 82, 560, 395};
    int x = view.x + view.w + 14, y = view.y, width = 224;
    if (object.instanceSource >= 0 && gScene.objs[object.instanceSource].rig) {
        note({view.x, view.y, view.w, 50}, "Use Make Rig Unique in the Rig Editor before editing this instance.");
        labFooter(panelRect, true);
        return;
    }
    bool bound = object.rig && !object.rig->weights.empty();
    if (object.type != ObjType::Mesh || !object.mesh || object.treePart) {
        if (button({view.x, view.y, 225, 28}, "Convert Selection to Mesh")) {
            if (convertMeshForEditing(object)) {
                sceneChanged();
                gMeshMessage = "Converted to an editable mesh.";
            } else gMeshMessage = "Use a standalone solid or mesh without procedural links.";
        }
        note({view.x, view.y + 40, view.w, 62}, gMeshMessage.empty() ?
             "Converting a solid replaces its procedural shape with editable triangles." : gMeshMessage.c_str());
        labFooter(panelRect);
        return;
    }
    const Mesh& mesh = *object.mesh;
    if (mesh.triCount() > 100000) {
        note({view.x, view.y, view.w, 50}, "Component editing supports meshes of up to 100,000 triangles.");
        labFooter(panelRect);
        return;
    }
    const char* modeNames[] = {"Vertices", "Edges", "Faces"};
    for (int i = 0; i < 3; ++i) {
        if (button({view.x + 92 * i, view.y - 33, 88, 25}, modeNames[i], i == (int)gMeshPick)) {
            gMeshPick = (MeshPick)i;
            gMeshSelection.clear();
        }
    }
    const char* viewNames[] = {"3D", "Front", "Top", "Side"};
    for (int i = 0; i < 4; ++i)
        if (button({view.x + 282 + 69 * i, view.y - 33, 64, 25}, viewNames[i], i == gMeshView)) gMeshView = i;
    M3 direction;
    if (gMeshView == 0) direction = rotX(rad(-25.f)) * rotY(rad(35.f));
    if (gMeshView == 2) direction = rotX(rad(90.f));
    if (gMeshView == 3) direction = rotY(rad(90.f));
    V3 center = (mesh.bmin + mesh.bmax) * .5f;
    float factor = .82f * view.h / std::max(length(mesh.bmax - mesh.bmin), .001f);
    std::vector<V3> points;
    points.reserve(mesh.v.size());
    for (V3 v : mesh.v) {
        V3 p = direction * (v - center);
        points.push_back({view.x + view.w * .5f + p.x * factor, view.y + view.h * .5f - p.y * factor, p.z});
    }
    using Edge = std::pair<uint32_t, uint32_t>;
    static std::weak_ptr<const Mesh> previousMesh;
    static std::vector<Edge> edges;
    if (previousMesh.lock() != object.mesh) {
        edges.clear();
        edges.reserve(mesh.tri.size());
        for (size_t i = 0; i < mesh.tri.size(); i += 3)
            for (size_t j = 0; j < 3; ++j)
                edges.push_back(std::minmax(mesh.tri[i + j], mesh.tri[i + (j + 1) % 3]));
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
        previousMesh = object.mesh;
    }
    int id = newId();
    press(view, id);
    if (clicked(view, id)) {
        float bestDist = 81.f, bestDepth = -1e30f;
        size_t hit = SIZE_MAX;
        V3 cursor{(float)gIn.mx, (float)gIn.my, 0};
        auto pointPick = [&](V3 p, size_t index) {
            V3 delta{p.x - cursor.x, p.y - cursor.y, 0};
            float distance = dot(delta, delta);
            if (distance < bestDist || (distance == bestDist && p.z > bestDepth)) {
                bestDist = distance;
                bestDepth = p.z;
                hit = index;
            }
        };
        if (gMeshPick == MeshPick::Vertex) {
            for (size_t i = 0; i < points.size(); ++i) pointPick(points[i], i);
        } else if (gMeshPick == MeshPick::Edge) {
            for (size_t i = 0; i < edges.size(); ++i) {
                V3 a = points[edges[i].first], b = points[edges[i].second];
                V3 delta{b.x - a.x, b.y - a.y, 0};
                float t = clampf(dot(cursor - a, delta) / std::max(.001f, dot(delta, delta)), 0, 1);
                pointPick(lerp(a, b, t), i);
            }
        } else {
            for (size_t i = 0; i < mesh.tri.size(); i += 3) {
                V3 a = points[mesh.tri[i]], b = points[mesh.tri[i + 1]], c = points[mesh.tri[i + 2]];
                float area = cross(b - a, c - a).z;
                if (std::fabs(area) < .001f) continue;
                float u = cross(cursor - a, c - a).z / area;
                float v = cross(b - a, cursor - a).z / area;
                if (u < 0 || v < 0 || u + v > 1) continue;
                float depth = a.z + u * (b.z - a.z) + v * (c.z - a.z);
                if (depth > bestDepth) { bestDepth = depth; hit = i / 3; }
            }
        }
        if (!gIn.shift) gMeshSelection.clear();
        if (hit != SIZE_MAX && !gMeshSelection.erase(hit)) gMeshSelection.insert(hit);
    }
    auto edgeLine = [&](uint32_t a, uint32_t b, Color color) {
        line((int)points[a].x, (int)points[a].y, (int)points[b].x, (int)points[b].y, color);
    };
    fill(view, rgb(25, 29, 36));
    setClip(view);
    for (size_t i = 0; i < edges.size(); ++i)
        edgeLine(edges[i].first, edges[i].second, gMeshPick == MeshPick::Edge && gMeshSelection.count(i) ?
                 rgb(255, 194, 83) : rgb(132, 153, 177));
    if (gMeshPick == MeshPick::Face) {
        for (size_t i : gMeshSelection) {
            const auto* face = &mesh.tri[3 * i];
            for (int j = 0; j < 3; ++j) edgeLine(face[j], face[(j + 1) % 3], rgb(255, 194, 83));
        }
    }
    if (gMeshPick == MeshPick::Vertex) {
        for (size_t i = 0; i < points.size(); ++i)
            circle((int)points[i].x, (int)points[i].y, 3, gMeshSelection.count(i) ? rgb(255, 194, 83) : rgb(178, 203, 226));
    }
    clearClip();
    bevel(view, true);
    std::string count = std::to_string(mesh.v.size()) + " vertices / " + std::to_string(mesh.triCount()) + " triangles";
    label(x, y, count.c_str(), width);
    y += 26;
    size_t limit = gMeshPick == MeshPick::Vertex ? mesh.v.size() :
                   gMeshPick == MeshPick::Edge ? edges.size() : (size_t)mesh.triCount();
    if (button({x, y, 108, 24}, "Select All"))
        for (size_t i = 0; i < limit; ++i) gMeshSelection.insert(i);
    if (button({x + 114, y, 108, 24}, "Clear")) gMeshSelection.clear();
    y += 32;
    auto commit = [&](Mesh edited, const char* message, bool resetSelection = false) {
        object.mesh = std::make_shared<Mesh>(std::move(edited));
        object.instanceSource = -1;
        if (resetSelection) gMeshSelection.clear();
        gMeshMessage = message;
        sceneChanged();
    };
    const char* transforms[] = {"Move", "Rotate", "Scale"};
    for (int i = 0; i < 3; ++i) {
        if (button({x + 76 * i, y, 70, 24}, transforms[i], gMeshTransform == i)) {
            gMeshTransform = i;
            gMeshOffset = i == 2 ? V3(1) : V3{};
        }
    }
    y += 29;
    const char* axes[] = {"X", "Y", "Z"};
    float* components[] = {&gMeshOffset.x, &gMeshOffset.y, &gMeshOffset.z};
    V3 neutral = gMeshTransform == 2 ? V3(1) : V3{};
    for (int i = 0; i < 3; ++i) {
        dragF({x + 76 * i, y, 70, 24}, axes[i], *components[i], gMeshTransform == 1 ? .5f : .025f,
              gMeshTransform == 2 ? .01f : -10000, gMeshTransform == 2 ? 100.f : 10000.f, "%.2f");
    }
    y += 28;
    if (button({x, y, width, 24}, "Apply Transform") && !gMeshSelection.empty() && length(gMeshOffset - neutral) > 0) {
        Mesh edited = mesh;
        std::set<uint32_t> vertices;
        for (size_t i : gMeshSelection) {
            if (gMeshPick == MeshPick::Vertex) vertices.insert((uint32_t)i);
            else if (gMeshPick == MeshPick::Edge) { vertices.insert(edges[i].first); vertices.insert(edges[i].second); }
            else vertices.insert(mesh.tri.begin() + i * 3, mesh.tri.begin() + i * 3 + 3);
        }
        V3 pivot;
        for (uint32_t i : vertices) pivot += mesh.v[i];
        pivot = pivot / (float)vertices.size();
        M3 rotation = rotY(rad(gMeshOffset.y)) * rotX(rad(gMeshOffset.x)) * rotZ(rad(gMeshOffset.z));
        for (uint32_t i : vertices) {
            V3 relative = mesh.v[i] - pivot;
            if (gMeshTransform == 0) edited.v[i] += gMeshOffset;
            else if (gMeshTransform == 1) edited.v[i] = pivot + rotation * relative;
            else edited.v[i] = pivot + relative * gMeshOffset;
        }
        edited.n.clear();
        edited.build();
        gMeshOffset = neutral;
        commit(std::move(edited), "Transformed the selected components.");
        return;
    }
    y += 31;
    if (gMeshPick == MeshPick::Face && !bound) {
        dragF({x, y, width, 24}, "Extrusion Distance", gMeshDistance, .02f, -1000, 1000, "%.2f");
        y += 28;
        bool extrude = button({x, y, width, 24}, "Extrude Faces");
        y += 28;
        bool remove = button({x, y, 108, 24}, "Delete Faces");
        bool flip = button({x + 114, y, 108, 24}, "Flip Faces");
        if ((extrude || remove || flip) && !gMeshSelection.empty()) {
            Mesh edited = mesh;
            std::vector<size_t> faces(gMeshSelection.begin(), gMeshSelection.end());
            if (extrude && extrudeMeshFaces(edited, faces, gMeshDistance) && edited.triCount() <= 100000) {
                commit(std::move(edited), "Extruded the face region.");
                return;
            }
            if (remove && deleteMeshFaces(edited, faces)) {
                commit(std::move(edited), "Deleted selected faces.", true);
                return;
            }
            if (flip) {
                for (size_t face : faces) std::swap(edited.tri[face * 3 + 1], edited.tri[face * 3 + 2]);
                edited.n.clear();
                edited.build();
                commit(std::move(edited), "Reversed selected face winding.");
                return;
            }
            gMeshMessage = extrude ? "Use a region with a consistent winding and a nonzero distance." :
                           "Keep at least one face in the mesh.";
        }
        y += 30;
    }
    if (bound) {
        note({x, y, width, 31}, "Unbind skin in the Rig Editor to change topology.");
    } else if (button({x, y, width, 24}, "Subdivide Mesh")) {
        Mesh edited = mesh;
        if (mesh.triCount() <= 25000 && subdivideMesh(edited)) {
            commit(std::move(edited), "Split every triangle into four.", true);
            return;
        }
        gMeshMessage = "Subdivision would exceed the editor's triangle limit.";
    }
    y += 32;
    label(x, y, "Planar UV Projection", width);
    for (int axis = 0; axis < 3; ++axis) {
        if (button({x + 76 * axis, y + 20, 70, 24}, axes[axis])) {
            Mesh edited = mesh;
            projectMeshUV(edited, axis);
            commit(std::move(edited), "Projected planar UV coordinates.");
            return;
        }
    }
    note({view.x, view.y + view.h + 8, view.w, 30}, gMeshMessage.empty() ?
         "Click to select. Shift-click toggles components in the selection." : gMeshMessage.c_str());
    labFooter(panelRect);
}

void drawRigLab() {
    Object& object = gScene.objs[gSel];
    R p = panel(836, 560, "Rig Editor  -  " + object.name);
    R view{p.x + 16, p.y + 82, 555, 380};
    int x = view.x + view.w + 14, y = view.y, width = p.x + p.w - x - 16;
    if (object.type != ObjType::Mesh || !object.mesh || object.treePart) {
        if (button({view.x, y, 208, 28}, "Convert Selection to Mesh")) {
            if (convertMeshForEditing(object)) sceneChanged();
            else gRigMessage = "Use an imported mesh or a standalone solid.";
        }
        note({view.x, y + 40, view.w, 35}, gRigMessage.empty() ? "Joints deform a mesh." : gRigMessage.c_str());
        labFooter(p);
        return;
    }
    const auto& mesh = *object.mesh;
    if (object.instanceSource >= 0) {
        if (button({view.x, y, 174, 28}, "Make Rig Unique")) {
            object.rig = gScene.objs[object.instanceSource].rig;
            object.instanceSource = -1;
            sceneChanged();
        }
        note({view.x, y + 40, view.w, 38}, "Make this instance unique to edit its skeleton and poses.");
        labFooter(p);
        return;
    }
    if (mesh.v.empty() || mesh.v.size() > MAX_SKIN_VERTICES) {
        note({view.x, y, view.w, 35}, "Rigging supports nonempty meshes of up to one million vertices.");
        labFooter(p);
        return;
    }
    auto commit = [&](Rig edited, const char* message) {
        if (!validRig(edited, mesh.v.size())) { gRigMessage = "Use a nonzero joint length and valid pose values."; return false; }
        object.rig = std::make_shared<Rig>(std::move(edited));
        gRigMessage = message;
        sceneChanged();
        return true;
    };
    const char* modes[] = {"Skeleton", "Pose", "Weights"};
    for (int i = 0; i < 3; ++i) {
        if (button({view.x + i * 89, view.y - 33, 83, 26}, modes[i], i == gRigMode)) {
            gRigMode = i;
            gRigMessage.clear();
        }
    }
    const char* views[] = {"3D", "Front", "Top", "Side"};
    for (int i = 0; i < 4; ++i)
        if (button({view.x + 273 + i * 69, view.y - 33, 64, 26}, views[i], gRigView == i)) gRigView = i;
    const Rig* rig = object.rig.get();
    if (rig) gRigJoint = std::clamp(gRigJoint, 0, (int)rig->joints.size() - 1);
    else gRigMode = 0;
    bool weights = rig && !rig->weights.empty();
    const Mesh& shown = gRigMode == 1 ? *object.visibleMesh() : mesh;
    std::vector<PosedJoint> pose;
    if (rig && gRigMode == 1) pose = poseRig(*rig, gScene.frame);
    auto head = [&](int i) { return !pose.empty() ? pose[i].head : rig->joints[i].head; };
    auto tail = [&](int i) { return !pose.empty() ? pose[i].tail : rig->joints[i].tail; };
    V3 low = shown.bmin, high = shown.bmax;
    auto include = [&](V3 v) {
        low = {std::min(low.x, v.x), std::min(low.y, v.y), std::min(low.z, v.z)};
        high = {std::max(high.x, v.x), std::max(high.y, v.y), std::max(high.z, v.z)};
    };
    if (rig) {
        for (int i = 0; i < (int)rig->joints.size(); ++i) { include(head(i)); include(tail(i)); }
    }
    M3 direction;
    if (gRigView == 0) direction = rotX(rad(-25.f)) * rotY(rad(35.f));
    if (gRigView == 2) direction = rotX(rad(90.f));
    if (gRigView == 3) direction = rotY(rad(90.f));
    float factor = .84f * view.h / std::max(length(high - low), .001f);
    V3 center = (low + high) * .5f;
    auto project = [&](V3 v) {
        V3 q = direction * (v - center);
        return V3(view.x + view.w * .5f + q.x * factor, view.y + view.h * .5f - q.y * factor, q.z);
    };
    int id = newId();
    press(view, id);
    if (rig && clicked(view, id)) {
        V3 cursor((float)gIn.mx, (float)gIn.my, 0);
        float best = 100, depth = -1e30f;
        size_t selected = SIZE_MAX;
        auto test = [&](V3 p, size_t i) {
            float d = (p.x - cursor.x) * (p.x - cursor.x) + (p.y - cursor.y) * (p.y - cursor.y);
            if (d < best || (d == best && p.z > depth)) { best = d; depth = p.z; selected = i; }
        };
        if (gRigMode == 2 && weights) {
            for (size_t i = 0; i < mesh.v.size(); ++i) test(project(mesh.v[i]), i);
            if (!gIn.shift) gRigVertices.clear();
            if (selected != SIZE_MAX) {
                V3 position = mesh.v[selected];
                bool add = !gRigVertices.count((uint32_t)selected);
                for (uint32_t i = 0; i < mesh.v.size(); ++i) {
                    if (dot(mesh.v[i] - position, mesh.v[i] - position) > 1e-12f) continue;
                    if (add) gRigVertices.insert(i);
                    else gRigVertices.erase(i);
                }
            }
        } else {
            for (size_t i = 0; i < rig->joints.size(); ++i) {
                V3 a = project(head((int)i)), b = project(tail((int)i));
                V3 along(b.x - a.x, b.y - a.y, 0);
                float t = clampf(dot(cursor - a, along) / std::max(.01f, dot(along, along)), 0, 1);
                test(lerp(a, b, t), i);
            }
            if (selected != SIZE_MAX) gRigJoint = (int)selected;
        }
    }
    fill(view, rgb(26, 30, 38));
    setClip(view);
    auto stroke = [](V3 a, V3 b, Color color) { line((int)a.x, (int)a.y, (int)b.x, (int)b.y, color); };
    size_t stride = std::max<size_t>(1, (size_t)shown.triCount() / 10000);
    for (size_t face = 0; face < (size_t)shown.triCount(); face += stride) {
        auto* tri = &shown.tri[face * 3];
        V3 points[] = {project(shown.v[tri[0]]), project(shown.v[tri[1]]), project(shown.v[tri[2]])};
        for (int i = 0; i < 3; ++i) stroke(points[i], points[(i + 1) % 3], rgb(86, 104, 127));
    }
    if (rig) {
        for (int i = 0; i < (int)rig->joints.size(); ++i) {
            V3 a = project(head(i)), b = project(tail(i));
            Color color = i == gRigJoint ? rgb(255, 204, 91) : rgb(111, 220, 233);
            stroke(a, b, color);
            circle((int)a.x, (int)a.y, 4, color);
            circle((int)b.x, (int)b.y, 3, color);
        }
    }
    if (gRigMode == 2 && weights) {
        size_t sample = std::max<size_t>(1, mesh.v.size() / 6000);
        for (size_t i = 0; i < mesh.v.size(); ++i) {
            bool selected = gRigVertices.count((uint32_t)i) != 0;
            if (!selected && i % sample != 0) continue;
            float weight = 0;
            for (size_t j = 0; j < 4; ++j)
                if (rig->weights[i].joint[j] == gRigJoint) weight += rig->weights[i].weight[j];
            V3 point = project(mesh.v[i]);
            Color color = selected ? rgb(255, 240, 157) : rgb((unsigned)lerpf(70, 239, weight),
                          (unsigned)lerpf(132, 77, weight), (unsigned)lerpf(232, 57, weight));
            circle((int)point.x, (int)point.y, selected ? 4 : 2, color);
        }
    }
    clearClip();
    bevel(view, true);
    if (gRigMode == 0) {
        bool addRoot = button({x, y, 108, 24}, "Add Root");
        bool addChild = button({x + 114, y, width - 114, 24}, "Add Child");
        if ((addRoot || (addChild && rig)) && (!rig || rig->joints.size() < MAX_RIG_JOINTS)) {
            Rig edited = rig ? *rig : Rig{};
            Joint joint;
            joint.name = "Joint " + std::to_string(edited.joints.size() + 1);
            float extent = std::max(.1f, length(mesh.bmax - mesh.bmin) * .35f);
            if (addRoot) {
                joint.head = (mesh.bmin + mesh.bmax) * .5f - V3(0, extent, 0);
                joint.tail = joint.head + V3(0, extent, 0);
            } else {
                const Joint& parent = rig->joints[gRigJoint];
                joint.parent = gRigJoint;
                joint.head = parent.tail;
                joint.tail = parent.tail + (parent.tail - parent.head) * .75f;
            }
            edited.joints.push_back(std::move(joint));
            if (commit(std::move(edited), "Added a joint.")) gRigJoint = (int)object.rig->joints.size() - 1;
            return;
        }
        y += 31;
    }
    if (rig) {
        if (button({x, y, 26, 24}, "<")) gRigJoint = std::max(0, gRigJoint - 1);
        if (button({x + width - 26, y, 26, 24}, ">")) gRigJoint = std::min((int)rig->joints.size() - 1, gRigJoint + 1);
        const Joint& joint = rig->joints[gRigJoint];
        std::string name = gEditObj == -4 ? gEditBuf + "|" : joint.name;
        if (button({x + 31, y, width - 62, 24}, name.c_str(), gEditObj == -4)) { gEditObj = -4; gEditBuf = joint.name; }
        y += 32;
        auto vecRow = [&](const char* title, V3& value, float speed, float minimum, float maximum) {
            label(x, y, title, width);
            y += 18;
            int cell = (width - 8) / 3;
            bool changed = dragF({x, y, cell, 21}, "X", value.x, speed, minimum, maximum, "%.2f");
            changed |= dragF({x + cell + 4, y, cell, 21}, "Y", value.y, speed, minimum, maximum, "%.2f");
            changed |= dragF({x + (cell + 4) * 2, y, cell, 21}, "Z", value.z, speed, minimum, maximum, "%.2f");
            y += 27;
            return changed;
        };
        if (gRigMode == 0) {
            V3 h = joint.head, t = joint.tail;
            bool changed = vecRow("Rest Head", h, .025f, -100000, 100000);
            changed |= vecRow("Rest Tail", t, .025f, -100000, 100000);
            int parent = joint.parent;
            std::string parentName = parent < 0 ? "Root" : rig->joints[parent].name;
            if (button({x, y, width, 24}, ("Parent  " + parentName).c_str()) && gRigJoint > 0) {
                parent = parent + 1 < gRigJoint ? parent + 1 : -1;
                changed = true;
            }
            y += 30;
            bool bind = button({x, y, 108, 24}, weights ? "Rebind Skin" : "Bind Skin");
            bool unbind = button({x + 114, y, width - 114, 24}, "Unbind Skin");
            y += 30;
            if (button({x, y, 108, 24}, "Remove Rig")) {
                object.rig.reset();
                gRigVertices.clear();
                sceneChanged();
                return;
            }
            if (button({x + 114, y, width - 114, 24}, "Delete Joint")) {
                if (rig->joints.size() == 1 ||
                    std::any_of(rig->joints.begin(), rig->joints.end(), [&](const Joint& item) { return item.parent == gRigJoint; })) {
                    gRigMessage = "Delete child joints first, or use Remove Rig.";
                    return;
                }
                Rig edited = *rig;
                for (auto& skin : edited.weights) {
                    setSkinWeight(skin, gRigJoint, 0, edited.joints.size());
                    for (size_t i = 0; i < skin.joint.size(); ++i) {
                        if (skin.weight[i] == 0) skin.joint[i] = 0;
                        else if (skin.joint[i] > gRigJoint) --skin.joint[i];
                    }
                }
                edited.joints.erase(edited.joints.begin() + gRigJoint);
                for (auto& remaining : edited.joints)
                    if (remaining.parent > gRigJoint) --remaining.parent;
                if (commit(std::move(edited), "Deleted a leaf joint. Unweighted vertices retain their rest positions."))
                    gRigJoint = std::min(gRigJoint, (int)object.rig->joints.size() - 1);
                return;
            }
            if (changed || bind || (unbind && weights)) {
                Rig edited = *rig;
                auto& target = edited.joints[gRigJoint];
                target.head = h;
                target.tail = t;
                target.parent = parent;
                if (unbind) { edited.weights.clear(); gRigVertices.clear(); }
                if (bind && !bindRig(edited, mesh)) {
                    gRigMessage = "Could not bind this joint layout.";
                } else commit(std::move(edited), bind ? "Bound skin by distance to joints." :
                              unbind ? "Removed skin weights." : "Updated the rest joint.");
                return;
            }
        } else if (gRigMode == 1) {
            int frame = gScene.frame;
            if (sliderI({x, y, width, 24}, "Frame", frame, 0, gScene.frames - 1)) {
                gScene.applyFrame(frame);
                gViewPreviewDirty = gNanoDirty = true;
            }
            y += 30;
            Key poseKey = sampleTransformTrack(joint.keys, (float)frame, joint.pose);
            bool changed = vecRow("Local Translation", poseKey.pos, .025f, -100000, 100000);
            changed |= vecRow("Local Rotation", poseKey.rot, .5f, -100000, 100000);
            changed |= vecRow("Local Scale", poseKey.scl, .025f, .01f, 100);
            bool key = button({x, y, 108, 24}, "Set Pose Key");
            bool remove = button({x + 114, y, width - 114, 24}, "Delete Key");
            y += 30;
            bool reset = button({x, y, 108, 24}, "Reset Pose");
            bool ease = button({x + 114, y, width - 114, 24}, (std::string("Ease ") + easeName(poseKey.ease)).c_str());
            if (changed || key || remove || reset || ease) {
                Rig edited = *rig;
                auto& target = edited.joints[gRigJoint];
                if (reset) { poseKey.pos = {}; poseKey.rot = {}; poseKey.scl = V3(1); }
                if (ease) poseKey.ease = (Ease)(((int)poseKey.ease + 1) % (int)Ease::COUNT);
                if (remove) {
                    if (!eraseKey(target.keys, frame)) { gRigMessage = "There is no key on this frame."; return; }
                } else if (key || !target.keys.empty()) {
                    putKey(target.keys, poseKey);
                } else target.pose = poseKey;
                commit(std::move(edited), remove ? "Deleted the pose key." : "Updated the joint pose.");
                return;
            }
        } else if (weights) {
            std::string count = std::to_string(gRigVertices.size()) + " vertices selected";
            label(x, y, count.c_str(), width);
            y += 25;
            slider({x, y, width, 24}, "Joint Weight", gRigWeight, 0, 1);
            y += 30;
            if (button({x, y, width, 24}, "Apply Joint Weight") && !gRigVertices.empty()) {
                Rig edited = *rig;
                for (uint32_t vertex : gRigVertices) {
                    if (!setSkinWeight(edited.weights[vertex], gRigJoint, gRigWeight, rig->joints.size())) {
                        gRigMessage = "With one joint, use weight 0 or 1.";
                        return;
                    }
                }
                commit(std::move(edited), "Assigned and normalized vertex weights.");
                return;
            }
            y += 30;
            if (button({x, y, width, 24}, "Clear Selection")) gRigVertices.clear();
        } else {
            note({x, y, width, 54}, "Bind skin in the Skeleton tab to edit weights.");
        }
    }
    note({view.x, view.y + view.h + 8, view.w, 44}, gRigMessage.empty() ?
         (gRigMode == 2 ? "Select vertices in the rest mesh. Shift-click toggles selection." :
          gRigMode == 1 ? "Click a joint to pose it. Edits on animated joints set a key at the current frame." :
          "Add a root and child joints, adjust their rest positions, and bind the skin.") : gRigMessage.c_str());
    labFooter(p);
}

void drawCurveLab() {
    Object& object = gScene.objs[gSel];
    R p = panel(810, 510, "Curve Editor  -  " + object.name);
    int x = p.x + 16, y = p.y + 48;
    const char* channels[] = {"Location", "Rotation", "Scale"};
    const char* axes[] = {"X", "Y", "Z"};
    for (int i = 0; i < 3; ++i) {
        if (button({x + i * 96, y, 90, 24}, channels[i], i == gCurveChannel)) gCurveChannel = i;
        if (button({x + 302 + i * 44, y, 40, 24}, axes[i], i == gCurveAxis)) gCurveAxis = i;
    }
    if (object.rig && !object.rig->joints.empty()) {
        if (button({x + 438, y, 80, 24}, gCurveJointTrack ? "Joint" : "Object", gCurveJointTrack)) {
            gCurveJointTrack = !gCurveJointTrack;
            gCurveKey = -1;
        }
        if (gCurveJointTrack && button({x + 523, y, 112, 24}, object.rig->joints[gCurveJoint].name.c_str(), false, gSmall)) {
            int size = (int)object.rig->joints.size();
            gCurveJoint = (gCurveJoint + (gIn.shift ? size - 1 : 1)) % size;
            gCurveKey = -1;
        }
    }
    bool jointTrack = gCurveJointTrack && object.rig && !object.rig->joints.empty();
    Key fallback{0, object.pos, object.rot, object.scl};
    std::vector<Key> jointKeys;
    if (jointTrack) {
        const auto& joint = object.rig->joints[gCurveJoint];
        fallback = joint.pose;
        jointKeys = joint.keys;
    }
    auto& keys = jointTrack ? jointKeys : object.keys;
    auto commitKeys = [&] {
        if (jointTrack) {
            auto rig = std::make_shared<Rig>(*object.rig);
            rig->joints[gCurveJoint].keys = jointKeys;
            object.rig = std::move(rig);
        }
        gScene.applyFrame(gScene.frame);
        sceneChanged();
    };
    auto value = [&](Key& key) -> float& {
        auto& v = gCurveChannel == 0 ? key.pos : gCurveChannel == 1 ? key.rot : key.scl;
        return gCurveAxis == 0 ? v.x : gCurveAxis == 1 ? v.y : v.z;
    };
    auto scrub = [&](int frame) {
        if (gScene.frame == frame) return;
        gScene.applyFrame(frame);
        gShowRender = false;
        gNanoDirty = gViewPreviewDirty = true;
    };
    auto choose = [&](int frame) {
        gCurveKey = (int)(std::lower_bound(keys.begin(), keys.end(), frame,
            [](const Key& key, int time) { return key.frame < time; }) - keys.begin());
    };
    if (button({p.x + p.w - 152, y, 136, 24}, "Set Key Here")) {
        Key key = sampleTransformTrack(keys, (float)gScene.frame, fallback);
        bool room = !jointTrack || keys.size() < MAX_JOINT_KEYS ||
                    std::any_of(keys.begin(), keys.end(), [&](const Key& other) { return other.frame == key.frame; });
        if (!room) return;
        putKey(keys, key);
        choose(key.frame);
        commitKeys();
    }
    R area{x, y + 36, p.w - 32, 294};
    R graph{area.x + 64, area.y + 13, area.w - 82, area.h - 37};
    int last = std::max(1, gScene.frames - 1);
    auto posX = [&](float frame) { return (int)std::lround(graph.x + graph.w * clampf(frame / (float)last, 0, 1)); };
    auto sample = [&](float frame) { Key key = sampleTransformTrack(keys, frame, fallback); return value(key); };
    std::vector<float> samples(graph.w + 1);
    float low = sample(0), high = low;
    for (int i = 0; i <= graph.w; ++i) {
        samples[i] = sample(last * (float)i / graph.w);
        low = std::min(low, samples[i]);
        high = std::max(high, samples[i]);
    }
    for (auto& key : keys) {
        if (key.frame > last) break;
        low = std::min(low, value(key));
        high = std::max(high, value(key));
    }
    float margin = std::max(gCurveChannel == 1 ? 5.f : gCurveChannel == 2 ? .1f : .5f, (high - low) * .12f);
    low -= margin;
    high += margin;
    int id = newId();
    static float dragLow = 0, dragHigh = 1;
    static bool dragPoint = false;
    if (gActive == id && gIn.down && dragPoint) { low = dragLow; high = dragHigh; }
    auto posY = [&](float v) {
        return (int)std::lround(graph.y + graph.h * (1 - clampf((v - low) / (high - low), 0, 1)));
    };
    press(graph, id);
    if (gActive == id && gIn.pressed) {
        int best = 121;
        int hitKey = -1;
        for (size_t i = 0; i < keys.size(); ++i) {
            if (keys[i].frame > last) break;
            int dx = posX((float)keys[i].frame) - gIn.mx, dy = posY(value(keys[i])) - gIn.my;
            int distance = dx * dx + dy * dy;
            if (distance < best) { best = distance; hitKey = (int)i; }
        }
        dragPoint = hitKey >= 0;
        dragLow = low;
        dragHigh = high;
        if (dragPoint) {
            gCurveKey = hitKey;
            scrub(keys[gCurveKey].frame);
        }
    }
    if (gActive == id && gIn.down) {
        if (dragPoint && gCurveKey >= 0 && gIn.dy) {
            float& v = value(keys[gCurveKey]);
            float maximum = jointTrack && gCurveChannel == 2 ? 100.f : 1e6f;
            v = clampf(v - gIn.dy * (dragHigh - dragLow) / graph.h, gCurveChannel == 2 ? .01f : -1e6f, maximum);
            commitKeys();
        } else if (!dragPoint) {
            int frame = (int)std::lround(last * clampf((gIn.mx - graph.x) / (float)graph.w, 0, 1));
            scrub(frame);
        }
    }
    fill(area, rgb(25, 29, 36));
    for (int i = 0; i <= 4; ++i) {
        int gy = graph.y + i * graph.h / 4;
        int gx = graph.x + i * graph.w / 4;
        line(graph.x, gy, graph.x + graph.w, gy, rgb(58, 67, 83));
        line(gx, graph.y, gx, graph.y + graph.h, rgb(58, 67, 83));
        char tick[32];
        std::snprintf(tick, sizeof tick, "%.3g", lerpf(high, low, i * .25f));
        textIn({area.x, gy - 8, 60, 16}, tick, TXT_DIM, gSmall, TextRight | TextMiddle);
        std::snprintf(tick, sizeof tick, "%d", (int)std::lround(last * i * .25f));
        textIn({gx - 24, graph.y + graph.h + 4, 48, 16}, tick, TXT_DIM, gSmall);
    }
    int playhead = posX((float)gScene.frame);
    line(playhead, graph.y, playhead, graph.y + graph.h, rgb(235, 147, 92));
    for (int i = 1; i <= graph.w; ++i)
        line(graph.x + i - 1, posY(samples[i - 1]), graph.x + i, posY(samples[i]), rgb(111, 205, 236));
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].frame > last) break;
        int gx = posX((float)keys[i].frame), gy = posY(value(keys[i]));
        fill({gx - 3, gy - 3, 7, 7}, (int)i == gCurveKey ? rgb(255, 200, 80) : rgb(180, 223, 242));
    }
    bevel(area, true);
    y = area.y + area.h + 10;
    if (gCurveKey >= 0 && gCurveKey < (int)keys.size()) {
        Key& key = keys[gCurveKey];
        int frame = key.frame;
        bool retime = dragI({x, y, 140, 24}, "Key Frame", frame, .25f, 0, last);
        bool change = dragF({x + 146, y, 153, 24}, "Key Value", value(key), (high - low) / 250,
                            gCurveChannel == 2 ? .01f : -1e6f, jointTrack && gCurveChannel == 2 ? 100.f : 1e6f, "%.3f");
        std::string ease = std::string("Ease ") + easeName(key.ease);
        if (button({x + 305, y, 118, 24}, ease.c_str())) {
            key.ease = (Ease)(((int)key.ease + 1) % (int)Ease::COUNT);
            change = true;
        }
        bool copyKey = button({x + 429, y, 118, 24}, "Copy to Playhead");
        bool removeKey = button({x + 553, y, 105, 24}, "Remove Key");
        if (copyKey && gScene.frame != key.frame) {
            Key copy = key;
            copy.frame = gScene.frame;
            if (jointTrack && keys.size() >= MAX_JOINT_KEYS &&
                std::none_of(keys.begin(), keys.end(), [&](const Key& other) { return other.frame == copy.frame; })) return;
            putKey(keys, copy);
            choose(copy.frame);
            change = true;
        } else if (removeKey) {
            keys.erase(keys.begin() + gCurveKey);
            gCurveKey = -1;
            change = true;
        } else if (retime && std::none_of(keys.begin(), keys.end(), [frame](const Key& k) { return k.frame == frame; })) {
            Key moved = key;
            moved.frame = frame;
            keys.erase(keys.begin() + gCurveKey);
            putKey(keys, moved);
            choose(frame);
            scrub(frame);
            change = true;
        }
        if (change) commitKeys();
    }
    note({x, y + 34, area.w, 24}, "Drag a key up or down to change its value. Drag the graph background to scrub.");
    labFooter(p);
}

void drawCommandsLab() {
    R p = panel(770, 588, "Commands");
    struct Command { const char* label; const char* key; std::function<void()> run; };
    const Command commands[] = {
        {"Render / Resume", "Ctrl+R", actRender}, {"Frame Selection", "F", [] { frameView(validSel()); }},
        {"Group Selection", "Ctrl+G", actGroup}, {"Ungroup Selection", "Ctrl+U", actUngroup},
        {"Duplicate", "Ctrl+D", actDuplicate}, {"Land Selection", "Ctrl+L", actLand},
        {"Attributes", "A", [] { openLab(LabAttributes); }}, {"Materials", "M", [] { openLab(LabMat); }},
        {"Object Editor", "E", [] { if (validSel()) openLab(objectEditor(gScene.objs[gSel])); }},
        {"Motion Editor", "Ctrl+K sets a key", [] { openLab(LabMotion); }},
        {"Cameras", "1-4 switch views", [] { openLab(LabCameras); }},
        {"Object Library", "Browse saved assets", [] { openLab(LabAssets); }},
        {"Export Mesh", "OBJ or DXF", actExportGeometry}, {"Render Options", "Output, crop and samples", [] { openLab(LabRender); }},
        {"Collect Scene Assets", "Save a portable scene folder", actPackage}, {"Relink Missing Asset", "Replace an unavailable image", actRelink},
        {"Recover Autosave", "Restore a recovery scene", actRecover},
        {"Reload Scene Images", "Refresh files from disk", [] { gScene.reloadAssets(); sceneChanged(); }},
        {"Save with Embedded Images", "Keep images inside the scene file", actEmbed},
        {"Scatter Brush", "Paint linked objects", [] { openLab(LabScatter); }},
        {"Transform", "Snap, center and mirror", [] { openLab(LabTransform); }},
        {"Simulation", "Bake physics or liquid", [] { openLab(LabSimulation); }},
        {"Mesh Editor", "Edit mesh components", [] { openLab(LabMesh); }},
        {"Curve Editor", "Edit object transform curves", [] { openLab(LabCurves); }},
        {"Rig Editor", "Bind and animate a mesh", [] { openLab(LabRig); }},
    };
    for (int i = 0; i < (int)std::size(commands); ++i) {
        int x = p.x + 18 + (i % 2) * 369, y = p.y + 50 + (i / 2) * 36;
        if (button({x, y, 182, 27}, commands[i].label)) {
            auto run = commands[i].run;
            closeLab(true);
            defer([run] { gNoPush = false; beginInteraction(); run(); endInteraction(); });
            return;
        }
        textIn({x + 192, y, 164, 27}, commands[i].key, TXT_DIM, gSmall, TextLeft | TextMiddle);
    }
    note({p.x + 20, p.y + 520, p.w - 48, 22}, "Shift-click adds to selection. Alt-click picks a group member. Ctrl lowers terrain and Shift smooths.");
    labFooter(p, true);
}

void drawTransformLab() {
    R p = panel(550, 406, "Transform");
    int x = p.x + 18, y = p.y + 46;
    label(x, y, "Pivot");
    const char* pivots[] = {"Median", "Bounds Center", "Active Object", "World Origin", "3D Cursor"};
    for (int i = 0; i < 5; ++i)
        if (button({x + i * 103, y + 21, 98, 24}, pivots[i], gPivotMode == (Pivot)i, gSmall)) {
            gPivotMode = (Pivot)i;
            if (gPivotMode == Pivot::Cursor) gCursorVisible = true;
        }
    y += 56;
    label(x, y, "Axis Constraint");
    const char* axes[] = {"Free", "X", "Y", "Z"};
    for (int i = 0; i < 4; ++i)
        if (button({x + i * 64, y + 21, 60, 24}, axes[i], gTransformAxis == i - 1)) gTransformAxis = i - 1;
    toggle({x + 263, y + 21, 251, 24}, "Auto-lock Move Axis", gAutoAxis);
    y += 56;
    label(x, y, "Snapping");
    const char* modes[] = {"Off", "Grid", "Surface"};
    for (int i = 0; i < 3; ++i)
        if (button({x + i * 64, y + 21, 60, 24}, modes[i], gSnapMode == (Snap)i)) gSnapMode = (Snap)i;
    slider({x + 197, y + 21, 154, 24}, "Grid Step", gGridStep, .05f, 100.f, "%.2f", true);
    slider({x + 357, y + 21, 157, 24}, "Angle Step", gAngleStep, 1.f, 90.f, "%.0f");
    y += 56;
    label(x, y, "Placement");
    // Scene actions close the settings palette and use the viewport's undo history.
    auto apply = [](void (*action)()) {
        closeLab(false);
        defer([action] { gNoPush = false; beginInteraction(); action(); endInteraction(); });
    };
    if (button({x, y + 21, 124, 24}, "Snap to Grid")) { apply([] { placeSelection(Placement::Grid); }); return; }
    if (button({x + 130, y + 21, 124, 24}, "Center at Origin")) { apply([] { placeSelection(Placement::Origin); }); return; }
    if (button({x + 260, y + 21, 124, 24}, "Center at Cursor")) { apply([] { placeSelection(Placement::Cursor); }); return; }
    if (button({x + 390, y + 21, 124, 24}, "Cursor to Origin")) { gCursor = {}; gCursorVisible = true; }
    y += 56;
    label(x, y, "Mirror Across Pivot");
    for (int axis = 0; axis < 3; ++axis)
        if (button({x + axis * 44, y + 21, 40, 24}, axes[axis + 1], gMirrorAxis == axis)) gMirrorAxis = axis;
    if (button({x + 140, y + 21, 154, 24}, "Mirror Duplicate")) { apply(mirrorSelection); return; }
    if (button({x + 300, y + 21, 214, 24}, "Cursor to Selection Center") && validSel()) {
        gCursor = selectionPivot(Pivot::Bounds, transformRoots());
        gCursorVisible = true;
    }
    note({x, y + 53, 398, 28}, "G/R/S choose a tool. X/Y/Z lock an axis.\nShift+right-click places the cursor.");
    labFooter(p, true);
}

void addSimulationSource(bool liquid) {
    closeLab(false);
    defer([liquid] {
        gNoPush = false;
        beginInteraction();
        ObjType kind = liquid ? ObjType::Cube : ObjType::Sphere;
        auto object = makeObject(kind, ++gCounter[(int)kind]);
        const V3 target = gScene.viewCamera().target;
        object.scl = liquid ? V3(1.5f) : V3(1.f);
        Hit surface;
        float top = target.y + 1000.f;
        float height = intersectScene(gScene, {{target.x, top, target.z}, {0, -1, 0}}, 2000.f, surface) ?
                       surface.p.y : target.y;
        object.pos = {target.x, height + 5, target.z};
        gStatus = liquid ? "Added a source above the scene." : "Added a body above the scene.";
        addObject(std::move(object));
        endInteraction();
        openLab(LabSimulation);
    });
}

void launchSimulation(bool play, int first) {
    gSimulationError.clear();
    auto rigid = gRigidSettings;
    auto liquid = gLiquidSettings;
    rigid.time.startFrame = liquid.time.startFrame = first;
    try {
        gSimulationBake = std::async(std::launch::async, [scene = gScene.snapshot(), rigid, liquid, play,
                                    useLiquid = gLiquidSimulation, selection = gSelSet, source = gSel]() mutable {
            SimulationBake result;
            auto& time = useLiquid ? liquid.time : rigid.time;
            result.last = time.startFrame + time.frames - 1;
            result.play = play;
            result.firstParticle = useLiquid ? (int)scene.objs.size() : -1;
            if (useLiquid) {
                result.particles = bakeLiquid(scene, source, liquid, result.error);
                result.ok = result.particles > 0;
            } else {
                result.ok = bakeRigidBodies(scene, selection, rigid, result.error);
            }
            if (result.ok) result.scene = std::move(scene);
            return result;
        });
    } catch (const std::exception&) {
        gSimulationError = "Could not start the simulation bake.";
    }
}

void drawSimulationLab() {
    R p = panel(650, gSimulationAdvanced ? 596 : 416, "Simulation");
    int x = p.x + 20, y = p.y + 44, w = p.w - 40, half = (w - 8) / 2;
    if (gSimulationBake.valid()) {
        label(x, y, gLiquidSimulation ? "Baking liquid motion..." : "Baking rigid-body motion...");
        note({x, y + 26, w, 42}, "The result will appear on the timeline when the bake finishes.");
        return;
    }
    if (button({x, y, half, 28}, "Rigid Bodies", !gLiquidSimulation)) {
        gLiquidSimulation = false;
        gSimulationError.clear();
    }
    if (button({x + half + 8, y, half, 28}, "Liquid", gLiquidSimulation)) {
        gLiquidSimulation = true;
        gSimulationError.clear();
    }
    y += 38;
    std::string selection = gLiquidSimulation ? (validSel() ? "Volume source  " + gScene.objs[gSel].name : "Select a volume source or add one.") :
                            (gSelSet.empty() ? "Select a solid or add one." : std::to_string(gSelSet.size()) + " selected for rigid-body motion.");
    textIn({x, y, w - 147, 25}, selection.c_str(), TXT, gFont, TextLeft | TextMiddle);
    if (button({x + w - 142, y, 142, 25}, gLiquidSimulation ? "Add Liquid Source" : "Add Drop Body")) {
        addSimulationSource(gLiquidSimulation);
        return;
    }
    y += 37;
    label(x, y, gLiquidSimulation ? "Liquid Preset" : "Body Preset");
    int count = gLiquidSimulation ? (int)LiquidPreset::COUNT : (int)RigidPreset::COUNT;
    int width = w / count;
    for (int i = 0; i < count; ++i) {
        auto liquid = (LiquidPreset)i;
        auto rigid = (RigidPreset)i;
        if (button({x + i * width, y + 19, width - 5, 26},
                   gLiquidSimulation ? liquidPresetName(liquid) : rigidPresetName(rigid),
                   gLiquidSimulation ? liquid == gLiquidSettings.preset : rigid == gRigidPreset)) {
            if (gLiquidSimulation) applyLiquidPreset(gLiquidSettings, liquid);
            else { gRigidPreset = rigid; applyRigidPreset(gRigidSettings, rigid); }
            gSimulationError.clear();
        }
    }
    y += 55;
    label(x, y, "Gravity Preset");
    auto& time = gLiquidSimulation ? gLiquidSettings.time : gRigidSettings.time;
    auto& gravity = gSimulationGravity[gLiquidSimulation ? 1 : 0];
    width = w / (int)GravityPreset::COUNT;
    for (int i = 0; i < (int)GravityPreset::COUNT; ++i) {
        auto preset = (GravityPreset)i;
        if (button({x + i * width, y + 19, width - 4, 26}, gravityPresetName(preset), gravity == preset, gSmall)) {
            gravity = preset;
            applyGravityPreset(time, preset);
            if (time.gravityMode != GravityMode::Direction && gCursorVisible) time.center = gCursor;
            gSimulationError.clear();
        }
    }
    y += 56;
    int first = gSimulationStartHere ? std::min(gScene.frame, 99998) : 0;
    int frameLimit = std::min(simulationFrameLimit(gScene.fps), 100000 - first);
    time.frames = std::clamp(time.frames, 2, frameLimit);
    sliderI({x, y, half, 24}, "Clip Frames", time.frames, 2, frameLimit);
    const char* detail[] = {"Draft", "Balanced", "Fine"};
    width = half / 3;
    for (int i = 0; i < 3; ++i) {
        if (button({x + half + 8 + i * width, y, width - 4, 24}, detail[i], time.detail == i)) {
            time.detail = i;
            if (gLiquidSimulation) gLiquidSettings.resolution = i == 0 ? 3 : i == 1 ? 4 : 8;
        }
    }
    y += 32;
    toggle({x, y, half, 24}, "Start at Current Frame", gSimulationStartHere);
    if (button({x + half + 8, y, half, 24}, gSimulationAdvanced ? "Hide Advanced" : "Advanced Settings", gSimulationAdvanced)) {
        gSimulationAdvanced = !gSimulationAdvanced;
        return;
    }
    y += 32;
    if (gSimulationAdvanced) {
        toggle({x, y, half, 24}, "Floor at Y = 0", time.floor);
        if (gLiquidSimulation) toggle({x + half + 8, y, half, 24}, "Hide Volume Source", gLiquidSettings.hideSource);
        y += 29;
        int third = (w - 16) / 3;
        auto vectorRow = [&](V3& value, const char* tx, const char* ty, const char* tz, float min, float max) {
            dragF({x, y, third, 24}, tx, value.x, .1f, min, max, "%.2f");
            dragF({x + third + 8, y, third, 24}, ty, value.y, .1f, min, max, "%.2f");
            dragF({x + 2 * (third + 8), y, third, 24}, tz, value.z, .1f, min, max, "%.2f");
            y += 29;
        };
        vectorRow(time.velocity, "Velocity X", "Velocity Y", "Velocity Z", -50, 50);
        if (slider({x, y, half, 24}, "Gravity Strength", time.gravity, 0, 50)) gravity = GravityPreset::COUNT;
        if (time.gravityMode != GravityMode::Direction)
            slider({x + half + 8, y, half, 24}, "Field Radius", time.radius, .1f, 1000, "%.2f", true);
        y += 29;
        if (time.gravityMode == GravityMode::Direction) {
            V3 old = time.direction;
            vectorRow(time.direction, "Gravity X", "Gravity Y", "Gravity Z", -10, 10);
            if (length(time.direction - old) > 1e-6f) gravity = GravityPreset::COUNT;
        } else {
            vectorRow(time.center, "Field Center X", "Field Center Y", "Field Center Z", -100000, 100000);
        }
        if (gLiquidSimulation) {
            sliderI({x, y, half, 24}, "Particle Grid", gLiquidSettings.resolution, 2, SIM_MAX_PARTICLE_GRID);
            slider({x + half + 8, y, half, 24}, "Viscosity", gLiquidSettings.viscosity, 0, 1);
            y += 29;
            slider({x, y, half, 24}, "Cohesion", gLiquidSettings.cohesion, 0, 1);
            slider({x + half + 8, y, half, 24}, "Surface Size", gLiquidSettings.surface, .8f, 1.3f);
        } else {
            bool custom = slider({x, y, third, 24}, "Bounce", gRigidSettings.bounce, 0, 1);
            custom |= slider({x + third + 8, y, third, 24}, "Friction", gRigidSettings.friction, 0, 1);
            custom |= slider({x + 2 * (third + 8), y, third, 24}, "Damping", gRigidSettings.damping, 0, 5);
            if (custom) gRigidPreset = RigidPreset::COUNT;
        }
        y += 33;
    }
    first = gSimulationStartHere ? std::min(gScene.frame, 99998) : 0;
    time.frames = std::min(time.frames, 100000 - first);
    char summary[96];
    std::snprintf(summary, sizeof summary, "Frames %d to %d   /   %.2f seconds at %d fps",
                  first, first + time.frames - 1, (time.frames - 1.f) / std::max(1, gScene.fps), gScene.fps);
    note({x, y, w, 34}, gSimulationError.empty() ? summary : gSimulationError.c_str());
    bool selected = gLiquidSimulation ? validSel() : !gSelSet.empty();
    int footerX = p.x + p.w - 100, footerY = p.y + p.h - 40;
    if (selected) {
        bool play = button({footerX - 144, footerY, 134, 28}, "Bake & Play", true, gBold);
        bool bake = button({footerX - 240, footerY, 88, 28}, "Bake");
        if (play || bake) {
            launchSimulation(play, first);
            return;
        }
    }
    labFooter(p, true);
}
void preview(R pv, const char* caption) {
    fill(pv, rgb(16, 16, 18));
    blitFit(gLabR, pv);
    bevel(pv, true);
    textIn({pv.x, pv.y + pv.h + 4, pv.w, 18}, gLabR.busy() ? "Rendering preview..." : caption, TXT_DIM, gSmall);
}

void drawObjectLab() {
    if (!validSel()) { closeLab(true); return; }
    Object& o = gScene.objs[gSel];
    R p = panel(660, isLight(o.type) ? 540 : 350, std::string(objTypeName(o.type)) + " Editor  -  " + o.name);
    int x = p.x + 16, y = p.y + 46;
    bool ch = false;
    if (o.type == ObjType::Picture) {
        Picture& picture = o.picture;
        int sel = gSel;
        if (button({x, y, 288, 24}, "Replace Picture..."))
            defer([sel] { if (loadPicture(gScene.objs[sel].picture)) sceneChanged(); });
        std::string filename = picture.image ? baseName(picture.path) : "Missing image: " + baseName(picture.path);
        textIn({x, y + 28, 288, 18}, filename.c_str(), TXT_DIM, gSmall, TextLeft | TextMiddle);
        ch |= toggle({x, y + 52, 142, 24}, "Face Camera", picture.billboard);
        ch |= toggle({x + 146, y + 52, 142, 24}, "Chroma Cutout", picture.chroma);
        ch |= toggle({x, y + 82, 142, 24}, "Cast Shadows", picture.shadows);
        ch |= toggle({x + 146, y + 82, 142, 24}, "In Reflections", picture.reflections);
        if (picture.chroma) {
            swatch({x, y + 112, 288, 24}, "Key Color", picture.keyColor);
            ch |= slider({x, y + 142, 288, 24}, "Key Tolerance", picture.keyTolerance, 0, 1);
        }
    } else if (o.type == ObjType::Water) {
        ch |= dragF({x, y, 288, 24}, "Water Level", o.pos.y, .05f); y += 30;
        ch |= toggle({x, y, 288, 24}, "Directional Waves", o.directionalWater); y += 30;
        if (o.directionalWater) {
            ch |= slider({x, y, 288, 24}, "Wind Direction", o.waveAngle, -180, 180, "%.0f"); y += 30;
            ch |= slider({x, y, 288, 24}, "Wavelength", o.waveLength, .1f, 50, "%.2f", true); y += 30;
            ch |= slider({x, y, 288, 24}, "Wave Height", o.waveHeight, 0, .7f); y += 30;
        }
        const char* water[] = {"Water", "Tropical Lagoon", "Deep Ocean"};
        for (int i = 0; i < 3; ++i)
            if (button({x + i * 96, y, 92, 24}, water[i], o.mat.name == water[i], gSmall)) { o.mat = findMaterial(water[i]); ch = true; }
    } else if (o.type == ObjType::Metaball) {
        ch |= slider({x, y, 288, 24}, "Influence Radius", o.metaRadius, 0.2f, 4.f);
        ch |= slider({x, y + 30, 288, 24}, "Field Strength", o.metaStrength, 0.1f, 4.f);
        ch |= toggle({x, y + 60, 288, 24}, "Subtract From Field", o.metaNegative);
        note({x, y + 96, 288, 82}, "Group overlapping metaballs to fuse them. A subtractive ball removes field inside that group. Group Boolean primitives with the balls to cut or intersect the fused shape.");
    } else if (o.type == ObjType::Torus) {
        TorusShape& t = o.torus;
        if (slider({x, y, 288, 24}, "Ring Radius", t.major, 0.1f, 3)) {
            t.minor = std::min(t.minor, t.major * 0.98f);
            ch = true;
        }
        ch |= slider({x, y + 30, 288, 24}, "Tube Radius", t.minor, 0.04f, t.major * 0.98f);
        ch |= slider({x, y + 60, 288, 24}, "Tube Height Ratio", t.section, 0.2f, 3);
        ch |= slider({x, y + 90, 288, 24}, "Start Angle", t.start, 0, 360, "%.0f");
        ch |= slider({x, y + 120, 288, 24}, "Sweep Angle", t.sweep, 5, 360, "%.0f");
    } else if (o.type == ObjType::Stone) {
        int seed = (int)std::min(o.seed, 1000000u);
        if (sliderI({x, y, 288, 24}, "Seed", seed, 0, 1000000)) { o.seed = (uint32_t)seed; ch = true; }
        if (button({x, y + 30, 288, 24}, "New Seed")) { o.seed = (++gSeed * 7919u) % 1000000u; ch = true; }
        ch |= slider({x, y + 60, 288, 24}, "Irregularity", o.stone.irregularity, 0, 2);
        ch |= slider({x, y + 90, 288, 24}, "Blockiness", o.stone.blockiness, 0, 1);
        ch |= slider({x, y + 120, 288, 24}, "Smoothing", o.stone.smoothing, 0, 1);
    } else if (o.type == ObjType::Mesh) {
        std::string info = o.mesh ? std::to_string(o.mesh->v.size()) + " vertices  /  " +
            std::to_string(o.mesh->triCount()) + " triangles" : "No mesh data";
        label(x, y, info.c_str(), 288);
        ch |= toggle({x, y + 26, 288, 24}, "Flat Shading", o.flatMesh);
        swatch({x, y + 70, 288, 24}, "Base Color", o.mat.color1);
        ch |= slider({x, y + 100, 288, 24}, "Diffuse", o.mat.diffuse, 0, 1);
    } else if (isLight(o.type)) {
        int ly = y;
        lightControls(o, x, ly, 288);
        if (o.type != ObjType::Light) row3(x, ly, 288, "Aim Rotation", o.rot, 0.5f);
        auto& l = o.lighting;
        const char* falloff[] = {"Soft", "Constant", "Linear", "Inverse Square"};
        if (button({x, ly, 288, 24}, (std::string("Falloff ") + falloff[(int)l.falloff]).c_str())) {
            l.falloff = (Falloff)(((int)l.falloff + 1) % (int)Falloff::COUNT);
            ch = true;
        }
        ly += 28;
        ch |= slider({x, ly, 288, 24}, "Falloff Range", l.range, 0.1f, 250, "%.1f", true); ly += 28;
        ch |= sliderI({x, ly, 288, 24}, "Area Shadow Samples", l.samples, 1, 16); ly += 28;
        ch |= toggle({x, ly, 142, 24}, "Enabled", l.enabled);
        ch |= toggle({x + 146, ly, 142, 24}, "Cast Shadows", l.casts); ly += 28;
        ch |= toggle({x, ly, 142, 24}, "Disk Area", l.disk);
        ch |= toggle({x + 146, ly, 142, 24}, "Visible Emitter", l.visible); ly += 28;
        int px = p.x + 324, py = y + 266;
        ch |= slider({px, py, 158, 24}, "Diffuse Light", l.diffuseStrength, 0, 2);
        ch |= slider({px + 162, py, 158, 24}, "Specular Light", l.specularStrength, 0, 2); py += 28;
        ch |= dragI({px, py, 158, 24}, "Target Group", l.targetGroup, .2f, 0, 1000000);
        if (button({px + 162, py, 158, 24}, l.excludeGroup ? "Exclude Target" : "Include Target")) {
            l.excludeGroup = !l.excludeGroup;
            ch = true;
        }
        note({px, py + 28, 320, 42}, "Group 0 lights all objects. Drag to set a group number.");
        if (o.type == ObjType::Spotlight) {
            int sel = gSel;
            if (button({x, ly, 188, 24}, l.gel.empty() ? "Load Projected Gel..." : baseName(l.gel).c_str())) defer([sel] {
                std::string path;
                if (!fileDialog(false, IMAGE_FILTER, "png", path)) return;
                auto img = loadImageCached(path, true);
                if (!img) { gStatus = "Could not load the projector gel."; return; }
                auto& effect = gScene.objs[sel].lighting;
                effect.gel = path;
                effect.gelImage = std::move(img);
                sceneChanged();
            });
            if (button({x + 192, ly, 96, 24}, "Clear Gel")) { l.gel.clear(); l.gelImage.reset(); ch = true; }
        }
    }
    if (ch) sceneChanged();
    preview({p.x + 324, y, 320, 240}, "Scene preview");
    labFooter(p);
}

void drawAttributesLab() {
    if (!validSel()) { closeLab(true); return; }
    Object& o = gScene.objs[gSel];
    R p = panel(390, 280, "Attributes  -  " + o.name);
    int x = p.x + 20, y = p.y + 48, w = p.w - 40;
    row3(x, y, w, "Position", o.pos, 0.05f);
    if (o.type != ObjType::Light) row3(x, y, w, "Rotation", o.rot, 0.5f);
    if (!isLight(o.type)) row3(x, y, w, "Size", o.scl, 0.02f, 0.01f);
    bool ch = toggle({x, y + 6, 168, 24}, "Hidden", o.hidden);
    ch |= toggle({x + 182, y + 6, 168, 24}, "Locked", o.locked);
    if (ch) sceneChanged();
    labFooter(p);
}

template <class T> int grid2(int x, int y, const std::vector<T>& items, std::function<const char*(const T&)> name, std::function<bool(const T&)> on,
                             std::function<void(const T&)> act, int bw = 110) {
    for (size_t i = 0; i < items.size(); i++)
        if (button({x + (int)(i % 2) * (bw + 4), y + (int)(i / 2) * 26, bw, 22}, name(items[i]), on(items[i]), gSmall)) act(items[i]);
    return y + (int)((items.size() + 1) / 2) * 26;
}

void drawTextureLayers(Material& m) {
    R p = panel(1040, 640, "Texture Layers");
    int x = p.x + 16, y = p.y + 46, sel = gSel;
    auto& layers = m.layers;
    bool ch = false;
    if (button({x, y, 216, 24}, "Base Material")) gMatLayers = false;
    y += 36;
    if ((int)layers.size() < MAX_TEXTURE_LAYERS) {
        if (button({x, y, 106, 24}, "Add Layer")) {
            layers.emplace_back();
            layers.back().tex = TexType::Noise;
            gLayerIndex = (int)layers.size() - 1;
            ch = true;
        }
        if (button({x + 110, y, 106, 24}, "Load Texture...")) {
            defer([sel] {
                std::string path;
                if (!fileDialog(false, "Texture Preset (*.c3tex)\0*.c3tex\0", "c3tex", path)) return;
                TextureLayer layer;
                if (!layer.load(path)) { gStatus = "Could not read texture preset " + path + "."; return; }
                auto& items = gScene.objs[sel].mat.layers;
                items.push_back(std::move(layer));
                gLayerIndex = (int)items.size() - 1;
                sceneChanged();
            });
        }
    }
    y += 30;
    if (!layers.empty()) {
        gLayerIndex = std::clamp(gLayerIndex, 0, (int)layers.size() - 1);
        bool up = button({x, y, 62, 24}, "Up");
        bool down = button({x + 66, y, 62, 24}, "Down");
        int next = std::clamp(gLayerIndex + (down ? 1 : 0) - (up ? 1 : 0), 0, (int)layers.size() - 1);
        if (next != gLayerIndex) {
            std::swap(layers[gLayerIndex], layers[next]);
            gLayerIndex = next;
            ch = true;
        }
        if (button({x + 132, y, 84, 24}, "Remove")) {
            layers.erase(layers.begin() + gLayerIndex);
            gLayerIndex = std::max(0, std::min(gLayerIndex, (int)layers.size() - 1));
            ch = true;
        }
    }
    y += 32;
    label(x, y, "Layers in blend order", 216);
    y += 20;
    for (int i = 0; i < (int)layers.size(); i++) {
        std::string name = std::to_string(i + 1) + ". " + texName(layers[i].tex);
        if (button({x, y + i * 24, 216, 22}, name.c_str(), gLayerIndex == i, gSmall)) gLayerIndex = i;
    }

    const char* channel[] = {"Color", "Bump", "Specular", "Reflection", "Transparency", "Ambient"};
    std::string affected;
    if (!layers.empty()) {
        TextureLayer& layer = layers[gLayerIndex];
        int tx = p.x + 250;
        y = p.y + 46;
        label(tx, y, "Texture");
        y += 20;
        for (int i = 0; i < (int)TexType::COUNT; i++)
            if (button({tx + (i % 2) * 110, y + (i / 2) * 22, 106, 20}, texName((TexType)i), layer.tex == (TexType)i, gSmall)) {
                layer.tex = (TexType)i;
                ch = true;
            }
        y += ((int)TexType::COUNT + 1) / 2 * 22 + 6;
        label(tx, y, "Coordinate source");
        y += 20;
        for (int i = 0; i < (int)TexMap::COUNT; i++)
            if (button({tx + (i % 2) * 110, y + (i / 2) * 22, 106, 20}, mapName((TexMap)i), layer.map == (TexMap)i, gSmall)) {
                layer.map = (TexMap)i;
                ch = true;
            }
        y += ((int)TexMap::COUNT + 1) / 2 * 22 + 6;
        swatch({tx, y, 216, 24}, "Color A", layer.color1); y += 28;
        swatch({tx, y, 216, 24}, "Color B", layer.color2); y += 28;
        if (button({tx, y, 216, 24}, layer.image.empty() ? "Load BMP..." : baseName(layer.image).c_str())) {
            int i = gLayerIndex;
            defer([sel, i] {
                std::string path;
                if (!fileDialog(false, IMAGE_FILTER, "png", path)) return;
                auto image = loadImageCached(path, true);
                if (!image) { gStatus = "Could not read picture " + path + "."; return; }
                auto& item = gScene.objs[sel].mat.layers[i];
                item.image = path;
                item.img = std::move(image);
                item.tex = TexType::Image;
                sceneChanged();
            });
        }
        y += 30;
        ch |= slider({tx, y, 216, 24}, "Scale", layer.texScale, 0.01f, 20, "%.3f", true); y += 30;
        row3(tx, y, 216, "Offset", layer.offset, 0.02f);
        row3(tx, y, 216, "Rotation", layer.rotation, 0.5f);

        int ax = p.x + 484;
        y = p.y + 46;
        label(ax, y, "Blend");
        const char* blend[] = {"Mix", "Multiply", "Add", "Subtract", "Screen"};
        for (int i = 0; i < (int)TexBlend::COUNT; i++)
            if (button({ax + (i % 3) * 72, y + 20 + (i / 3) * 24, 68, 22}, blend[i], (int)layer.blend == i, gSmall)) {
                layer.blend = (TexBlend)i;
                ch = true;
            }
        y += 70;
        const char* mask[] = {"No Mask", "Mask: Luminance", "Mask: Inverse Luminance", "Mask: Previous Layer", "Mask: Elevation", "Mask: Slope"};
        if (button({ax, y, 212, 24}, mask[(int)layer.mask])) {
            layer.mask = (TexMask)(((int)layer.mask + 1) % (int)TexMask::COUNT);
            ch = true;
        }
        y += 30;
        ch |= slider({ax, y, 212, 24}, "Opacity", layer.opacity, 0, 1); y += 34;
        label(ax, y, "Channel influence", 212); y += 20;
        for (int i = 0; i < TexChannelCount; i++) {
            ch |= slider({ax, y, 212, 24}, channel[i], layer.influence[i], 0, 1); y += 30;
            if (layer.influence[i] > 0 && layer.opacity > 0) {
                if (!affected.empty()) affected += ", ";
                affected += channel[i];
            }
        }
        const char* outputs[] = {"Default", "Luminance", "Alpha", "Red", "Green", "Blue"};
        auto chooseOutput = [&](const char* label, TexOutput& output) {
            std::string caption = std::string(label) + " " + outputs[(int)output];
            if (button({ax, y, 212, 24}, caption.c_str())) {
                output = (TexOutput)(((int)output + 1) % (int)TexOutput::COUNT);
                ch = true;
            }
            y += 28;
        };
        chooseOutput("Bump Source", layer.heightOutput);
        chooseOutput("Transparency Source", layer.transparencyOutput);
        if (button({ax, y, 212, 24}, "Save Texture...")) {
            TextureLayer preset = layer;
            defer([preset] {
                std::string path;
                if (fileDialog(true, "Texture Preset (*.c3tex)\0*.c3tex\0", "c3tex", path))
                    gStatus = preset.save(path) ? "Saved texture preset " + baseName(path) + "." : "Could not save texture preset " + path + ".";
            });
        }
    }
    preview({p.x + 714, p.y + 46, 310, 240}, "Material preview");
    if (!layers.empty()) {
        TextureLayer& layer = layers[gLayerIndex];
        int sx = p.x + 714, sy = p.y + 315;
        if (layer.tex != TexType::Image) {
            ch |= slider({sx, sy, 310, 24}, "Coordinate Warp", layer.warp, 0, 4); sy += 28;
            if (layer.tex == TexType::Noise || layer.tex == TexType::Ridged) {
                ch |= sliderI({sx, sy, 310, 24}, "Noise Octaves", layer.octaves, 1, 10); sy += 28;
            }
        }
        ch |= slider({sx, sy, 152, 24}, "Contrast", layer.contrast, 0, 4);
        ch |= slider({sx + 158, sy, 152, 24}, "Bias", layer.bias, -1, 1); sy += 28;
        if (layer.mask == TexMask::Elevation || layer.mask == TexMask::Slope) {
            ch |= slider({sx, sy, 152, 24}, "Mask From", layer.maskLow, 0, 1);
            ch |= slider({sx + 158, sy, 152, 24}, "Mask To", layer.maskHigh, 0, 1); sy += 28;
            ch |= slider({sx, sy, 310, 24}, "Mask Feather", layer.maskFeather, .001f, .5f); sy += 28;
            note({sx, sy, 310, 32}, layer.mask == TexMask::Slope ? "Slope runs from flat (0) to vertical (1)." : "Elevation is measured in object space.");
            sy += 36;
        }
        note({sx, sy, 310, 52}, ("Selected layer affects " + (affected.empty() ? std::string("no channels.") : affected + ".")).c_str());
    }
    if (ch) sceneChanged();
    labFooter(p);
}

void drawMatLab() {
    if (!validSel()) { gLab = LabNone; return; }
    int sel = gSel;
    Object& o = gScene.objs[sel];
    Material& m = o.mat;
    if (gMatLayers) { drawTextureLayers(m); return; }
    bool ch = false;
    R P = panel(1100, 640, "Materials Lab  -  " + o.name + (gSelSet.size() > 1 ? "  (OK applies to all selected)" : ""));
    int x = P.x + 16, y = P.y + 46;
    auto cats = categories(false);
    cats.push_back("Library");
    gMatCat = std::min(gMatCat, (int)cats.size() - 1);
    label(x, y, "Category");
    y = grid2<std::string>(x, y + 20, cats, [](const std::string& s) { return s.c_str(); }, [&](const std::string& s) { return cats[gMatCat] == s; },
                           [&](const std::string& s) { gMatCat = (int)(std::find(cats.begin(), cats.end(), s) - cats.begin()); }) + 8;
    bool lib = gMatCat == (int)cats.size() - 1;
    std::vector<const Material*> list;
    if (lib) for (auto& lm : gLib.mats) list.push_back(&lm);
    else for (auto& pm : materialPresets()) if (pm.category == cats[gMatCat]) list.push_back(&pm);
    label(x, y, lib ? "Your saved materials" : "Presets");
    y = grid2<const Material*>(x, y + 20, list, [](const Material* const& p) { return p->name.c_str(); }, [&](const Material* const& p) { return p->name == m.name; },
                               [&](const Material* const& p) { m = *p; ch = true; }) + 8;
    if (lib && list.empty()) { note({x, y, 228, 40}, "Empty. Use Add to Library to keep your own materials."); y += 40; }
    if (button({x, y, 110, 24}, "Add to Library")) {
        rememberLibrary();
        Material c = m;
        c.category = "Library";
        c.img.reset();
        gLib.mats.erase(std::remove_if(gLib.mats.begin(), gLib.mats.end(), [&](const Material& e) { return e.name == c.name; }), gLib.mats.end());
        gLib.mats.push_back(c);
        saveLibrary();
        gStatus = "Added \"" + c.name + "\" to the library.";
    }
    if (lib && button({x + 114, y, 110, 24}, "Remove")) {
        rememberLibrary();
        gLib.mats.erase(std::remove_if(gLib.mats.begin(), gLib.mats.end(), [&](const Material& e) { return e.name == m.name; }), gLib.mats.end());
        saveLibrary();
    }
    int x2 = P.x + 260;
    y = P.y + 46;
    label(x2, y, "Texture");
    for (int t = 0; t < (int)TexType::COUNT; t++)
        if (button({x2 + (t % 2) * 114, y + 20 + (t / 2) * 26, 110, 22}, texName((TexType)t), m.tex == (TexType)t, gSmall)) { m.tex = (TexType)t; ch = true; }
    y += 20 + (((int)TexType::COUNT + 1) / 2) * 26 + 6;
    label(x2, y, "Mapping");
    for (int t = 0; t < (int)TexMap::COUNT; t++)
        if (button({x2 + (t % 2) * 114, y + 20 + (t / 2) * 26, 110, 22}, mapName((TexMap)t), m.map == (TexMap)t, gSmall)) { m.map = (TexMap)t; ch = true; }
    y += 20 + (((int)TexMap::COUNT + 1) / 2) * 26 + 6;
    label(x2, y, "Colors");
    y += 20;
    swatch({x2, y, 224, 24}, "Color A", m.color1);
    y += 28;
    swatch({x2, y, 224, 24}, "Color B", m.color2);
    y += 34;
    if (button({x2, y, 224, 24}, "Load Picture...")) {
        defer([sel] {
            std::string p;
            if (!fileDialog(false, IMAGE_FILTER, "png", p) || sel >= (int)gScene.objs.size()) return;
            auto img = loadImageCached(p);
            if (!img) { gStatus = "Could not read picture " + p + "."; return; }
            Material& mm = gScene.objs[sel].mat;
            mm.image = p;
            mm.img = img;
            mm.tex = TexType::Image;
            sceneChanged();
        });
    }
    y += 28;
    textIn({x2, y, 224, 18}, m.image.empty() ? "(no picture loaded)" : baseName(m.image).c_str(), TXT_DIM, gSmall, TextLeft | TextMiddle);
    int x3 = P.x + 504, sw = 200;
    y = P.y + 46;
    label(x3, y, "Channels");
    y += 20;
    struct S { const char* n; float* v; float lo, hi; const char* fmt; bool lg; } ss[] = {
        {"Diffuse", &m.diffuse, 0, 1, "%.2f", false},      {"Ambient", &m.ambient, 0, 1, "%.2f", false},
        {"Specular", &m.specular, 0, 1, "%.2f", false},    {"Shininess", &m.shininess, 1, 250, "%.0f", false},
        {"Reflection", &m.reflect, 0, 1, "%.2f", false},   {"Transparency", &m.transparency, 0, 1, "%.2f", false},
        {"Refraction", &m.ior, 1, 2.5f, "%.2f", false},    {"Bump", &m.bump, 0, 2, "%.2f", false},
        {"Scale", &m.texScale, 0.01f, 20, "%.3f", true},   {"Metallic", &m.metal, 0, 1, "%.2f", false},
        {"Glow", &m.emission, 0, 3, "%.2f", false},        {"Blur", &m.blur, 0, 1, "%.2f", false},
    };
    for (auto& s : ss) { ch |= slider({x3, y, sw, 24}, s.n, *s.v, s.lo, s.hi, s.fmt, s.lg); y += 30; }
    note({x3, y + 4, sw, 60}, "Blur gives frosted glass and brushed reflections in Premium renders.");
    preview({P.x + P.w - 336, P.y + 46, 320, 320}, m.name.c_str());
    std::string layers = "Texture Layers (" + std::to_string(m.layers.size()) + ")...";
    if (button({P.x + P.w - 336, P.y + 396, 320, 26}, layers.c_str())) gMatLayers = true;
    int fx = P.x + P.w - 336, fy = P.y + 434;
    ch |= slider({fx, fy, 320, 24}, "Absorption", m.absorption, 0, 10); fy += 30;
    ch |= slider({fx, fy, 320, 24}, "Volume Density", m.volumeDensity, 0, 8); fy += 30;
    ch |= slider({fx, fy, 320, 24}, "Volume Scattering", m.volumeScatter, 0, 1); fy += 30;
    ch |= slider({fx, fy, 320, 24}, "Volume Noise", m.volumeNoise, 0, 1);
    if (ch) sceneChanged();
    labFooter(P);
}

void addLightRig(int style) {
    int group = gScene.maxGroup();
    if (group == (std::numeric_limits<int>::max)()) { gStatus = "Scene group limit reached."; return; }
    ++group;
    int count = style == 0 ? 12 : (style == 1 ? 5 : 3);
    V3 center = validSel() ? gScene.objs[gSel].worldPos : gScene.cam.target;
    float radius = std::max(6.f, gScene.cam.dist * .3f);
    for (int i = 0; i < count; ++i) {
        bool dome = style == 0;
        auto type = dome ? ObjType::Parallel : ObjType::Light;
        Object light = makeObject(type, ++gCounter[(int)type]);
        light.name = std::string(dome ? "Dome " : (style == 1 ? "Cluster " : "Fill ")) + std::to_string(i + 1);
        float angle = i * (dome ? 2.3999632f : (2 * PI / count));
        float up = dome ? (i + .5f) / count : .55f;
        float side = std::sqrt(1 - up * up);
        V3 direction(std::cos(angle) * side, up, std::sin(angle) * side);
        light.pos = center + direction * radius;
        light.rot = {std::acos(up) * 180 / PI, std::atan2(direction.x, direction.z) * 180 / PI, 0};
        light.lightPower = (dome ? 1.2f : (style == 1 ? 1.6f : .75f)) / count;
        light.lightColor = dome ? V3(.78f, .86f, 1.f) : V3(1.f, .93f, .83f);
        light.lighting.falloff = Falloff::InverseSquare;
        light.lighting.range = radius;
        light.lighting.casts = style != 2;
        light.lightSize = dome ? .6f : radius * .12f;
        light.group = group;
        gScene.objs.push_back(std::move(light));
    }
    gStatus = "Added " + std::to_string(count) + " grouped lights.";
    sceneChanged();
}

void drawSkyLab() {
    Sky& k = gScene.sky;
    bool ch = false;
    R P = panel(1100, 640, "Sky Lab");
    int x = P.x + 16, y = P.y + 46, w = 228;
    auto cats = categories(true);
    cats.push_back("Library");
    gLabSkyCat = std::min(gLabSkyCat, (int)cats.size() - 1);
    label(x, y, "Category");
    y = grid2<std::string>(x, y + 20, cats, [](const std::string& s) { return s.c_str(); }, [&](const std::string& s) { return cats[gLabSkyCat] == s; },
                           [&](const std::string& s) { gLabSkyCat = (int)(std::find(cats.begin(), cats.end(), s) - cats.begin()); }) + 8;
    bool lib = gLabSkyCat == (int)cats.size() - 1;
    std::vector<const Sky*> list;
    if (lib) for (auto& s : gLib.skies) list.push_back(&s);
    else for (auto& s : skyPresets()) if (s.category == cats[gLabSkyCat]) list.push_back(&s);
    label(x, y, lib ? "Your saved skies" : "Presets");
    y = grid2<const Sky*>(x, y + 20, list, [](const Sky* const& s) { return s->name.c_str(); }, [&](const Sky* const& s) { return s->name == k.name; },
                          [&](const Sky* const& s) { k = *s; ch = true; }) + 8;
    if (button({x, y, 110, 24}, "Add to Library")) {
        rememberLibrary();
        Sky c = k;
        c.category = "Library";
        gLib.skies.erase(std::remove_if(gLib.skies.begin(), gLib.skies.end(), [&](const Sky& e) { return e.name == c.name; }), gLib.skies.end());
        gLib.skies.push_back(c);
        saveLibrary();
        gStatus = "Added \"" + c.name + "\" to the library.";
    }
    if (lib && button({x + 114, y, 110, 24}, "Remove")) {
        rememberLibrary();
        gLib.skies.erase(std::remove_if(gLib.skies.begin(), gLib.skies.end(), [&](const Sky& e) { return e.name == k.name; }), gLib.skies.end());
        saveLibrary();
    }
    y += 34;
    label(x, y, "Sky Mode");
    for (int i = 0; i < 4; i++)
        if (button({x + (i % 2) * 114, y + 20 + (i / 2) * 26, 110, 22}, skyModeName(i), k.mode == i, gSmall)) { k.mode = i; ch = true; }
    y += 20 + 52 + 8;
    swatch({x, y, w, 24}, "Zenith", k.zenith); y += 28;
    swatch({x, y, w, 24}, "Horizon", k.horizon); y += 28;
    swatch({x, y, w, 24}, "Sun / Light", k.sunColor);

    int x2 = P.x + 260, w2 = 220;
    y = P.y + 46;
    label(x2, y, "Sun");
    y += 20;
    ch |= slider({x2, y, w2, 24}, "Altitude", k.sunAlt, -10, 90, "%.0f"); y += 28;
    ch |= slider({x2, y, w2, 24}, "Azimuth", k.sunAz, 0, 360, "%.0f"); y += 28;
    ch |= slider({x2, y, w2, 24}, "Disc Size", k.sunSize, 0.2f, 8, "%.1f"); y += 28;
    ch |= toggle({x2, y, w2, 24}, k.sunVisible ? "Sun Disc: Visible" : "Sun Disc: Hidden", k.sunVisible); y += 34;
    label(x2, y, "Moon");
    y += 20;
    ch |= toggle({x2, y, w2, 24}, k.moon ? "Moon: On" : "Moon: Off", k.moon); y += 28;
    ch |= slider({x2, y, w2, 24}, "Altitude", k.moonAlt, -10, 90, "%.0f"); y += 28;
    ch |= slider({x2, y, w2, 24}, "Azimuth", k.moonAz, 0, 360, "%.0f"); y += 28;
    ch |= slider({x2, y, w2, 24}, "Phase", k.moonPhase, 0, 1); y += 28;
    ch |= slider({x2, y, w2, 24}, "Size", k.moonSize, 0.3f, 10, "%.1f"); y += 34;
    swatch({x2, y, w2, 24}, "Haze Color", k.hazeColor); y += 28;
    swatch({x2, y, w2, 24}, "Fog Color", k.fogColor);

    int x3 = P.x + 496;
    y = P.y + 46;
    label(x3, y, "Atmosphere & Clouds");
    y += 20;
    struct S { const char* n; float* v; float lo, hi; const char* fmt; bool lg; } ss[] = {
        {"Haze", &k.haze, 0, 1, "%.2f", false},           {"Fog Density", &k.fog, 0, 1, "%.2f", false},
        {"Fog Height", &k.fogHeight, 0, 40, "%.1f", false}, {"Cumulus", &k.clouds, 0, 1, "%.2f", false},
        {"Stratus", &k.stratus, 0, 1, "%.2f", false},      {"Cloud Scale", &k.cloudScale, 0.1f, 5, "%.2f", true},
        {"Ambient", &k.ambient, 0, 1.5f, "%.2f", false},   {"Stars", &k.stars, 0, 1, "%.2f", false},
        {"Rainbow", &k.rainbow, 0, 1, "%.2f", false},
    };
    for (auto& s : ss) { ch |= slider({x3, y, w2, 24}, s.n, *s.v, s.lo, s.hi, s.fmt, s.lg); y += 28; }
    ch |= toggle({x3, y, w2, 24}, k.shadows ? "Shadows: On" : "Shadows: Off", k.shadows); y += 34;
    swatch({x3, y, w2, 24}, "Cloud Color", k.cloudColor); y += 28;
    swatch({x3, y, w2, 24}, "Ambient Color", k.ambientColor);
    preview({P.x + P.w - 356, P.y + 46, 340, 164}, "Camera view preview");
    int sx = P.x + P.w - 356, sy = P.y + 242;
    ch |= slider({sx, sy, 340, 24}, "Cloud Altitude", k.cloudAltitude, 1, 200); sy += 28;
    ch |= slider({sx, sy, 340, 24}, "Cloud Thickness", k.cloudThickness, 0.1f, 30); sy += 28;
    ch |= slider({sx, sy, 340, 24}, "Cloud Shadows", k.cloudShadow, 0, 1); sy += 28;
    ch |= slider({sx, sy, 340, 24}, "Sky Lighting", k.skylight, 0, 2); sy += 28;
    swatch({sx, sy, 340, 24}, "Stratus Tint", k.stratusColor); sy += 28;
    swatch({sx, sy, 340, 24}, "Moon Tint", k.moonColor); sy += 28;
    if (button({sx, sy, 244, 24}, k.environment.empty() ? "Load HDR Environment..." : baseName(k.environment).c_str())) defer([] {
        std::string path;
        if (!fileDialog(false, HDR_FILTER, "hdr", path)) return;
        auto image = loadImageCached(path, true);
        if (!image || !image->hdr) { gStatus = "Could not read this Radiance HDR image."; return; }
        gScene.sky.environment = path;
        gScene.sky.environmentImage = std::move(image);
        sceneChanged();
    });
    if (button({sx + 250, sy, 90, 24}, "Clear HDR") && !k.environment.empty()) {
        k.environment.clear();
        k.environmentImage.reset();
        ch = true;
    }
    sy += 28;
    if (!k.environment.empty()) {
        ch |= slider({sx, sy, 166, 24}, "HDR Rotation", k.environmentYaw, -180, 180, "%.0f");
        ch |= slider({sx + 172, sy, 168, 24}, "HDR Strength", k.environmentStrength, 0, 16); sy += 28;
        float samples = (float)k.environmentSamples;
        if (slider({sx, sy, 166, 24}, "HDR Samples", samples, 1, 64, "%.0f")) {
            k.environmentSamples = (int)std::lround(samples);
            ch = true;
        }
        ch |= toggle({sx + 172, sy, 168, 24}, "HDR Background", k.environmentBackground);
        sy += 28;
    }
    ch |= slider({sx, sy, 110, 24}, "Indirect Light", k.indirectStrength, 0, 2);
    ch |= sliderI({sx + 114, sy, 110, 24}, "Samples", k.indirectSamples, 1, 16);
    ch |= sliderI({sx + 228, sy, 112, 24}, "Bounces", k.indirectBounces, 1, 4);
    sy += 28;
    const char* rigs[] = {"Add Dome", "Add Cluster", "Add Fill"};
    for (int i = 0; i < 3; ++i)
        if (button({sx + i * 114, sy, 110, 24}, rigs[i])) addLightRig(i);
    if (ch) sceneChanged();
    labFooter(P);
}

void rememberTerrain(const Heightfield& f) {
    if (gTerrainUndo.size() >= 8) gTerrainUndo.erase(gTerrainUndo.begin());
    gTerrainUndo.push_back({f.res, f.h, {}});
    size_t cells = 0;
    for (const auto& entry : gTerrainUndo) cells += entry.h.size();
    while (cells > 33554432 && gTerrainUndo.size() > 1) {
        cells -= gTerrainUndo.front().h.size();
        gTerrainUndo.erase(gTerrainUndo.begin());
    }
    gTerrainRedo.clear();
}

void terrainHistory(bool redo) {
    if (!validSel() || !gScene.objs[gSel].hf) return;
    auto& f = *gScene.objs[gSel].hf;
    auto& from = redo ? gTerrainRedo : gTerrainUndo;
    auto& to = redo ? gTerrainUndo : gTerrainRedo;
    if (from.empty()) return;
    to.push_back({f.res, std::move(f.h), {}});
    f = std::move(from.back());
    from.pop_back();
    f.computeNormals();
    sceneChanged();
}

void drawTerrainLab() {
    if (!validSel() || !gScene.objs[gSel].hf) { gLab = LabNone; return; }
    Object& o = gScene.objs[gSel];
    Heightfield& f = *o.hf;
    bool ch = false;
    R P = panel(1100, 640, std::string(o.type == ObjType::Lattice ? "Symmetric Lattice  -  " : "Terrain Editor  -  ") + o.name);
    R hm{P.x + 16, P.y + 46, 400, 400};
    static std::vector<uint32_t> img;
    img.resize((size_t)hm.w * hm.h);
    for (int y = 0; y < hm.h; y++)
        for (int x = 0; x < hm.w; x++) {
            uint32_t v = (uint32_t)(clampf(f.at(x * (f.res - 1) / (hm.w - 1), y * (f.res - 1) / (hm.h - 1)), 0, 1) * 255);
            img[(size_t)y * hm.w + x] = (v << 16) | (v << 8) | v;
        }
    blit(hm, img.data(), hm.w, hm.h);
    bevel(hm, true);
    int id = newId();
    press(hm, id);
    if (hov(hm) && !gRegionTool) {
        int rp = (int)(gBrushSize * hm.w);
        circle(gIn.mx, gIn.my, rp, rgb(255, 80, 60));
    }
    if (gActive == id && gIn.down && gRegionTool) {
        gRegionEnd = {clampf((gIn.mx - hm.x) / (float)hm.w, 0, 1), 0, clampf((gIn.my - hm.y) / (float)hm.h, 0, 1)};
        if (gIn.pressed) gRegionStart = gRegionEnd;
        gRegionMask = true;
    }
    V3 low = gRegionMask ? V3(std::min(gRegionStart.x, gRegionEnd.x), 0, std::min(gRegionStart.z, gRegionEnd.z)) : V3();
    V3 high = gRegionMask ? V3(std::max(gRegionStart.x, gRegionEnd.x), 0, std::max(gRegionStart.z, gRegionEnd.z)) : V3(1);
    HeightRegion region{(int)(low.x * (f.res - 1)), (int)(low.z * (f.res - 1)),
                        (int)(high.x * (f.res - 1)), (int)(high.z * (f.res - 1))};
    if (gRegionMask) {
        int x0 = hm.x + (int)(low.x * (hm.w - 1)), x1 = hm.x + (int)(high.x * (hm.w - 1));
        int y0 = hm.y + (int)(low.z * (hm.h - 1)), y1 = hm.y + (int)(high.z * (hm.h - 1));
        Color c = rgb(255, 210, 55);
        line(x0, y0, x1, y0, c); line(x1, y0, x1, y1, c);
        line(x1, y1, x0, y1, c); line(x0, y1, x0, y0, c);
    }
    if (gActive == id && gIn.down && !gRegionTool) {
        if (gIn.pressed) rememberTerrain(f);
        BrushMode bm = gBrush;
        if (gIn.shift && (bm == BrushMode::Raise || bm == BrushMode::Lower)) bm = bm == BrushMode::Raise ? BrushMode::Lower : BrushMode::Raise;
        hfBrush(f, (gIn.mx - hm.x) / (float)hm.w, (gIn.my - hm.y) / (float)hm.h, gBrushSize, gBrushStr * 0.03f, bm, &region);
        ch = true;
    }
    int ry = hm.y + hm.h + 8;
    toggle({hm.x, ry, 128, 24}, "Select Region", gRegionTool);
    if (button({hm.x + 132, ry, 92, 24}, "Clear Mask")) gRegionMask = false;
    if (button({hm.x + 228, ry, 82, 24}, "Copy")) gTerrainClipboard = hfCopyRegion(f, region);
    if (button({hm.x + 314, ry, 86, 24}, "Paste") && !gTerrainClipboard.values.empty()) {
        rememberTerrain(f);
        hfPasteRegion(f, region, gTerrainClipboard);
        ch = true;
    }
    if (button({hm.x, ry + 28, 128, 24}, "Undo Terrain Edit")) terrainHistory(false);
    if (button({hm.x + 132, ry + 28, 132, 24}, "Redo Terrain Edit")) terrainHistory(true);
    if (button({hm.x, ry + 56, 128, 24}, "Make Tileable")) {
        rememberTerrain(f);
        hfMakeTileable(f, .15f);
        ch = true;
    }
    bool mirrorRight = button({hm.x, ry + 84, 195, 24}, "Mirror Left to Right");
    bool mirrorLeft = button({hm.x + 205, ry + 84, 195, 24}, "Mirror Right to Left");
    if (mirrorRight || mirrorLeft) {
        rememberTerrain(f);
        hfMirrorX(f, mirrorRight);
        ch = true;
    }
    if (gTerrainBakeLayer >= (int)o.mat.layers.size()) gTerrainBakeLayer = -1;
    if (!o.mat.layers.empty()) {
        std::string source = gTerrainBakeLayer < 0 ? "Bake Source  Full Material" :
                             "Bake Source  Layer " + std::to_string(gTerrainBakeLayer + 1);
        if (button({hm.x + 268, ry + 56, 132, 24}, source.c_str(), false, gSmall))
            gTerrainBakeLayer = gTerrainBakeLayer + 1 < (int)o.mat.layers.size() ? gTerrainBakeLayer + 1 : -1;
    }
    if (button({hm.x + 268, ry + 28, 132, 24}, "Material to Height")) {
        Image heights = terrainMaterialHeightMap(o, std::min(f.res, 1025), gTerrainBakeLayer);
        if (!heights.px.empty()) {
            rememberTerrain(f);
            hfBlendImage(f, region, heights, gTerrainBlend, gTerrainBlendMask.get());
            ch = true;
            gStatus = "Blended the material bump or color into the selected terrain region.";
        }
    }

    int x = P.x + 432, y = P.y + 46;
    label(x, y, "Generate");
    y += 20;
    for (int i = 0; i < (int)TerrainKind::COUNT; i++)
        if (button({x + (i % 3) * 76, y + (i / 3) * 26, 72, 22}, terrainKindName((TerrainKind)i), false, gSmall)) {
            rememberTerrain(f);
            hfGenerate(f, (TerrainKind)i, ++gSeed, gRough);
            ch = true;
        }
    y += (((int)TerrainKind::COUNT + 2) / 3) * 26 + 6;
    label(x, y, "Filter");
    y += 20;
    struct Op { const char* n; std::function<void()> fn; } ops[] = {
        {"Erode", [&] { hfErode(f, (int)std::min(4000000.f, f.res * f.res * gErosionDensity), (uint32_t)gTerrainSeed); }},
        {"Smooth", [&] { hfSmooth(f); }},
        {"Sharpen", [&] { hfSharpen(f); }},                           {"Add Noise", [&] { hfNoise(f, ++gSeed, 0.06f); }},
        {"Terrace", [&] { hfTerrace(f, (int)gTerraces); }},           {"Plateau", [&] { hfPlateau(f, 0.6f); }},
        {"Spikes", [&] { hfSpikes(f, ++gSeed, 12); }},                {"Crater", [&] { hfCrater(f, 0.5f, 0.5f, 0.28f, 0.3f); }},
        {"Mound", [&] { hfMound(f); }},                               {"Round Edges", [&] { hfRoundEdges(f); }},
        {"Raise", [&] { hfAdd(f, 0.05f); }},                          {"Lower", [&] { hfAdd(f, -0.05f); }},
        {"Invert", [&] { hfInvert(f); }},                             {"Equalize", [&] { hfNormalize(f); }},
        {"Clear", [&] { hfInit(f, f.res); }},
    };
    int nops = (int)(sizeof ops / sizeof ops[0]);
    for (int i = 0; i < nops; i++)
        if (button({x + (i % 3) * 76, y + (i / 3) * 26, 72, 22}, ops[i].n, false, gSmall)) {
            rememberTerrain(f); ops[i].fn(); ch = true;
        }
    y += ((nops + 2) / 3) * 26 + 6;
    label(x, y, "Resolution");
    y += 20;
    const int resv[] = {129, 257, 513, 1025, 2049, MAX_TERRAIN_RESOLUTION};
    for (int i = 0; i < (int)std::size(resv); i++) {
        char b[16];
        snprintf(b, sizeof b, "%d", resv[i] - 1);
        if (button({x + i * 38, y, 36, 22}, b, f.res == resv[i], gSmall) && f.res != resv[i]) {
            rememberTerrain(f); hfResample(f, resv[i]); ch = true;
        }
    }
    y += 30;
    label(x, y, "Settings");
    y += 20;
    slider({x, y, 224, 24}, "Roughness", gRough, 0.3f, 0.7f); y += 28;
    slider({x, y, 224, 24}, "Terrace Steps", gTerraces, 2, 20, "%.0f"); y += 28;
    slider({x, y, 224, 24}, "Brush Size", gBrushSize, 0.01f, 0.3f, "%.2f", true); y += 28;
    slider({x, y, 224, 24}, "Brush Strength", gBrushStr, 0.05f, 1); y += 28;
    for (int i = 0; i < (int)BrushMode::COUNT; i++)
        if (button({x + (i % 4) * 57, y + (i / 4) * 26, 54, 22}, brushName((BrushMode)i), gBrush == (BrushMode)i, gSmall)) gBrush = (BrushMode)i;
    int sel = gSel;
    int x4 = P.x + P.w - 336;
    preview({x4, P.y + 46, 320, 178}, "Camera view preview");
    y = P.y + 250;
    label(x4, y, "Picture");
    y += 20;
    if (button({x4, y, 158, 24}, "Blend Height Map...")) {
        defer([sel, region] {
            std::string p;
            if (!fileDialog(false, IMAGE_FILTER, "png", p) || sel >= (int)gScene.objs.size() || !gScene.objs[sel].hf) return;
            auto im = loadImage(p);
            if (!im) { gStatus = "Could not read " + p; return; }
            rememberTerrain(*gScene.objs[sel].hf);
            hfBlendImage(*gScene.objs[sel].hf, region, *im, gTerrainBlend, gTerrainBlendMask.get());
            sceneChanged();
        });
    }
    if (button({x4 + 162, y, 158, 24}, "Export Height Map...")) {
        defer([sel] {
            std::string p;
            if (!fileDialog(true, "16-bit TIFF\0*.tif;*.tiff\0" "16-bit PGM\0*.pgm\0Bitmap\0*.bmp\0", "tif", p) ||
                sel >= (int)gScene.objs.size() || !gScene.objs[sel].hf) return;
            const Heightfield& hf = *gScene.objs[sel].hf;
            auto ext = std::filesystem::path(p).extension().string();
            for (char& c : ext) c = (char)std::tolower((unsigned char)c);
            bool saved = false;
            if (ext == ".tif" || ext == ".tiff") saved = saveHeightTiff(p, hf.res, hf.res, hf.h);
            else if (ext == ".pgm") {
                std::ofstream out(p, std::ios::binary);
                out << "P5\n" << hf.res << ' ' << hf.res << "\n65535\n";
                for (float h : hf.h) {
                    unsigned value = (unsigned)std::lround(clampf(h, 0, 1) * 65535);
                    out.put((char)(value >> 8)); out.put((char)(value & 255));
                }
                out.close();
                saved = (bool)out;
            }
            else if (ext == ".bmp") {
                std::vector<uint32_t> px(hf.h.size());
                for (size_t i = 0; i < px.size(); i++) {
                    uint32_t v = (uint32_t)(clampf(hf.h[i], 0, 1) * 255);
                    px[i] = (v << 16) | (v << 8) | v;
                }
                saved = saveBMP(p, hf.res, hf.res, px);
            }
            gStatus = saved ? "Exported height map " + p : "Could not write " + p;
        });
    }
    y += 32;
    if (button({x4, y, 226, 22}, gTerrainBlendMask ? "Replace Blend Mask..." : "Load Blend Mask...")) defer([] {
        std::string path;
        if (!fileDialog(false, IMAGE_FILTER, "png", path)) return;
        auto mask = loadImage(path);
        if (mask) gTerrainBlendMask = std::move(mask);
        else gStatus = "Could not read the blend mask.";
    });
    if (button({x4 + 232, y, 88, 22}, "Clear Mask")) gTerrainBlendMask.reset();
    y += 26;
    slider({x4, y, 320, 22}, "Height Map Blend", gTerrainBlend, 0, 1); y += 26;
    sliderI({x4, y, 155, 22}, "Seed", gTerrainSeed, 0, 99999);
    sliderI({x4 + 162, y, 158, 22}, "Octaves", gTerrainOctaves, 1, 10); y += 26;
    slider({x4, y, 155, 22}, "Frequency", gTerrainFrequency, 0.5f, 16);
    slider({x4 + 162, y, 158, 22}, "Erosion Density", gErosionDensity, 0.05f, 2); y += 26;
    if (button({x4, y, 320, 24}, o.type == ObjType::Lattice ? "Generate Symmetric Ridges" : "Generate Custom Fractal")) {
        rememberTerrain(f);
        hfCustomFractal(f, (uint32_t)gTerrainSeed, gRough, o.type == ObjType::Lattice, gTerrainFrequency, gTerrainOctaves);
        ch = true;
    }
    y += 28;
    ch |= slider({x4, y, 320, 24}, "Height Gain", o.terrain.height, 0.05f, 5); y += 28;
    ch |= slider({x4, y, 320, 24}, "Clip Low Points", o.terrain.clip, 0, 1); y += 28;
    ch |= slider({x4, y, 320, 24}, "Surface Smoothing", o.terrain.smoothing, 0, 1); y += 28;
    if (o.type == ObjType::Terrain) ch |= slider({x4, y, 320, 24}, "Base Thickness", o.terrain.thickness, 0, 2);
    note({x4, y + 32, 320, 42}, "A white mask blends at full strength. TIFF and PGM preserve 16-bit height precision.");
    if (ch) sceneChanged();
    labFooter(P);
}

void drawTreeLab() {
    auto members = treeMembers();
    if (members.empty()) { gLab = LabNone; return; }
    bool ch = false;
    R P = panel(900, 560, "Tree Lab");
    int x = P.x + 16, y = P.y + 46, w = 260;
    label(x, y, "Shape");
    y += 20;
    TreeParams& t = gTreeP;
    ch |= slider({x, y, w, 24}, "Height", t.height, 0.5f, 1.6f); y += 30;
    ch |= slider({x, y, w, 24}, "Branch Spread", t.spread, 10, 75, "%.0f"); y += 30;
    ch |= slider({x, y, w, 24}, "Trunk Thickness", t.trunkRadius, 0.02f, 0.12f, "%.3f"); y += 30;
    ch |= slider({x, y, w, 24}, "Leaf Size", t.leafSize, 0.02f, 0.2f, "%.3f"); y += 30;
    ch |= slider({x, y, w, 24}, "Leaf Density", t.leafDensity, 0, 3); y += 30;
    ch |= sliderI({x, y, w, 24}, "Branch Levels", t.levels, 1, 5); y += 30;
    ch |= sliderI({x, y, w, 24}, "Branches", t.branches, 2, 6); y += 30;
    ch |= slider({x, y, w, 24}, "Branch Lift", t.upwardBias, -.5f, .7f); y += 30;
    ch |= slider({x, y, w, 24}, "Branch Taper", t.taper, .25f, 1); y += 30;
    const char* foliage[] = {"Leaf Cards", "Broad Leaves", "Needles"};
    if (button({x, y, w, 24}, foliage[t.foliageShape])) { t.foliageShape = (t.foliageShape + 1) % 3; ch = true; }
    y += 30;
    if (button({x, y, w, 26}, "New Random Shape")) { t.seed = ++gSeed * 7 + 3; ch = true; }
    y += 34;
    if (button({x, y, 85, 24}, "Pine")) { t = {1.3f, 28, .065f, .06f, 1.7f, 3, 4, t.seed, .25f, .72f, 2}; ch = true; }
    if (button({x + 88, y, 84, 24}, "Oak")) { t = {1.1f, 56, .085f, .11f, 1.35f, 3, 4, t.seed, .12f, .66f, 1}; ch = true; }
    if (button({x + 175, y, 85, 24}, "Willow")) { t = {1.2f, 53, .058f, .08f, 1.2f, 3, 5, t.seed, -.25f, .59f, 2}; ch = true; }
    if (ch) {
        for (int i : members) { gScene.objs[i].tree = t; regenTree(gScene.objs[i]); }
        sceneChanged();
    }
    int x2 = P.x + 300;
    y = P.y + 46;
    std::vector<std::string> trunks, leaves = {"Leaves", "Autumn Leaves", "Grass", "Alien Slime", "Snowy Peaks", "Neon Glow"};
    for (auto& m : materialPresets())
        if (m.category == "Woods" && m.name != "Leaves" && m.name != "Autumn Leaves") trunks.push_back(m.name);
    auto matPicker = [&](const char* title, const std::vector<std::string>& names, int part) {
        label(x2, y, title);
        y += 20;
        std::string cur;
        for (int i : members)
            if (gScene.objs[i].treePart == part) cur = gScene.objs[i].mat.name;
        y = grid2<std::string>(x2, y, names, [](const std::string& s) { return s.c_str(); }, [&](const std::string& s) { return s == cur; },
                               [&](const std::string& s) {
                                   for (int i : members)
                                       if (gScene.objs[i].treePart == part) gScene.objs[i].mat = findMaterial(s);
                                   sceneChanged();
                               }) + 10;
    };
    matPicker("Trunk Material", trunks, 1);
    matPicker("Foliage Material", leaves, 2);
    note({x2, y, 228, 80}, "The trunk and foliage are grouped meshes; select either to move the whole tree. Fine-tune each in the Materials Lab.");
    preview({P.x + P.w - 336, P.y + 46, 320, 320}, "Tree preview");
    labFooter(P);
}

void drawRenderLab() {
    RenderSettings& r = gScene.render;
    bool ch = false;
    R P = panel(820, 640, "Render Options");
    int x = P.x + 16, y = P.y + 46, w = 360;
    label(x, y, "Image Size");
    y += 20;
    struct Sz { const char* n; int w, h; } sizes[] = {{"Match View", 0, 0},   {"640 x 480", 640, 480},   {"800 x 600", 800, 600},
                                                     {"1024 x 768", 1024, 768}, {"1280 x 720", 1280, 720}, {"1920 x 1080", 1920, 1080},
                                                     {"2048 x 1024 (360)", 2048, 1024}, {"3840 x 2160", 3840, 2160}};
    for (int i = 0; i < 8; i++)
        if (button({x + (i % 2) * 180, y + (i / 2) * 26, 176, 22}, sizes[i].n, r.width == sizes[i].w && r.height == sizes[i].h, gSmall)) {
            r.width = sizes[i].w; r.height = sizes[i].h; ch = true;
        }
    y += 4 * 26 + 4;
    ch |= dragI({x, y, 176, 22}, "Width", r.width, 4, 0, 8192);
    ch |= dragI({x + 180, y, 176, 22}, "Height", r.height, 4, 0, 8192);
    y += 34;
    label(x, y, "Quality");
    y += 20;
    for (int i = 0; i < 3; i++)
        if (button({x + i * 120, y, 116, 24}, qualityName(i), r.quality == i)) { r.quality = i; ch = true; }
    y += 30;
    ch |= sliderI({x, y, w, 24}, "Premium Rays per Pixel", r.samples, 1, 128); y += 30;
    ch |= toggle({x, y, 176, 24}, "Soft Shadows", r.softShadows);
    ch |= toggle({x + 180, y, 176, 24}, "Blurry Reflections", r.blurReflections);
    y += 32;
    ch |= sliderI({x, y, w, 24}, "Ray Depth (0 = Auto)", r.bounces, 0, 16); y += 30;
    ch |= toggle({x, y, w, 24}, "Colored Transparent Shadows", r.transparentShadows); y += 30;
    ch |= toggle({x, y, 176, 24}, "Adaptive Sampling", r.adaptive);
    ch |= toggle({x + 180, y, 176, 24}, "Edge-aware Denoise", r.denoise); y += 28;
    ch |= sliderI({x, y, w, 24}, "Sampling Seed", r.seed, 1, 65535); y += 28;
    ch |= toggle({x, y, w, 24}, "Render Region", r.region); y += 28;
    if (r.region) {
        ch |= slider({x, y, 176, 24}, "Region Left", r.cropX, 0, 0.99f);
        ch |= slider({x + 180, y, 176, 24}, "Region Top", r.cropY, 0, 0.99f); y += 28;
        ch |= slider({x, y, 176, 24}, "Region Width", r.cropW, 0.01f, 1);
        ch |= slider({x + 180, y, 176, 24}, "Region Height", r.cropH, 0.01f, 1); y += 28;
    }
    if (button({x, y, w, 24}, "Start Render Queue...")) {
        closeLab(false);
        defer(actQueue);
        return;
    }
    int x2 = P.x + 420;
    y = P.y + 46;
    label(x2, y, "Camera");
    y += 20;
    ch |= toggle({x2, y, 184, 24}, "Depth of Field", r.dof);
    ch |= toggle({x2 + 188, y, 184, 24}, "360 Panorama", r.panorama);
    y += 30;
    ch |= slider({x2, y, 372, 24}, "Focus Distance", r.focusDist, 1, 500, "%.1f", true); y += 30;
    ch |= slider({x2, y, 372, 24}, "Aperture", r.aperture, 0, 4); y += 30;
    if (button({x2, y, 372, 24}, "Focus on Selection") && validSel()) {
        ViewBasis vb = cameraBasis(gScene.cam);
        r.focusDist = std::max(1.f, dot(gScene.objs[gSel].worldPos - vb.o, vb.f));
        ch = true;
    }
    y += 40;
    label(x2, y, "Post Processing");
    y += 20;
    ch |= slider({x2, y, 372, 24}, "Gamma", r.gamma, 0.4f, 2.5f); y += 30;
    ch |= slider({x2, y, 372, 24}, "Brightness", r.brightness, -0.5f, 0.5f); y += 30;
    ch |= slider({x2, y, 372, 24}, "Contrast", r.contrast, 0.3f, 2.f); y += 30;
    ch |= slider({x2, y, 372, 24}, "Saturation", r.saturation, 0, 2); y += 30;
    if (button({x2, y, 372, 24}, "Reset Post Processing")) { r.gamma = 1; r.brightness = 0; r.contrast = 1; r.saturation = 1; ch = true; }
    y += 38;
    ch |= toggle({x2, y, 372, 24}, "Export Alpha with PNG or TIFF", r.alpha); y += 28;
    ch |= toggle({x2, y, 184, 24}, "Write Depth PNG", r.depthOutput);
    ch |= toggle({x2 + 188, y, 184, 24}, "Write Object ID PNG", r.idOutput);
    if (ch) sceneChanged();
    labFooter(P);
}

void alignSel(int axis, int mode) {
    if (!validSel()) return;
    V3 rlo, rhi;
    worldBounds(gScene.objs[gSel], rlo, rhi);
    float ref = mode == 0 ? rlo[axis] : (mode == 1 ? (rlo[axis] + rhi[axis]) * 0.5f : rhi[axis]);
    for (int i : gSelSet) {
        if (i == gSel) continue;
        Object& o = gScene.objs[i];
        V3 lo, hi;
        worldBounds(o, lo, hi);
        float cur = mode == 0 ? lo[axis] : (mode == 1 ? (lo[axis] + hi[axis]) * 0.5f : hi[axis]), d = ref - cur;
        o.moveWorld({axis == 0 ? d : 0, axis == 1 ? d : 0, axis == 2 ? d : 0});
    }
    sceneChanged();
}

void distributeSel(int axis) {
    auto s = sortedSel();
    if (s.size() < 3) { gStatus = "Select at least three objects to distribute."; return; }
    auto key = [&](int i) { return gScene.objs[i].worldPos[axis]; };
    std::sort(s.begin(), s.end(), [&](int a, int b) { return key(a) < key(b); });
    float a = key(s.front()), b = key(s.back());
    for (size_t k = 0; k < s.size(); k++) {
        float v = a + (b - a) * k / (float)(s.size() - 1);
        Object& o = gScene.objs[s[k]];
        float d = v - o.worldPos[axis];
        o.moveWorld({axis == 0 ? d : 0, axis == 1 ? d : 0, axis == 2 ? d : 0});
    }
    sceneChanged();
}

void drawToolsLab() {
    R P = panel(780, 556, "Edit Tools  -  " + std::to_string(gSelSet.size()) + " object(s)");
    int x = P.x + 16, y = P.y + 46, w = 350;
    label(x, y, "Replicate");
    y += 20;
    slider({x, y, w, 24}, "Copies", gRepCount, 1, 30, "%.0f"); y += 30;
    row3(x, y, w, "Offset per copy", gRepOff, 0.05f);
    row3(x, y, w, "Rotation per copy", gRepRot, 0.5f);
    slider({x, y, w, 24}, "Size per copy", gRepScale, 0.5f, 1.5f); y += 30;
    if (button({x, y, w, 26}, "Replicate Selection", false, gBold)) replicate((int)gRepCount, gRepOff, gRepRot, gRepScale);
    y += 40;
    label(x, y, "Randomize");
    y += 20;
    slider({x, y, w, 24}, "Position", gRandPos, 0, 30, "%.1f"); y += 30;
    slider({x, y, w, 24}, "Rotation", gRandRot, 0, 180, "%.0f"); y += 30;
    slider({x, y, w, 24}, "Size", gRandSize, 0, 0.9f); y += 30;
    if (button({x, y, w, 26}, "Randomize Selection", false, gBold)) {
        Rng rng(++gSeed * 2654435761u);
        for (int i : gSelSet) {
            Object& o = gScene.objs[i];
            if (o.locked) continue;
            o.pos += V3(rng.f() - 0.5f, 0, rng.f() - 0.5f) * (2 * gRandPos);
            o.rot.y += (rng.f() - 0.5f) * 2 * gRandRot;
            if (!isLight(o.type)) o.scl = o.scl * (1 + (rng.f() - 0.5f) * 2 * gRandSize);
        }
        sceneChanged();
    }
    int x2 = P.x + 400;
    y = P.y + 46;
    label(x2, y, "Align to the primary selection");
    y += 20;
    const char* ax[3] = {"X", "Y", "Z"}, *md[3] = {"Min", "Center", "Max"};
    for (int a = 0; a < 3; a++) {
        textIn({x2, y, 24, 24}, ax[a], TXT, gBold);
        for (int m = 0; m < 3; m++)
            if (button({x2 + 28 + m * 112, y, 108, 24}, md[m])) alignSel(a, m);
        y += 28;
    }
    y += 8;
    label(x2, y, "Distribute evenly");
    y += 20;
    for (int a = 0; a < 3; a++)
        if (button({x2 + a * 120, y, 116, 24}, (std::string("Along ") + ax[a]).c_str())) distributeSel(a);
    y += 36;
    label(x2, y, "Other");
    y += 20;
    if (button({x2, y, 176, 24}, "Land All")) actLand();
    if (button({x2 + 180, y, 176, 24}, "Reset Rotation")) { for (int i : gSelSet) gScene.objs[i].rot = V3(0.f); sceneChanged(); }
    y += 28;
    if (button({x2, y, 176, 24}, "Group")) actGroup();
    if (button({x2 + 180, y, 176, 24}, "Ungroup")) actUngroup();
    y += 28;
    if (button({x2, y, 176, 24}, "Parent to Active")) actParent(false);
    if (button({x2 + 180, y, 176, 24}, "Detach Parent")) actParent(true);
    y += 30;
    if (button({x2, y, 176, 24}, "Nest Boolean")) actNestBoolean(false);
    if (button({x2 + 180, y, 176, 24}, "Detach Boolean")) actNestBoolean(true);
    y += 28;
    if (button({x2, y, 176, 24}, (std::string("Nested Role ") + boolName(gNestedMode)).c_str()))
        gNestedMode = (BoolMode)(1 + (int)gNestedMode % 3);
    if (button({x2 + 180, y, 176, 24}, "Bake Mesh Copy")) actBakeGeometry();
    y += 28;
    if (toggle({x2, y, 356, 24}, "Use Cutter Materials on Cut Faces", gScene.cutterMaterial)) sceneChanged();
    y += 29;
    if (button({x2, y, 112, 24}, "Select Type")) actSelectMatching(0);
    if (button({x2 + 118, y, 116, 24}, "Select Material")) actSelectMatching(1);
    if (button({x2 + 240, y, 116, 24}, "Select Group")) actSelectMatching(2);
    y += 28;
    if (button({x2, y, 112, 24}, "Isolate")) actIsolate(false);
    if (button({x2 + 118, y, 116, 24}, "Show All")) actIsolate(true);
    if (button({x2 + 240, y, 116, 24}, "Linked Copy")) actLinked();
    y += 29;
    size_t tris = 0, bytes = 0;
    std::set<const void*> seen;
    for (const auto& o : gScene.objs) {
        if (o.mesh) {
            tris += o.mesh->triCount();
            if (seen.insert(o.mesh.get()).second) bytes += (o.mesh->v.size() + o.mesh->n.size()) * sizeof(V3) + o.mesh->tri.size() * sizeof(uint32_t);
        }
        if (o.hf) {
            size_t cells = (size_t)std::max(0, o.hf->res - 1);
            tris += cells * cells * (o.type == ObjType::Lattice ? 4 : 2);
            if (seen.insert(o.hf.get()).second) bytes += o.hf->h.size() * sizeof(float) + o.hf->n.size() * sizeof(V3);
        }
    }
    auto stats = std::to_string(tris) + " triangles  /  " + std::to_string(bytes / 1024) + " KiB of geometry data";
    label(x2, y, stats.c_str(), 356);
    labFooter(P);
}

void drawAssetsLab() {
    R p = panel(700, 390, "Scene and Object Library");
    int x = p.x + 16, y = p.y + 46;
    bool selection = validSel();
    if (button({x, y, 152, 24}, selection ? "Save Selected Objects" : "Save Current Scene")) {
        Scene asset = selection ? gScene.subset(gSelSet) : gScene.snapshot();
        storeAsset(asset, selection ? gScene.objs[gSel].name : (gPath.empty() ? "Scene" : baseName(gPath)));
    }
    if (button({x + 158, y, 130, 24}, "Import Asset...")) defer([] {
        std::string path;
        Scene asset;
        if (!fileDialog(false, SCENE_FILTER, "c3d", path)) return;
        if (asset.load(path)) storeAsset(asset, baseName(path));
        else gStatus = "Could not read the asset.";
    });
    if (gAssetSelection >= 0 && gAssetSelection < (int)gAssets.size()) {
        if (button({x + 296, y, 116, 24}, "Insert Asset")) {
            std::string path = gAssets[gAssetSelection].path;
            int first = (int)gScene.objs.size();
            if (gScene.merge(path) && (int)gScene.objs.size() > first) {
                gSelSet.clear();
                for (int i = first; i < (int)gScene.objs.size(); ++i) gSelSet.push_back(i);
                gSel = first;
                sceneChanged();
                closeLab(false);
                return;
            }
            gStatus = "Could not insert the asset.";
        }
        if (button({x + 418, y, 142, 24}, "Export Asset...")) {
            std::string asset = gAssets[gAssetSelection].path;
            defer([asset] {
                std::string path;
                Scene contents;
                if (fileDialog(true, SCENE_FILTER, "c3d", path))
                    gStatus = contents.load(asset) && contents.save(path) ? "Exported the asset." : "Could not export the asset.";
            });
        }
    }
    y += 38;
    const int pageSize = 10;
    for (int i = 0; i < pageSize && i + gAssetPage * pageSize < (int)gAssets.size(); ++i) {
        int n = gAssetPage * pageSize + i;
        const auto& item = gAssets[n];
        if (iconButton({x + (i % 5) * 132, y + (i / 5) * 94, 126, 88}, item.thumbnail, 96, 64, item.name.c_str(),
                       n == gAssetSelection, "3D")) gAssetSelection = n;
    }
    if (gAssets.empty()) note({x, y, 600, 38}, "Save a selection, a scene, or import a scene file to fill the library.");
    y += 194;
    if (button({x, y, 86, 22}, "Previous") && gAssetPage > 0) --gAssetPage;
    if (button({x + 92, y, 86, 22}, "Next") && (gAssetPage + 1) * pageSize < (int)gAssets.size()) ++gAssetPage;
    labFooter(p, true);
}

void drawExamplesLab() {
    R P = panel(700, 150 + 28 * (int)exampleNames().size(), "Example Scenes");
    int y = P.y + 50;
    for (size_t i = 0; i < exampleNames().size(); i++) {
        if (button({P.x + 20, y, 180, 24}, exampleNames()[i].c_str(), false, gBold)) {
            int n = (int)i;
            closeLab(true);
            defer([n] { loadExample(n); });
        }
        textIn({P.x + 214, y, P.w - 234, 24}, exampleDescriptions()[i].c_str(), TXT, gFont, TextLeft | TextMiddle);
        y += 28;
    }
    note({P.x + 20, y + 6, P.w - 40, 40}, "Loading an example replaces the current scene.");
    labFooter(P, true);
}

// ---------------------------------------------------------------- frame

void drawUI() {
    Modifiers input = modifiers();
    gIn.shift = input.shift;
    gIn.ctrl = input.ctrl;
    gIn.alt = input.alt;
    gVP = {LEFT, TOP, std::max(1, gW - LEFT - RIGHT), std::max(1, gH - TOP - STATUS - TIMELINE)};
    gBlocked = gLab != LabNone || gModal;
    drawTop();
    drawLeft();
    drawRight();
    drawViewport();
    drawTimeline();
    drawStatus();
    if (gLab != LabNone) {
        dimAll();
        gBlocked = gModal;
        switch (gLab) {
        case LabMat: drawMatLab(); break;
        case LabSky: drawSkyLab(); break;
        case LabTerrain: drawTerrainLab(); break;
        case LabTree: drawTreeLab(); break;
        case LabRender: drawRenderLab(); break;
        case LabTools: drawToolsLab(); break;
        case LabExamples: drawExamplesLab(); break;
        case LabObject: drawObjectLab(); break;
        case LabAttributes: drawAttributesLab(); break;
        case LabDxf: drawDxfLab(); break;
        case LabMotion: drawMotionLab(); break;
        case LabAssets: drawAssetsLab(); break;
        case LabCameras: drawCamerasLab(); break;
        case LabCommands: drawCommandsLab(); break;
        case LabScatter: drawScatterLab(); break;
        case LabTransform: drawTransformLab(); break;
        case LabSimulation: drawSimulationLab(); break;
        case LabMesh: drawMeshLab(); break;
        case LabCurves: drawCurveLab(); break;
        case LabRig: drawRigLab(); break;
        default: break;
        }
    }
}

void doFrame() {
    if (!gBits) return;
    for (int guard = 0; guard < 3; guard++) {
        gIdc = 0;
        gHoverTip.clear();
        gResizeHovered = false;
        drawUI();
        if (gIn.released) gActive = 0;
        gIn.pressed = gIn.released = false;
        gIn.dx = gIn.dy = 0;
        if (gDeferred.empty()) break;
        auto d = std::move(gDeferred);
        gDeferred.clear();
        for (auto& f : d) f();
    }
    showResizeCursor(gResizeHovered);
    present();
}

void tick() {
    uint64_t now = nowMs();
    if (gSimulationBake.valid() && gSimulationBake.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try {
            auto result = gSimulationBake.get();
            if (result.ok) {
                gScene = std::move(result.scene);
                if (result.particles > 0) selectObj(result.firstParticle, false);
                gScatter = gShowRender = false;
                gStatus = (result.particles > 0 ? "Baked liquid" : "Baked rigid bodies") +
                          std::string(" for frames ") + std::to_string(gScene.frame) + " to " + std::to_string(result.last) + ".";
                sceneChanged();
                closeLab(false);
                if (result.play) startPlayback(result.last);
            } else {
                gSimulationError = std::move(result.error);
            }
        } catch (const std::exception&) {
            gSimulationError = "The simulation bake could not finish.";
        }
        doFrame();
    }
    if (gDirty && !gModal && !gAnimRender && gQueue.empty() && gLab == LabNone && !gIn.down &&
        now - gAutosaveTime >= 60000 && now - gLastChange >= 1500) {
        gAutosaveTime = now;
        gScene.save(recoveryPath());
    }
    if (gDrag == DragSculpt && gIn.down && gActive == VIEW_ID && !gModal && now - gSculptLast >= 33) {
        gSculptLast = now;
        sculptStep();
        doFrame();
    }
    if (gPlaying) {
        uint64_t elapsed = std::max(now, gPlayStart) - gPlayStart;
        uint64_t frame = (uint64_t)gPlayStartFrame + elapsed * (uint64_t)std::max(1, gScene.fps) / 1000;
        if (gPlayEndFrame >= 0 && frame >= (uint64_t)gPlayEndFrame) {
            gPlaying = false;
            frame = (uint64_t)gPlayEndFrame;
        }
        int next = (int)(frame % (uint64_t)std::max(1, gScene.frames));
        if (next != gScene.frame) {
            gScene.applyFrame(next);
            gShowRender = false;
            gNanoDirty = gViewPreviewDirty = true;
            gLastChange = now - 1000;
            doFrame();
        }
    }
    if (gNanoDirty && (!gPlaying || !gNanoR.busy()) && now - gLastChange > 120) {
        gNanoDirty = false;
        Scene preview = gScene;
        preview.render = {};
        gNanoR.start(preview, NANO_W, NANO_H, 0);
    }
    static int previewView = -1, previewWidth = 0, previewHeight = 0;
    if (!gShowRender && (!gPlaying || !gViewR.busy()) &&
        (gViewMode == Solid || gViewMode == Textured) && now - gLastChange > 100 &&
        (gViewPreviewDirty || previewView != gView || previewWidth != gVP.w || previewHeight != gVP.h)) {
        gViewPreviewDirty = false;
        previewView = gView;
        previewWidth = gVP.w; previewHeight = gVP.h;
        Scene preview = gScene;
        preview.sky.shadows = false;
        preview.render = {};
        for (auto& o : preview.objs) {
            if (gViewMode == Solid) o.mat = Material{};
            o.mat.reflect = o.mat.transparency = o.mat.volumeDensity = 0;
            for (auto& layer : o.mat.layers) layer.influence[TexReflection] = layer.influence[TexTransparency] = 0;
            o.lighting.visible = false;
        }
        ViewBasis basis = curBasis();
        gViewR.start(preview, std::max(1, (int)(gVP.w * gPreviewScale)), std::max(1, (int)(gVP.h * gPreviewScale)), 0, &basis);
    }
    if (gLab != LabNone && gLabDirty && now - gLastChange > 120) { gLabDirty = false; startLabPreview(); }
    if (!gQueue.empty() && !gMainR.busy()) {
        if (gQueueIndex >= 0 && (!gMainR.finished() || !saveRenderer(gQueue[gQueueIndex].output))) {
            gStatus = "Queue could not write " + gQueue[gQueueIndex].output;
            gQueue.clear();
        } else if (++gQueueIndex >= (int)gQueue.size()) {
            gStatus = "Render queue complete.";
            gQueue.clear();
        } else {
            Scene scene;
            if (!scene.load(gQueue[gQueueIndex].scene)) {
                gStatus = "Queue could not read " + gQueue[gQueueIndex].scene;
                gQueue.clear();
            } else {
                gRenderStart = nowMs();
                gMainR.start(scene, scene.render.width > 0 ? scene.render.width : gVP.w,
                             scene.render.height > 0 ? scene.render.height : gVP.h);
                gStatus = "Queue item " + std::to_string(gQueueIndex + 1) + " of " + std::to_string(gQueue.size()) + ".";
            }
        }
        doFrame();
    }
    if (gAnimRender && !gMainR.busy()) {
        if (gAnimFrame >= 0) {
            if (!gMainR.finished()) gAnimRender = false;
            else {
                auto path = animationName(gAnimFrame);
                if (!saveRenderer(path)) { gAnimRender = false; gStatus = "Could not write " + path; }
            }
        }
        if (gAnimRender && ++gAnimFrame < gScene.frames) {
            gScene.applyFrame(gAnimFrame);
            gNanoDirty = true;
            int w, h;
            renderSize(w, h);
            gMainR.start(gScene, w, h);
        } else {
            char b[200];
            if (gAnimRender) snprintf(b, sizeof b, "Rendered %d animation frames in %.1f s.", gScene.frames, (now - gRenderStart) / 1000.f);
            else snprintf(b, sizeof b, "Animation render stopped at frame %d.", std::max(gAnimFrame, 0));
            if (gStatus.rfind("Could not", 0) != 0) gStatus = b;
            gAnimRender = false;
        }
        doFrame();
    }
    bool mb = gMainR.busy();
    if (gMainWasBusy && !mb && !gAnimRender && gQueue.empty() && gMainR.finished() && gStatus.rfind("Queue", 0) != 0 && gStatus != "Render queue complete.") {
        char b[96];
        snprintf(b, sizeof b, "Render complete in %.1f s. Click the image to return to the wireframe.", (now - gRenderStart) / 1000.f);
        gStatus = b;
    }
    gMainWasBusy = mb;
    static bool previewWasBusy = false;
    bool previewActive = gViewR.busy() || previewWasBusy;
    previewWasBusy = gViewR.busy();
    static uint64_t tipPainted = 0;
    bool tipReady = !gLastTip.empty() && now - gTipSince > 450 && tipPainted != gTipSince;
    if (tipReady) tipPainted = gTipSince;
    if (previewActive || tipReady) doFrame();
    static bool wasBusy = false;
    bool busy = mb || gNanoR.busy() || gLabR.busy();
    if ((busy || wasBusy) && !gModal) doFrame();
    wasBusy = busy;
}

// ---------------------------------------------------------------- icons

void glowIcon(std::vector<uint32_t>& img, V3 tint) {
    img.assign(ICW * ICH, 0);
    for (int y = 0; y < ICH; y++)
        for (int x = 0; x < ICW; x++) {
            float d = std::sqrt((float)((x - ICW / 2) * (x - ICW / 2) + (y - ICH / 2) * (y - ICH / 2)));
            V3 c = lerp(V3(0.12f, 0.12f, 0.16f), tint, clampf(1.4f - d / 11.f, 0, 1));
            img[y * ICW + x] = ((uint32_t)(c.x * 255) << 16) | ((uint32_t)(c.y * 255) << 8) | (uint32_t)(c.z * 255);
        }
}

void buildIcons() {
    for (ObjType type : kCreate) {
        auto& img = gIcons[(int)type];
        if (type == ObjType::Light) { glowIcon(img, V3(1, 0.92f, 0.5f)); continue; }
        if (type == ObjType::Spotlight) { glowIcon(img, V3(1, 0.65f, 0.3f)); continue; }
        if (type == ObjType::Parallel) { glowIcon(img, V3(0.7f, 0.85f, 1)); continue; }
        Scene s;
        s.sky = skyPresets()[0];
        s.sky.clouds = 0;
        s.sky.haze = 0;
        Object o = makeObject(ObjType::Sphere, 1);
        o.type = type;
        o.pos = V3(0.f);
        o.scl = V3(1.f);
        o.mat = findMaterial("Default Gray");
        s.cam.target = {0, 0, 0};
        s.cam.dist = 4.6f; s.cam.pitch = 22; s.cam.yaw = 35; s.cam.fov = 40;
        if (isHeightfield(type)) {
            o.hf = std::make_shared<Heightfield>();
            hfInit(*o.hf, 65);
            hfFractal(*o.hf, 7, 0.5f, type == ObjType::Lattice);
            o.mat = findMaterial(type == ObjType::Terrain ? "Snowy Peaks" : "Canyon Strata");
            o.pos = {0, type == ObjType::Terrain ? -0.4f : 0.f, 0};
            if (type == ObjType::Lattice) o.scl = {1, 0.6f, 1};
            s.cam.dist = 3.6f; s.cam.pitch = 30;
        } else if (type == ObjType::Stone) o.mat = findMaterial("Granite");
        else if (type == ObjType::Ground) { o.mat = findMaterial("Earth"); s.cam.dist = 8; s.cam.pitch = 14; s.cam.target = {0, 1, 0}; }
        else if (type == ObjType::Water) { o.mat = findMaterial("Water"); s.cam.dist = 8; s.cam.pitch = 14; s.cam.target = {0, 1, 0}; }
        else if (type == ObjType::Cloud) {
            o.mat = findMaterial("Clouds");
            o.mat.texScale = 0.25f;
            o.pos = {0, 4, 0};
            s.cam.target = {0, 4, -6}; s.cam.dist = 8; s.cam.pitch = -30;
        }
        s.objs.push_back(o);
        img.assign(ICW * ICH, 0);
        renderImage(s, ICW, ICH, img.data(), 1);
    }
    {
        Scene s;
        s.sky = skyPresets()[0];
        s.sky.clouds = 0;
        TreeParams p;
        p.seed = 5;
        addTree(s, p, V3(0.f), 1, findMaterial("Bark"), findMaterial("Leaves"), 1);
        s.cam.target = {0, 0.5f, 0}; s.cam.dist = 2.4f; s.cam.pitch = 8; s.cam.yaw = 30; s.cam.fov = 40;
        gTreeIcon.assign(ICW * ICH, 0);
        renderImage(s, ICW, ICH, gTreeIcon.data(), 1);
    }
    auto& sp = skyPresets();
    gSkyThumbs.resize(sp.size());
    for (size_t i = 0; i < sp.size(); i++) {
        Scene s;
        s.sky = sp[i];
        s.objs.push_back(makeObject(ObjType::Ground, 0));
        s.cam.target = {0, 3, 0}; s.cam.dist = 5; s.cam.pitch = -6; s.cam.yaw = sp[i].sunAz + 160; s.cam.fov = 70;
        if (sp[i].moon) s.cam.yaw = sp[i].moonAz + 180;
        gSkyThumbs[i].assign(STW * STH, 0);
        renderImage(s, STW, STH, gSkyThumbs[i].data(), 0);
    }
}

// ---------------------------------------------------------------- window

void handleKey(int wp) {
    Modifiers input = modifiers();
    bool ctrl = input.ctrl, shift = input.shift;
    bool axisKey = !ctrl && !input.alt && wp >= 'X' && wp <= 'Z';
    if (gDrag == DragObj && gIn.down) {
        if (wp == KeyCode::Escape || wp == KeyCode::Enter) finishObjectDrag(wp == KeyCode::Escape);
        else if (axisKey) {
            int axis = wp - 'X';
            gTransformAxis = gTransformAxis == axis ? -1 : axis;
            gIn.shift = shift;
            gIn.ctrl = ctrl;
            dragObject();
        }
        return;
    }
    if (wp == KeyCode::Escape) {
        if (gLab == LabSimulation && gSimulationBake.valid()) return;
        if (gLab != LabNone) { closeLab(true); endInteraction(); }
        else if (gScatter) { gScatter = false; gDrag = DragNone; gStatus = "Scatter brush closed."; }
        else if (gMainR.busy() || gAnimRender || !gQueue.empty()) actRender();
        else gShowRender = false;
        return;
    }
    if (gLab == LabTerrain && ctrl && (wp == 'Z' || wp == 'Y')) { terrainHistory(shift || wp == 'Y'); return; }
    if ((gLab == LabMat || gLab == LabSky) && ctrl && (wp == 'Z' || wp == 'Y')) { libraryHistory(shift || wp == 'Y'); return; }
    if (gLab != LabNone) return;
    if (wp == KeyCode::Help) { openLab(LabCommands); return; }
    beginInteraction();
    if (wp == KeyCode::Delete) actDelete();
    else if (ctrl && wp == 'Z') undoRedo(shift);
    else if (ctrl && wp == 'Y') undoRedo(true);
    else if (ctrl && wp == 'D') {
        if (shift) mirrorSelection();
        else actDuplicate();
    }
    else if (ctrl && wp == 'G') actGroup();
    else if (ctrl && wp == 'U') actUngroup();
    else if (ctrl && wp == 'A') { gSelSet.clear(); for (int i = 0; i < (int)gScene.objs.size(); i++) gSelSet.push_back(i); gSel = gSelSet.empty() ? -1 : 0; }
    else if (ctrl && wp == 'R') actRender();
    else if (ctrl && wp == 'S') actSave(false);
    else if (ctrl && wp == 'O') actOpen();
    else if (ctrl && wp == 'N') actNew();
    else if (ctrl && wp == 'L') actLand();
    else if (ctrl && wp == 'K') addKey();
    else if (!ctrl && wp == 'A' && validSel()) openLab(LabAttributes);
    else if (!ctrl && wp == 'M' && validSel()) openLab(LabMat);
    else if (!ctrl && wp == 'E' && validSel()) openLab(objectEditor(gScene.objs[gSel]));
    else if (!ctrl && wp == 'F') frameView(validSel());
    else if (!ctrl && !input.alt && (wp == 'G' || wp == 'R' || wp == 'S')) {
        gTool = wp == 'G' ? 0 : wp == 'R' ? 1 : 2;
        gScatter = false;
        gTab = 1;
        gShowRender = false;
    }
    else if (axisKey && gTool != TOOL_SCULPT) {
        int axis = wp - 'X';
        gTransformAxis = gTransformAxis == axis ? -1 : axis;
    }
    else if (wp == ' ') togglePlayback();
    else if (wp == KeyCode::LeftBracket || wp == KeyCode::RightBracket) gBrushSize = clampf(gBrushSize * (wp == KeyCode::RightBracket ? 1.2f : 1 / 1.2f), 0.01f, 0.3f);
    else if (wp >= '1' && wp <= '4') { gView = (int)(wp - '1'); gShowRender = false; }
    endInteraction();
}

bool parseInt(const char* arg, int& value) {
    const char* end = arg + std::strlen(arg);
    auto parsed = std::from_chars(arg, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

void usage(FILE* out) {
    std::fputs("Usage:\n"
               "  Brycescape --render OUTPUT [WIDTH HEIGHT [SCENE.c3d]]\n"
               "  Brycescape --example INDEX OUTPUT [WIDTH HEIGHT [QUALITY]]\n"
               "  Brycescape --export-example INDEX OUTPUT.c3d\n"
               "  Brycescape --render-frames SCENE.c3d OUTPUT_DIR FIRST LAST\n"
               "  Brycescape --batch QUEUE.txt\n", out);
}

int renderFrames(const char* path, const char* directory, int first, int last) {
    namespace fs = std::filesystem;
    Scene scene;
    if (!scene.load(path)) { std::fprintf(stderr, "Could not read scene: %s\n", path); return 3; }
    if (first < 0 || last < first || last >= scene.frames) {
        std::fprintf(stderr, "Frame range must be between 0 and %d.\n", scene.frames - 1);
        return 2;
    }
    int w = scene.render.width > 0 ? scene.render.width : 800, h = scene.render.height > 0 ? scene.render.height : 500;
    if ((size_t)w * h > 64000000) return 2;
    fs::path folder(directory);
    std::error_code error;
    fs::create_directories(folder, error);
    if (error) { std::fprintf(stderr, "Could not open output folder: %s\n", directory); return 4; }
    int rendered = 0, busy = 0, complete = 0;
    for (int frame = first; frame <= last; ++frame) {
        char name[32];
        std::snprintf(name, sizeof name, "frame_%06d", frame);
        fs::path output = folder / (std::string(name) + ".png");
        if (fs::is_regular_file(output, error)) { ++complete; continue; }
        fs::path claim = folder / (std::string(name) + ".rendering");
        if (!fs::create_directory(claim, error)) {
            if (error) { std::fprintf(stderr, "Could not claim frame %d.\n", frame); return 4; }
            ++busy;
            continue;
        }
        // Recheck after claiming in case a worker published between the earlier check and directory creation.
        bool alreadyDone = fs::is_regular_file(output, error);
        bool ok = alreadyDone;
        const char* suffixes[] = {"_depth.png", "_id.png", ".png"};
        if (!alreadyDone) {
            Scene frameScene = scene;
            frameScene.applyFrame(frame);
            std::vector<uint32_t> pixels((size_t)w * h);
            std::printf("Rendering frame %d\n", frame);
            renderImage(frameScene, w, h, pixels.data());
            ok = saveRenderOutput((claim / output.filename()).string(), frameScene, w, h, std::move(pixels));
            for (size_t i = 0; ok && i < std::size(suffixes); ++i) {
                if ((i == 0 && !scene.render.depthOutput) || (i == 1 && !scene.render.idOutput)) continue;
                std::string file = std::string(name) + suffixes[i];
                fs::rename(claim / file, folder / file, error);
                ok = !error;
            }
        }
        for (const char* suffix : suffixes) fs::remove(claim / (std::string(name) + suffix), error);
        fs::remove(claim, error);
        if (!ok) { std::fprintf(stderr, "Could not publish frame %d.\n", frame); return 4; }
        if (alreadyDone) ++complete;
        else ++rendered;
    }
    std::printf("Frames rendered: %d, already complete: %d, claimed by other workers: %d\n", rendered, complete, busy);
    return 0;
}

int headless(int argc, char** argv) {
    std::string_view command = argv[1];
    if (command == "--compose" || command == "--inspect" || command == "--catalog") return agentCommand(argc, argv);
    if (command == "--help") { usage(stdout); return 0; }
    if (command == "--render-frames") {
        int first, last;
        if (argc != 6 || !parseInt(argv[4], first) || !parseInt(argv[5], last)) { usage(stderr); return 2; }
        return renderFrames(argv[2], argv[3], first, last);
    }
    if (command == "--batch") {
        std::vector<RenderJob> jobs;
        if (argc != 3 || !readQueue(argv[2], jobs)) { usage(stderr); return 2; }
        for (const auto& job : jobs) {
            Scene scene;
            if (!scene.load(job.scene)) { std::fprintf(stderr, "Could not read scene: %s\n", job.scene.c_str()); return 3; }
            int w = scene.render.width > 0 ? scene.render.width : 800, h = scene.render.height > 0 ? scene.render.height : 500;
            if ((size_t)w * h > 64000000) return 2;
            std::vector<uint32_t> pixels((size_t)w * h);
            renderImage(scene, w, h, pixels.data());
            if (!saveRenderOutput(job.output, scene, w, h, std::move(pixels))) return 4;
        }
        return 0;
    }
    Scene scene;
    int w = 800, h = 500, quality = -1, example = 0;
    const char* output;
    if (command == "--export-example" || command == "--example") {
        if (argc < 4 || argc > 7 || !parseInt(argv[2], example) ||
            example < 0 || example >= (int)exampleNames().size()) { usage(stderr); return 2; }
        output = argv[3];
        if (command == "--export-example")
            return argc == 4 ? (makeExample(example).save(output) ? 0 : 4) : 2;
        if ((argc >= 5 && !parseInt(argv[4], w)) || (argc >= 6 && !parseInt(argv[5], h)) ||
            (argc == 7 && (!parseInt(argv[6], quality) || quality < -1 || quality > 2))) { usage(stderr); return 2; }
        scene = makeExample(example);
    } else if (command == "--render") {
        if (argc < 3 || argc > 6 || (argc >= 4 && !parseInt(argv[3], w)) ||
            (argc >= 5 && !parseInt(argv[4], h))) { usage(stderr); return 2; }
        output = argv[2];
        if (argc == 6) {
            if (!scene.load(argv[5])) { std::fprintf(stderr, "Could not read scene: %s\n", argv[5]); return 3; }
        } else {
            scene = makeDefaultScene();
        }
    } else {
        usage(stderr);
        return 2;
    }
    if (w < 1 || h < 1 || w > 16384 || h > 16384 || (size_t)w * h > 64000000) { usage(stderr); return 2; }
    std::vector<uint32_t> px((size_t)w * h);
    renderImage(scene, w, h, px.data(), quality);
    if (saveRenderOutput(output, scene, w, h, std::move(px))) return 0;
    std::fprintf(stderr, "Could not write image: %s\n", output);
    return 4;
}

}  // namespace

namespace studio {

int entry(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]).rfind("--", 0) == 0) return headless(argc, argv);
    int status = desktop::run(APP_NAME);
    gMainR.cancel();
    gNanoR.cancel();
    gLabR.cancel();
    gViewR.cancel();
    return status;
}

void initialize() {
    gLibPath = libraryPath();
    gLib.load(gLibPath);
    gScene = makeDefaultScene();
    sceneReplaced();
    gAutosaveTime = nowMs();
    std::error_code recoveryError;
    if (std::filesystem::is_regular_file(recoveryPath(), recoveryError)) gStatus = "Autosave found. Open Commands to recover it.";
    buildIcons();
}

void resized(int width, int height) {
    if (width <= 0 || height <= 0) return;
    uint32_t* canvas = resizeCanvas(width, height);
    if (!canvas) return;
    gW = width; gH = height; gBits = canvas;
    doFrame();
}

void timer() { tick(); }

void pointerMoved(int x, int y) {
    if (gModal) return;
    gIn.dx = x - gIn.mx; gIn.dy = y - gIn.my; gIn.mx = x; gIn.my = y;
    if (gRDrag) {
        if (gView == 0) orbitCam((float)gIn.dx, (float)gIn.dy);
        else panView((float)gIn.dx, (float)gIn.dy);
    }
    if (gMDrag) panView((float)gIn.dx, (float)gIn.dy);
    doFrame();
}

void pointerButton(Mouse button, bool down, int x, int y) {
    if (gModal) return;
    gIn.mx = x; gIn.my = y;
    if (button != Mouse::Left) {
        bool& dragging = button == Mouse::Right ? gRDrag : gMDrag;
        if (down && (gLab != LabNone || gIn.down || !gVP.in(x, y))) return;
        if (down && button == Mouse::Right && modifiers().shift) {
            Ray ray = viewportRay(curBasis(), x, y);
            Hit hit;
            if (intersectScene(gScene, ray, 1e30f, hit)) gCursor = hit.p;
            else if (std::fabs(ray.d.y) > 1e-6f && -ray.o.y / ray.d.y >= 0)
                gCursor = ray.o + ray.d * (-ray.o.y / ray.d.y);
            else {
                gStatus = "No surface under the cursor.";
                doFrame();
                return;
            }
            gCursorVisible = true;
            gShowRender = false;
            gStatus = "Placed the 3D cursor.";
            doFrame();
            return;
        }
        dragging = down;
        capturePointer(gIn.down || gRDrag || gMDrag);
        if (down) { gShowRender = false; doFrame(); }
        return;
    }
    if (down) {
        capturePointer(true);
        commitRename();
        beginInteraction();
        gIn.down = gIn.pressed = true;
    } else {
        capturePointer(gRDrag || gMDrag);
        gIn.down = false;
        gIn.released = true;
    }
    doFrame();
    if (!down) {
        if (gDrag == DragObj) finishObjectDrag(false);
        else endInteraction();
    }
}

void pointerWheel(float steps, int x, int y) {
    if (gModal || gLab != LabNone || (gIn.down && gDrag == DragObj)) return;
    if (gListR.in(x, y)) {
        static float remainder = 0;
        remainder += steps * 2;
        int rows = (int)remainder;
        remainder -= rows;
        gListScroll -= rows;
    } else if (gVP.in(x, y)) {
        gShowRender = false;
        zoomView(steps);
    }
    doFrame();
}

void keyDown(int key) {
    if (gModal) return;
    if (gEditObj != -1) {
        if (key == KeyCode::Backspace && !gEditBuf.empty()) {
            size_t end = gEditBuf.size() - 1;
#ifndef _WIN32
            while (end > 0 && (gEditBuf[end] & 0xc0) == 0x80) --end;
#endif
            gEditBuf.resize(end);
        } else if (key == KeyCode::Enter) {
            commitRename();
        } else if (key == KeyCode::Escape) {
            gEditObj = -1;
        } else {
            return;
        }
    } else {
        handleKey(key);
    }
    doFrame();
}

void textInput(std::string_view text) {
    if (gModal || gEditObj == -1 || gEditBuf.size() + text.size() > 60) return;
    gEditBuf += text;
    doFrame();
}

bool shouldClose() {
    if (gModal || !confirmDiscard()) return false;
    if (gDirty) clearRecovery();
    return true;
}

}  // namespace studio
