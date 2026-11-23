#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "render.h"
#include "scene.h"
#include "terrain.h"

namespace {

struct R {
    int x, y, w, h;
    bool in(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

const int TOP = 98, LEFT = 170, RIGHT = 236, STATUS = 24, TIMELINE = 34;
const int NANO_W = 150, NANO_H = 112, ICW = 48, ICH = 32, STW = 64, STH = 40;
const int VIEW_ID = 1000000;
const ObjType kCreate[] = {ObjType::Sphere, ObjType::Cube,    ObjType::Cylinder, ObjType::Cone,  ObjType::Pyramid, ObjType::Torus,
                           ObjType::Disc,   ObjType::Square,  ObjType::Stone,    ObjType::Terrain, ObjType::Lattice, ObjType::Ground,
                           ObjType::Water,  ObjType::Cloud,   ObjType::Light,    ObjType::Spotlight, ObjType::Parallel};

HWND gWnd;
int gW = 1440, gH = 900;
HDC gDC;
HBITMAP gBmp;
uint32_t* gBits;
HFONT gFont, gSmall, gBold, gTitle;

struct Input {
    int mx = 0, my = 0, dx = 0, dy = 0;
    bool down = false, pressed = false, released = false, shift = false, ctrl = false, alt = false;
} gIn;
int gActive = 0, gIdc = 0;
bool gBlocked = false, gModal = false;
std::vector<std::function<void()>> gDeferred;

Scene gScene;
std::vector<int> gSelSet;
int gSel = -1, gTab = 0, gView = 0, gTool = 0;
bool gShowRender = false;
Renderer gMainR, gNanoR, gLabR;
enum Lab { LabNone, LabMat, LabSky, LabTerrain, LabTree, LabRender, LabTools, LabExamples } gLab = LabNone;
Scene gLabBackup;
bool gLabChanged = false, gNanoDirty = true, gLabDirty = false, gMainWasBusy = false;
DWORD gLastChange = 0, gRenderStart = 0;
std::string gStatus = "Welcome to Canyon 3D. Create objects from the Create palette, or open Examples, then press Render.", gPath;
V3 gOrthoC[4];
float gOrthoS[4] = {40, 40, 40, 40};
int gCounter[(int)ObjType::COUNT] = {}, gTreeCounter = 0;
R gVP{0, 0, 1, 1}, gListR{0, 0, 0, 0};
int gListScroll = 0;
uint32_t gSeed = 100;
float gBrushSize = 0.06f, gBrushStr = 0.5f, gRough = 0.5f, gTerraces = 6;
BrushMode gBrush = BrushMode::Raise;
enum Drag { DragNone, DragObj, DragCam } gDrag = DragNone;
bool gRDrag = false, gMDrag = false;
std::vector<uint32_t> gIcons[(int)ObjType::COUNT], gTreeIcon;
std::vector<std::vector<uint32_t>> gSkyThumbs;
std::vector<Scene> gUndo, gRedo;
Scene gPre;
bool gHavePre = false, gChanged = false, gNoPush = false, gDirty = false;
int gEditObj = -1;
std::string gEditBuf;
int gQuickCat = 0, gSkyCat = 0, gMatCat = 0, gLabSkyCat = 0;
Library gLib;
std::string gLibPath;
TreeParams gTreeP;
bool gPlaying = false, gAutoKey = false, gAnimRender = false;
int gAnimFrame = -1;
DWORD gPlayLast = 0;
std::string gAnimBase;
float gRepCount = 3, gRepScale = 1, gRandPos = 4, gRandRot = 45, gRandSize = 0.3f;
V3 gRepOff{3, 0, 0}, gRepRot{0, 0, 0};

// ---------------------------------------------------------------- drawing primitives

COLORREF rgb(int r, int g, int b) { return RGB(r, g, b); }
const COLORREF TXT = RGB(232, 232, 236), TXT_DIM = RGB(190, 194, 204);

void resizeCanvas(int w, int h) {
    gW = std::max(w, 1); gH = std::max(h, 1);
    if (!gDC) { HDC s = GetDC(gWnd); gDC = CreateCompatibleDC(s); ReleaseDC(gWnd, s); }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = gW;
    bi.bmiHeader.biHeight = -gH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* p = nullptr;
    HBITMAP b = CreateDIBSection(gDC, &bi, DIB_RGB_COLORS, &p, nullptr, 0);
    SelectObject(gDC, b);
    if (gBmp) DeleteObject(gBmp);
    gBmp = b;
    gBits = (uint32_t*)p;
    SetBkMode(gDC, TRANSPARENT);
}

void fill(R r, COLORREF c) {
    SetDCBrushColor(gDC, c);
    RECT rc{r.x, r.y, r.x + r.w, r.y + r.h};
    FillRect(gDC, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void vgrad(R r, COLORREF a, COLORREF b) {
    GdiFlush();
    int x0 = std::max(r.x, 0), x1 = std::min(r.x + r.w, gW), y0 = std::max(r.y, 0), y1 = std::min(r.y + r.h, gH);
    for (int y = y0; y < y1; y++) {
        float t = (y - r.y) / (float)std::max(1, r.h - 1);
        uint32_t v = ((uint32_t)lerpf(GetRValue(a), GetRValue(b), t) << 16) | ((uint32_t)lerpf(GetGValue(a), GetGValue(b), t) << 8) |
                     (uint32_t)lerpf(GetBValue(a), GetBValue(b), t);
        std::fill(gBits + (size_t)y * gW + x0, gBits + (size_t)y * gW + std::max(x0, x1), v);
    }
}

void line(int x0, int y0, int x1, int y1, COLORREF c) {
    SelectObject(gDC, GetStockObject(DC_PEN));
    SetDCPenColor(gDC, c);
    MoveToEx(gDC, x0, y0, nullptr);
    LineTo(gDC, x1, y1);
}

void bevel(R r, bool sunken) {
    COLORREF hi = rgb(160, 160, 168), lo = rgb(34, 34, 38);
    if (sunken) std::swap(hi, lo);
    line(r.x, r.y + r.h - 1, r.x, r.y, hi);
    LineTo(gDC, r.x + r.w - 1, r.y);
    line(r.x + r.w - 1, r.y, r.x + r.w - 1, r.y + r.h - 1, lo);
    LineTo(gDC, r.x - 1, r.y + r.h - 1);
}

void textIn(R r, const char* s, COLORREF c, HFONT f, UINT fmt = DT_CENTER | DT_VCENTER | DT_SINGLELINE) {
    SelectObject(gDC, f);
    SetTextColor(gDC, c);
    RECT rc{r.x, r.y, r.x + r.w, r.y + r.h};
    DrawTextA(gDC, s, -1, &rc, fmt | DT_NOPREFIX | DT_END_ELLIPSIS);
}

void label(int x, int y, const char* s, int w = 400) { textIn({x, y, w, 16}, s, TXT_DIM, gBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE); }
void note(R r, const char* s) { textIn(r, s, TXT_DIM, gSmall, DT_LEFT | DT_WORDBREAK); }

void blit(R d, const uint32_t* src, int sw, int sh) {
    if (d.w <= 0 || d.h <= 0 || sw <= 0 || sh <= 0) return;
    GdiFlush();
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
    GdiFlush();
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
    GdiFlush();
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

bool button(R r, const char* text, bool on = false, HFONT f = nullptr) {
    int id = newId();
    press(r, id);
    bool h = hov(r), act = gActive == id && h;
    if (on) vgrad(r, rgb(126, 142, 182), rgb(70, 82, 120));
    else if (act) vgrad(r, rgb(58, 58, 64), rgb(92, 92, 100));
    else if (h) vgrad(r, rgb(140, 140, 148), rgb(96, 96, 104));
    else vgrad(r, rgb(114, 114, 120), rgb(78, 78, 84));
    bevel(r, act);
    textIn(r, text, on ? rgb(255, 255, 255) : TXT, f ? f : gFont);
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
    if (!img.empty()) blit({r.x + (r.w - iw) / 2, r.y + 4, iw, ih}, img.data(), iw, ih);
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
    textIn({r.x + 6, r.y, r.w - 12, r.h}, text, TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    textIn({r.x + 6, r.y, r.w - 12, r.h}, buf, TXT, gFont, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
    textIn({r.x + 4, r.y, r.w - 8, r.h}, text, TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    textIn({r.x + 4, r.y, r.w - 8, r.h}, buf, TXT, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (hov(r)) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
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
    gScene.updateAll();
    gNanoDirty = true;
    gLastChange = GetTickCount();
    gChanged = true;
    if (gLab != LabNone) { gLabDirty = true; gLabChanged = true; }
}

bool pickColor(V3& c) {
    static COLORREF cust[16];
    CHOOSECOLORA cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = gWnd;
    cc.rgbResult = RGB((int)(clampf(c.x, 0, 1) * 255), (int)(clampf(c.y, 0, 1) * 255), (int)(clampf(c.z, 0, 1) * 255));
    cc.lpCustColors = cust;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    gModal = true;
    BOOL ok = ChooseColorA(&cc);
    gModal = false;
    gIn.down = false;
    gActive = 0;
    if (ok) c = V3(GetRValue(cc.rgbResult) / 255.f, GetGValue(cc.rgbResult) / 255.f, GetBValue(cc.rgbResult) / 255.f);
    return ok != 0;
}

void swatch(R r, const char* text, V3& c) {
    int id = newId();
    press(r, id);
    bool h = hov(r);
    vgrad(r, h ? rgb(130, 130, 138) : rgb(108, 108, 114), rgb(74, 74, 80));
    bevel(r, gActive == id && h);
    textIn({r.x + 6, r.y, r.w - 60, r.h}, text, TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    R sw{r.x + r.w - 52, r.y + 3, 46, r.h - 6};
    fill(sw, RGB((int)(clampf(c.x, 0, 1) * 255), (int)(clampf(c.y, 0, 1) * 255), (int)(clampf(c.z, 0, 1) * 255)));
    bevel(sw, true);
    if (clicked(r, id)) {
        V3* pc = &c;
        defer([pc] { if (pickColor(*pc)) sceneChanged(); });
    }
}

int modalBox(const char* text, UINT flags) {
    gModal = true;
    int r = MessageBoxA(gWnd, text, "Canyon 3D", flags);
    gModal = false;
    gIn.down = false;
    gActive = 0;
    return r;
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

void pushUndo(Scene s) {
    gUndo.push_back(std::move(s));
    gDirty = true;
    if (gUndo.size() > 40) gUndo.erase(gUndo.begin());
    gRedo.clear();
}

void beginInteraction() {
    if (gHavePre || gLab != LabNone) return;
    gPre = gScene.snapshot();
    gHavePre = true;
    gChanged = false;
}

void autoKey();

void endInteraction() {
    if (gHavePre && gChanged && !gNoPush) {
        pushUndo(std::move(gPre));
        if (gAutoKey) autoKey();
    }
    gHavePre = gNoPush = false;
}

void sceneReplaced() {
    gMainR.cancel();
    clearSel();
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
    to.push_back(gScene.snapshot());
    gScene = std::move(from.back());
    from.pop_back();
    sceneReplaced();
    gNoPush = gDirty = true;
    gStatus = redo ? "Redo." : "Undo.";
}

void commitRename() {
    if (gEditObj >= 0 && gEditObj < (int)gScene.objs.size() && !gEditBuf.empty() && gScene.objs[gEditObj].name != gEditBuf) {
        if (gLab == LabNone && !gHavePre) pushUndo(gScene.snapshot());
        gScene.objs[gEditObj].name = gEditBuf;
    }
    gEditObj = -1;
}

// ---------------------------------------------------------------- views & scene actions

ViewBasis curBasis() { return gView == 0 ? cameraBasis(gScene.cam) : orthoBasis(gView, gOrthoC[gView], gOrthoS[gView]); }

void orbitCam(float dx, float dy) {
    gScene.cam.yaw -= dx * 0.4f;
    gScene.cam.pitch = clampf(gScene.cam.pitch + dy * 0.3f, -89.f, 89.f);
    sceneChanged();
}

void panView(float dx, float dy) {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f;
    if (gView == 0) gScene.cam.target += (vb.r * -dx + vb.u * dy) * (gScene.cam.dist * vb.k / hh);
    else gOrthoC[gView] += (vb.r * -dx + vb.u * dy) * (vb.k / hh);
    sceneChanged();
}

void zoomView(float steps) {
    float f = std::pow(0.88f, steps);
    if (gView == 0) gScene.cam.dist = clampf(gScene.cam.dist * f, 1.f, 5000.f);
    else gOrthoS[gView] = clampf(gOrthoS[gView] * f, 1.f, 5000.f);
    sceneChanged();
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
    for (auto it = s.rbegin(); it != s.rend(); ++it) gScene.objs.erase(gScene.objs.begin() + *it);
    clearSel();
    sceneChanged();
    gStatus = "Deleted " + std::to_string(s.size()) + " object(s).";
}

void replicate(int times, V3 off, V3 rot, float scl) {
    auto s = sortedSel();
    if (s.empty()) return;
    int ng = gScene.maxGroup();
    std::vector<int> made;
    for (int k = 1; k <= times; k++) {
        std::map<int, int> gmap;
        for (int i : s) {
            Object o = gScene.objs[i];
            if (o.hf) o.hf = std::make_shared<Heightfield>(*o.hf);
            o.name += " copy";
            o.pos += off * (float)k;
            o.rot += rot * (float)k;
            o.scl = o.scl * std::pow(scl, (float)k);
            o.keys.clear();
            if (o.group > 0) {
                if (!gmap.count(o.group)) gmap[o.group] = ++ng;
                o.group = gmap[o.group];
            }
            gScene.objs.push_back(o);
            made.push_back((int)gScene.objs.size() - 1);
        }
    }
    gSelSet = made;
    gSel = made.back();
    sceneChanged();
    gStatus = "Created " + std::to_string(made.size()) + " copies.";
}

void actDuplicate() { replicate(1, V3(2, 0, 2), V3(0.f), 1); }

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
        for (int i : u.second) gScene.objs[i].pos.y += ground - lo.y;
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
    char buf[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = gWnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = ext;
    ofn.Flags = save ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    gModal = true;
    BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    gModal = false;
    gIn.down = false;
    gActive = 0;
    if (ok) out = buf;
    return ok != 0;
}

const char* SCENE_FILTER = "Canyon 3D Scene (*.c3d)\0*.c3d\0All Files (*.*)\0*.*\0";
const char* BMP_FILTER = "Bitmap Image (*.bmp)\0*.bmp\0";

std::string baseName(const std::string& p) {
    size_t s = p.find_last_of("\\/");
    return s == std::string::npos ? p : p.substr(s + 1);
}

void actImport() {
    std::string p, err;
    if (!fileDialog(false, "Wavefront OBJ (*.obj)\0*.obj\0All Files (*.*)\0*.*\0", "obj", p)) return;
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

bool confirmDiscard() { return !gDirty || modalBox("Discard unsaved changes to the current scene?", MB_YESNO | MB_ICONQUESTION) == IDYES; }

void actNew() {
    if (!confirmDiscard()) return;
    gScene = makeEmptyScene();
    gPath.clear();
    sceneReplaced();
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
    gStatus = "Opened " + p;
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
    if (!gScene.save(p)) { gStatus = "Could not write " + p; return; }
    gPath = p;
    gDirty = false;
    gStatus = "Saved " + p;
}

void loadExample(int i) {
    if (!confirmDiscard()) return;
    gScene = makeExample(i);
    gPath.clear();
    sceneReplaced();
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

void actSaveImage() {
    if (!gMainR.hasImage()) { gStatus = "Render the scene first."; return; }
    std::string p;
    if (!fileDialog(true, BMP_FILTER, "bmp", p)) return;
    std::vector<uint32_t> px;
    gMainR.copyTo(px);
    gStatus = saveBMP(p, gMainR.width(), gMainR.height(), px) ? "Saved image " + p : "Could not write " + p;
}

void renderSize(int& w, int& h) {
    const RenderSettings& r = gScene.render;
    w = r.width > 0 && r.height > 0 ? r.width : gVP.w;
    h = r.width > 0 && r.height > 0 ? r.height : gVP.h;
}

const char* qualityName(int q) { return q == 0 ? "Draft" : (q == 1 ? "Normal" : "Premium"); }

void actRender() {
    if (gMainR.busy() || gAnimRender) {
        gMainWasBusy = gAnimRender = false;
        gMainR.cancel();
        gStatus = "Render stopped.";
        return;
    }
    gView = 0;
    gShowRender = true;
    gRenderStart = GetTickCount();
    int w, h;
    renderSize(w, h);
    gMainR.start(gScene, w, h);
    char b[96];
    snprintf(b, sizeof b, "Rendering %dx%d (%s)...", w, h, qualityName(gScene.render.quality));
    gStatus = b;
}

void actRenderAnim() {
    std::string p;
    if (!fileDialog(true, BMP_FILTER, "bmp", p)) return;
    size_t dot = p.find_last_of('.');
    gAnimBase = dot == std::string::npos ? p : p.substr(0, dot);
    gMainR.cancel();
    gAnimRender = true;
    gAnimFrame = -1;
    gPlaying = false;
    gView = 0;
    gShowRender = true;
    gRenderStart = GetTickCount();
}

void setFrame(int f) {
    gScene.applyFrame(std::min(std::max(f, 0), gScene.frames - 1));
    gShowRender = false;
    sceneChanged();
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
        for (int i : gSelSet)
            for (auto& k : gScene.objs[i].keys) r.push_back(k.frame);
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

// ---------------------------------------------------------------- labs: open / close / preview

std::vector<int> treeMembers() {
    std::vector<int> r;
    for (int i : sortedSel())
        if (gScene.objs[i].treePart) r.push_back(i);
    return r;
}

void openLab(Lab l) {
    if (l == LabMat || l == LabTerrain) {
        if (!validSel()) { gStatus = "Select an object first."; return; }
        Object& o = gScene.objs[gSel];
        if (l == LabMat && isLight(o.type)) { gStatus = "Lights have no material; edit color and power in the side panel."; return; }
        if (l == LabTerrain && (!isHeightfield(o.type) || !o.hf)) { gStatus = "Select a terrain or lattice to open the Terrain Editor."; return; }
    }
    if (l == LabTree) {
        auto t = treeMembers();
        if (t.empty()) { gStatus = "Select a tree (or create one with the Tree button) to open the Tree Lab."; return; }
        gTreeP = gScene.objs[t[0]].tree;
    }
    if (l == LabTools && gSelSet.empty()) { gStatus = "Select objects to use the Edit Tools."; return; }
    commitRename();
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
        if (gLabChanged) pushUndo(std::move(gLabBackup));
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
            s.objs.push_back(o);
        }
        s.cam.target = {0, 2.4f, 0};
        s.cam.dist = 10; s.cam.pitch = 8; s.cam.yaw = 30; s.cam.fov = 45;
        gLabR.start(s, 320, 320, 1);
    } else if (gLab == LabSky) gLabR.start(gScene, 340, 255, 0);
    else if (gLab == LabTerrain) gLabR.start(gScene, 320, 240, 0);
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
    textIn({310, 4, 200, 22}, "CANYON 3D", rgb(240, 240, 245), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const char* fb[6] = {"New", "Open...", "Merge...", "Save", "Save As...", "Examples..."};
    int fx = gW - 6 * 76 - 6;
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
        for (ObjType t : kCreate) {
            if (iconButton({x, 30, 56, 62}, gIcons[(int)t], ICW, ICH, objTypeName(t))) createObject(t);
            x += 60;
        }
        if (iconButton({x, 30, 56, 62}, gTreeIcon, ICW, ICH, "Tree")) createTree();
        x += 60;
        if (iconButton({x, 30, 56, 62}, {}, ICW, ICH, "Import", false, "OBJ")) defer(actImport);
    } else if (gTab == 1) {
        const char* tools[3] = {"Move", "Rotate", "Resize"};
        for (int i = 0; i < 3; i++)
            if (button({10 + i * 64, 32, 60, 24}, tools[i], gTool == i)) gTool = i;
        struct B { const char* n; int w; std::function<void()> fn; } bs[] = {
            {"Materials Lab", 100, [] { openLab(LabMat); }}, {"Terrain Editor", 100, [] { openLab(LabTerrain); }},
            {"Tree Lab", 72, [] { openLab(LabTree); }},       {"Edit Tools", 80, [] { openLab(LabTools); }},
            {"Land", 50, actLand},                            {"Duplicate", 74, actDuplicate},
            {"Delete", 60, actDelete},                        {"Group", 56, actGroup},
            {"Ungroup", 66, actUngroup},                      {"Undo", 50, [] { undoRedo(false); }},
            {"Redo", 50, [] { undoRedo(true); }},
        };
        int x = 206;
        for (auto& b : bs) {
            if (button({x, 32, b.w, 24}, b.n)) b.fn();
            x += b.w + 4;
        }
        auto cats = categories(false);
        gQuickCat = (gQuickCat + (int)cats.size()) % (int)cats.size();
        textIn({10, 64, 100, 24}, "Quick materials:", TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (button({112, 64, 22, 24}, "<")) gQuickCat--;
        textIn({136, 64, 92, 24}, cats[gQuickCat].c_str(), rgb(255, 255, 255), gBold);
        if (button({230, 64, 22, 24}, ">")) gQuickCat++;
        int qx = 260;
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
    bool busy = gMainR.busy() || gAnimRender;
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
    if (button({x, y, 148, 22}, gShowRender ? "Show Wireframe" : "Show Render") && gMainR.hasImage()) gShowRender = !gShowRender;
    y += 26;
    if (button({x, y, 148, 22}, "Save Image...")) defer(actSaveImage);
}

void row3(int x, int& y, int w, const char* title, V3& v, float speed, float minV = -1e9f) {
    int cw = (w - 8) / 3;
    label(x, y, title);
    y += 18;
    bool ch = dragF({x, y, cw, 20}, "X", v.x, speed, minV);
    ch |= dragF({x + cw + 4, y, cw, 20}, "Y", v.y, speed, minV);
    ch |= dragF({x + 2 * (cw + 4), y, cw, 20}, "Z", v.z, speed, minV);
    y += 26;
    if (ch) sceneChanged();
}

void drawRight() {
    int x0 = gW - RIGHT;
    vgrad({x0, TOP, RIGHT, gH - TOP - STATUS - TIMELINE}, rgb(90, 90, 96), rgb(56, 56, 62));
    int x = x0 + 10, w = RIGHT - 20, y = TOP + 8;
    label(x, y, "Scene Objects");
    y += 18;
    const int rows = 8, n = (int)gScene.objs.size();
    gListR = {x, y, w, rows * 18 + 4};
    fill(gListR, rgb(38, 38, 44));
    bevel(gListR, true);
    gListScroll = std::max(0, std::min(gListScroll, n - rows));
    for (int k = 0; k < rows && gListScroll + k < n; k++) {
        int idx = gListScroll + k;
        R row{x + 2, y + 2 + k * 18, w - 4, 18};
        const Object& o = gScene.objs[idx];
        if (isSel(idx)) fill(row, idx == gSel ? rgb(92, 108, 156) : rgb(72, 84, 120));
        else if (hov(row)) fill(row, rgb(60, 60, 70));
        std::string nm = (o.group ? "[" + std::to_string(o.group) + "] " : "") + o.name;
        textIn({row.x + 6, row.y, row.w - 74, row.h}, nm.c_str(), o.hidden ? TXT_DIM : TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        std::string tn = std::string(o.hidden ? "hidden " : "") + (o.boolMode != BoolMode::Neutral ? boolName(o.boolMode) : objTypeName(o.type));
        textIn({row.x, row.y, row.w - 6, row.h}, tn.c_str(), TXT_DIM, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        int id = newId();
        press(row, id);
        if (gActive == id && gIn.pressed) selectObj(idx, gIn.shift, gIn.alt || gIn.ctrl);
    }
    y += gListR.h + 8;
    if (validSel()) {
        Object& o = gScene.objs[gSel];
        if (gSelSet.size() > 1) {
            char b[64];
            snprintf(b, sizeof b, "%d objects selected", (int)gSelSet.size());
            textIn({x, y, w, 16}, b, TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 16;
        }
        int id = newId();
        R nr{x, y, w, 24};
        press(nr, id);
        bool editing = gEditObj == gSel;
        fill(nr, editing ? rgb(28, 28, 34) : (hov(nr) ? rgb(70, 70, 80) : rgb(56, 56, 64)));
        bevel(nr, true);
        std::string shown = editing ? gEditBuf + "|" : o.name;
        textIn({nr.x + 6, nr.y, nr.w - 12, nr.h}, shown.c_str(), rgb(255, 255, 255), gBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (gActive == id && gIn.pressed && !editing) { gEditObj = gSel; gEditBuf = o.name; }
        y += 28;
        row3(x, y, w, "Position", o.pos, 0.05f);
        if (o.type != ObjType::Light) row3(x, y, w, isLight(o.type) ? "Aim (rotation)" : "Rotation", o.rot, 0.5f);
        if (!isLight(o.type)) {
            row3(x, y, w, "Size", o.scl, 0.02f, 0.01f);
            label(x, y, "Material");
            textIn({x + 66, y, w - 66, 16}, o.mat.name.c_str(), TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += 20;
            int bw = (w - 4) / 2;
            if (button({x, y, isHeightfield(o.type) || o.treePart ? bw : w, 22}, "Materials Lab...")) openLab(LabMat);
            if (isHeightfield(o.type) && button({x + bw + 4, y, bw, 22}, "Terrain Editor...")) openLab(LabTerrain);
            if (o.treePart && button({x + bw + 4, y, bw, 22}, "Tree Lab...")) openLab(LabTree);
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
            swatch({x, y, w, 24}, "Light Color", o.lightColor);
            y += 28;
            bool ch = slider({x, y, w, 22}, "Power", o.lightPower, 0, 5);
            y += 26;
            if (o.type == ObjType::Spotlight) {
                ch |= slider({x, y, w, 22}, "Cone Angle", o.spotAngle, 2, 85, "%.0f"); y += 26;
                ch |= slider({x, y, w, 22}, "Softness", o.spotSoft, 0, 1); y += 26;
            }
            ch |= slider({x, y, w, 22}, "Soft Shadow Size", o.lightSize, 0, 3); y += 28;
            if (ch) sceneChanged();
        }
        int tw = (w - 8) / 3;
        std::string gl = o.group ? "Group " + std::to_string(o.group) : "No group";
        textIn({x, y, tw, 22}, gl.c_str(), TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        bool hid = o.hidden, lock = o.locked;
        if (toggle({x + tw + 4, y, tw, 22}, "Hidden", hid)) { for (int i : gSelSet) gScene.objs[i].hidden = hid; sceneChanged(); }
        if (toggle({x + 2 * (tw + 4), y, tw, 22}, "Locked", lock)) { for (int i : gSelSet) gScene.objs[i].locked = lock; gChanged = true; }
        y += 28;
        if (button({x, y, tw, 24}, "Land")) actLand();
        if (button({x + tw + 4, y, tw, 24}, "Duplicate")) actDuplicate();
        if (button({x + 2 * (tw + 4), y, tw, 24}, "Delete")) actDelete();
    } else {
        Camera& c = gScene.cam;
        textIn({x, y, w, 20}, "Camera", rgb(255, 255, 255), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
        if (!isLight(o.type) || o.hidden) continue;
        V3 c = vb.toCam(o.pos);
        if (!vb.ortho && c.z < 0.1f) continue;
        float d = vb.ortho ? 1.f : c.z;
        float sx = cx + c.x / (d * vb.k) * hh, sy = cy - c.y / (d * vb.k) * hh;
        if (std::fabs(sx - mx) < 10 && std::fabs(sy - my) < 10) return i;
    }
    Hit h;
    return intersectScene(gScene, vb.ray((mx - cx) / hh, -(my - cy) / hh), 1e30f, h) ? h.obj : -1;
}

void dragObject() {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f, dx = (float)gIn.dx, dy = (float)gIn.dy;
    std::vector<int> s;
    for (int i : gSelSet)
        if (!gScene.objs[i].locked) s.push_back(i);
    if (s.empty()) { gStatus = "The selection is locked."; return; }
    V3 c(0.f);
    for (int i : s) c += gScene.objs[i].pos;
    c = c / (float)s.size();
    if (gTool == 0) {
        float wpp = (vb.ortho ? 1.f : std::max(0.5f, vb.toCam(gScene.objs[gSel].pos).z)) * vb.k / hh;
        V3 d = gIn.ctrl ? V3(0, -dy * wpp, 0) : (vb.r * dx - vb.u * dy) * wpp;
        for (int i : s) gScene.objs[i].pos += d;
    } else if (gTool == 1) {
        float yaw = gIn.shift ? 0 : dx * 0.5f;
        M3 ry = rotY(rad(yaw));
        for (int i : s) {
            Object& o = gScene.objs[i];
            if (gIn.shift) o.rot.z += dx * 0.5f;
            else { o.rot.y += yaw; o.rot.x += dy * 0.5f; }
            if (s.size() > 1) o.pos = c + ry * (o.pos - c);
        }
    } else {
        float f = std::exp(-dy * 0.01f + dx * 0.005f);
        for (int i : s) {
            Object& o = gScene.objs[i];
            if (isLight(o.type)) continue;
            if (gIn.shift) o.scl.y = std::max(0.01f, o.scl.y * f);
            else o.scl = V3(std::max(0.01f, o.scl.x * f), std::max(0.01f, o.scl.y * f), std::max(0.01f, o.scl.z * f));
            if (s.size() > 1) o.pos = c + (o.pos - c) * f;
        }
    }
    sceneChanged();
}

void drawViewport() {
    R vp = gVP;
    if (gShowRender && gMainR.hasImage()) {
        fill(vp, rgb(16, 16, 18));
        blitFit(gMainR, vp);
    } else {
        vgrad(vp, rgb(84, 86, 92), rgb(132, 134, 140));
        ViewBasis vb = curBasis();
        float hh = vp.h * 0.5f, cx = vp.x + vp.w * 0.5f, cy = vp.y + hh;
        IntersectClipRect(gDC, vp.x, vp.y, vp.x + vp.w, vp.y + vp.h);
        SelectObject(gDC, GetStockObject(DC_PEN));
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
            MoveToEx(gDC, x0, y0, nullptr);
            LineTo(gDC, x1, y1);
        };
        SetDCPenColor(gDC, rgb(112, 114, 122));
        for (int k = -10; k <= 10; k++) { seg(V3(k * 5.f, 0, -50), V3(k * 5.f, 0, 50)); seg(V3(-50, 0, k * 5.f), V3(50, 0, k * 5.f)); }
        std::vector<V3> s;
        auto drawObj = [&](int i) {
            const Object& o = gScene.objs[i];
            bool sel = isSel(i);
            COLORREF c = isInfinite(o.type) ? rgb(58, 78, 122) : rgb(24, 42, 140);
            if (isLight(o.type)) c = sel ? rgb(255, 140, 0) : rgb(240, 220, 60);
            else if (sel) c = i == gSel ? rgb(225, 28, 28) : rgb(235, 90, 70);
            else if (o.boolMode == BoolMode::Negative) c = rgb(20, 110, 40);
            else if (o.boolMode == BoolMode::Intersect) c = rgb(120, 40, 140);
            else if (o.locked) c = rgb(70, 70, 76);
            SetDCPenColor(gDC, c);
            s.clear();
            wireframe(o, s);
            for (size_t j = 0; j + 1 < s.size(); j += 2) seg(s[j], s[j + 1]);
        };
        for (int i = 0; i < (int)gScene.objs.size(); i++)
            if (!isSel(i) && !gScene.objs[i].hidden) drawObj(i);
        for (int i : gSelSet)
            if (i < (int)gScene.objs.size()) drawObj(i);
        SelectClipRgn(gDC, nullptr);
        const char* vn[4] = {"Camera View", "Top View", "Front View", "Side View"};
        const char* tn[3] = {"Move", "Rotate", "Resize"};
        char buf[160];
        snprintf(buf, sizeof buf, "%s   |   Tool: %s   |   Frame %d", vn[gView], tn[gTool], gScene.frame);
        textIn({vp.x + 10, vp.y + 6, vp.w - 20, 18}, buf, rgb(240, 240, 245), gBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    line(vp.x, vp.y, vp.x + vp.w, vp.y, rgb(28, 28, 32));

    if (hov(vp) && gIn.pressed && gActive == 0) {
        gActive = VIEW_ID;
        if (gShowRender) { gShowRender = false; gDrag = DragNone; }
        else {
            int p = pick(gIn.mx, gIn.my);
            if (p >= 0 && !(gIn.shift || isSel(p))) selectObj(p, false, gIn.alt);
            else if (p >= 0 && gIn.shift) selectObj(p, true, gIn.alt);
            else if (p >= 0) gSel = p;
            else if (!gIn.shift) clearSel();
            gDrag = p >= 0 && isSel(p) ? DragObj : DragCam;
            if (p >= 0) gStatus = "Selected " + gScene.objs[p].name + (gSelSet.size() > 1 ? " (+" + std::to_string(gSelSet.size() - 1) + " more)." : ".");
        }
    }
    if (gActive == VIEW_ID && gIn.down && (gIn.dx || gIn.dy)) {
        if (gDrag == DragObj && validSel()) dragObject();
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
    if (button({x + 60, y, 50, 24}, gPlaying ? "Stop" : "Play", gPlaying)) { gPlaying = !gPlaying; gPlayLast = GetTickCount(); }
    if (button({x + 112, y, 28, 24}, ">")) jumpKey(1);
    if (button({x + 142, y, 28, 24}, ">|")) setFrame(last);
    int rx = gW - 600;
    R tr{x + 180, y, rx - x - 190, 24};
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
    textIn({10, sb.y, gW - 360, sb.h}, gStatus.c_str(), TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (gMainR.busy() || gAnimRender) {
        R pb{gW - 330, sb.y + 5, 320, 14};
        fill(pb, rgb(30, 30, 34));
        vgrad({pb.x, pb.y, (int)(pb.w * gMainR.progress()), pb.h}, rgb(120, 200, 120), rgb(60, 130, 60));
        char buf[96];
        if (gAnimRender) snprintf(buf, sizeof buf, "Frame %d/%d  %d%%", std::max(gAnimFrame, 0), gScene.frames - 1, (int)(gMainR.progress() * 100));
        else snprintf(buf, sizeof buf, "Rendering %d%%", (int)(gMainR.progress() * 100));
        textIn(pb, buf, rgb(255, 255, 255), gSmall);
    } else
        textIn({gW - 600, sb.y, 590, sb.h}, "Shift+click multi-select   RMB orbit   MMB pan   Wheel zoom   Ctrl+Z undo   Ctrl+R render", TXT_DIM, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

// ---------------------------------------------------------------- labs

R panel(int w, int h, const std::string& title) {
    R P{(gW - w) / 2, (gH - h) / 2, w, h};
    vgrad(P, rgb(96, 96, 102), rgb(62, 62, 68));
    bevel(P, false);
    R t{P.x + 2, P.y + 2, P.w - 4, 30};
    vgrad(t, rgb(74, 86, 126), rgb(44, 52, 82));
    textIn({t.x + 12, t.y, t.w - 24, t.h}, title.c_str(), rgb(255, 255, 255), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    return P;
}

void labFooter(R P, bool cancelOnly = false) {
    if (!cancelOnly && button({P.x + P.w - 196, P.y + P.h - 40, 88, 28}, "OK", false, gBold)) closeLab(false);
    if (button({P.x + P.w - 100, P.y + P.h - 40, 88, 28}, cancelOnly ? "Close" : "Cancel", false, gBold)) closeLab(true);
}

void preview(R pv, const char* caption) {
    fill(pv, rgb(16, 16, 18));
    blitFit(gLabR, pv);
    bevel(pv, true);
    textIn({pv.x, pv.y + pv.h + 4, pv.w, 18}, gLabR.busy() ? "Rendering preview..." : caption, TXT_DIM, gSmall);
}

template <class T> int grid2(int x, int y, const std::vector<T>& items, std::function<const char*(const T&)> name, std::function<bool(const T&)> on,
                             std::function<void(const T&)> act, int bw = 110) {
    for (size_t i = 0; i < items.size(); i++)
        if (button({x + (int)(i % 2) * (bw + 4), y + (int)(i / 2) * 26, bw, 22}, name(items[i]), on(items[i]), gSmall)) act(items[i]);
    return y + (int)((items.size() + 1) / 2) * 26;
}

void drawMatLab() {
    if (!validSel()) { gLab = LabNone; return; }
    int sel = gSel;
    Object& o = gScene.objs[sel];
    Material& m = o.mat;
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
        Material c = m;
        c.category = "Library";
        c.img.reset();
        gLib.mats.erase(std::remove_if(gLib.mats.begin(), gLib.mats.end(), [&](const Material& e) { return e.name == c.name; }), gLib.mats.end());
        gLib.mats.push_back(c);
        saveLibrary();
        gStatus = "Added \"" + c.name + "\" to the library.";
    }
    if (lib && button({x + 114, y, 110, 24}, "Remove")) {
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
    y += 20 + 3 * 26 + 6;
    label(x2, y, "Colors");
    y += 20;
    swatch({x2, y, 224, 24}, "Color A", m.color1);
    y += 28;
    swatch({x2, y, 224, 24}, "Color B", m.color2);
    y += 34;
    if (button({x2, y, 224, 24}, "Load Picture (BMP)...")) {
        defer([sel] {
            std::string p;
            if (!fileDialog(false, "Bitmap Image (*.bmp)\0*.bmp\0", "bmp", p) || sel >= (int)gScene.objs.size()) return;
            auto img = loadImageCached(p);
            if (!img) { gStatus = "Could not read picture " + p + " (8, 24 or 32-bit uncompressed BMP required)."; return; }
            Material& mm = gScene.objs[sel].mat;
            mm.image = p;
            mm.img = img;
            mm.tex = TexType::Image;
            sceneChanged();
        });
    }
    y += 28;
    textIn({x2, y, 224, 18}, m.image.empty() ? "(no picture loaded)" : baseName(m.image).c_str(), TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
    note({P.x + P.w - 336, P.y + 390, 320, 120},
         "Pick a category and preset, then tune texture, mapping, colors and channels. Changes apply live; Cancel restores the original.");
    if (ch) sceneChanged();
    labFooter(P);
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
        Sky c = k;
        c.category = "Library";
        gLib.skies.erase(std::remove_if(gLib.skies.begin(), gLib.skies.end(), [&](const Sky& e) { return e.name == c.name; }), gLib.skies.end());
        gLib.skies.push_back(c);
        saveLibrary();
        gStatus = "Added \"" + c.name + "\" to the library.";
    }
    if (lib && button({x + 114, y, 110, 24}, "Remove")) {
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
    preview({P.x + P.w - 356, P.y + 46, 340, 255}, "Camera view preview");
    note({P.x + P.w - 356, P.y + 330, 340, 120},
         "Rainbows appear opposite a low sun. Use Key Sky on the timeline to animate between skies.");
    if (ch) sceneChanged();
    labFooter(P);
}

void drawTerrainLab() {
    if (!validSel() || !gScene.objs[gSel].hf) { gLab = LabNone; return; }
    Object& o = gScene.objs[gSel];
    Heightfield& f = *o.hf;
    bool ch = false;
    R P = panel(1100, 640, "Terrain Editor  -  " + o.name);
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
    if (hov(hm)) {
        int rp = (int)(gBrushSize * hm.w);
        SelectObject(gDC, GetStockObject(HOLLOW_BRUSH));
        SelectObject(gDC, GetStockObject(DC_PEN));
        SetDCPenColor(gDC, rgb(255, 80, 60));
        Ellipse(gDC, gIn.mx - rp, gIn.my - rp, gIn.mx + rp, gIn.my + rp);
    }
    if (gActive == id && gIn.down) {
        BrushMode bm = gBrush;
        if (gIn.shift && (bm == BrushMode::Raise || bm == BrushMode::Lower)) bm = bm == BrushMode::Raise ? BrushMode::Lower : BrushMode::Raise;
        hfBrush(f, (gIn.mx - hm.x) / (float)hm.w, (gIn.my - hm.y) / (float)hm.h, gBrushSize, gBrushStr * 0.03f, bm);
        ch = true;
    }
    note({hm.x, hm.y + hm.h + 6, hm.w, 36}, "Paint on the height map to sculpt (Shift swaps raise/lower). Bright = high, dark = low.");

    int x = P.x + 432, y = P.y + 46;
    label(x, y, "Generate / Filter");
    y += 20;
    struct Op { const char* n; std::function<void()> fn; } ops[] = {
        {"Fractal", [&] { hfFractal(f, ++gSeed, gRough, false); }},  {"Ridged", [&] { hfFractal(f, ++gSeed, gRough, true); }},
        {"Erode", [&] { hfErode(f, f.res * f.res, ++gSeed); }},       {"Smooth", [&] { hfSmooth(f); }},
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
        if (button({x + (i % 2) * 114, y + (i / 2) * 26, 110, 22}, ops[i].n, false, gSmall)) { ops[i].fn(); ch = true; }
    y += ((nops + 1) / 2) * 26 + 8;
    label(x, y, "Resolution");
    y += 20;
    const int resv[3] = {129, 257, 513};
    for (int i = 0; i < 3; i++) {
        char b[16];
        snprintf(b, sizeof b, "%d", resv[i] - 1);
        if (button({x + i * 76, y, 72, 22}, b, f.res == resv[i]) && f.res != resv[i]) { hfResample(f, resv[i]); ch = true; }
    }
    y += 30;
    label(x, y, "Settings");
    y += 20;
    slider({x, y, 224, 24}, "Roughness", gRough, 0.3f, 0.7f); y += 28;
    slider({x, y, 224, 24}, "Terrace Steps", gTerraces, 2, 20, "%.0f"); y += 28;
    slider({x, y, 224, 24}, "Brush Size", gBrushSize, 0.01f, 0.3f, "%.2f", true); y += 28;
    slider({x, y, 224, 24}, "Brush Strength", gBrushStr, 0.05f, 1); y += 28;
    const char* bn[4] = {"Raise", "Lower", "Smooth", "Flatten"};
    for (int i = 0; i < 4; i++)
        if (button({x + (i % 2) * 114, y + (i / 2) * 26, 110, 22}, (std::string("Brush: ") + bn[i]).c_str(), gBrush == (BrushMode)i, gSmall)) gBrush = (BrushMode)i;
    int sel = gSel;
    int x4 = P.x + P.w - 336;
    preview({x4, P.y + 46, 320, 240}, "Camera view preview");
    y = P.y + 320;
    label(x4, y, "Picture");
    y += 20;
    if (button({x4, y, 158, 24}, "Import Picture...")) {
        defer([sel] {
            std::string p;
            if (!fileDialog(false, "Bitmap Image (*.bmp)\0*.bmp\0", "bmp", p) || sel >= (int)gScene.objs.size() || !gScene.objs[sel].hf) return;
            auto im = loadBMP(p);
            if (!im) { gStatus = "Could not read " + p; return; }
            hfFromImage(*gScene.objs[sel].hf, *im);
            sceneChanged();
        });
    }
    if (button({x4 + 162, y, 158, 24}, "Export Height Map...")) {
        defer([sel] {
            std::string p;
            if (!fileDialog(true, BMP_FILTER, "bmp", p) || sel >= (int)gScene.objs.size() || !gScene.objs[sel].hf) return;
            const Heightfield& hf = *gScene.objs[sel].hf;
            std::vector<uint32_t> px(hf.h.size());
            for (size_t i = 0; i < px.size(); i++) { uint32_t v = (uint32_t)(clampf(hf.h[i], 0, 1) * 255); px[i] = (v << 16) | (v << 8) | v; }
            gStatus = saveBMP(p, hf.res, hf.res, px) ? "Exported height map " + p : "Could not write " + p;
        });
    }
    note({x4, y + 32, 320, 60}, "Imported pictures use brightness as height. Export saves the height map as a grayscale bitmap.");
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
    if (button({x, y, w, 26}, "New Random Shape")) { t.seed = ++gSeed * 7 + 3; ch = true; }
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
    R P = panel(820, 560, "Render Options");
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
    note({x, y, w, 90}, "Draft is fastest. Normal anti-aliases edges. Premium accumulates many rays per pixel for soft shadows "
                        "(set Soft Shadow Size on lights), depth of field and blurry reflections (set Blur in the Materials Lab).");
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
        r.focusDist = std::max(1.f, dot(gScene.objs[gSel].pos - vb.o, vb.f));
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
        (axis == 0 ? o.pos.x : axis == 1 ? o.pos.y : o.pos.z) += d;
    }
    sceneChanged();
}

void distributeSel(int axis) {
    auto s = sortedSel();
    if (s.size() < 3) { gStatus = "Select at least three objects to distribute."; return; }
    auto key = [&](int i) { return gScene.objs[i].pos[axis]; };
    std::sort(s.begin(), s.end(), [&](int a, int b) { return key(a) < key(b); });
    float a = key(s.front()), b = key(s.back());
    for (size_t k = 0; k < s.size(); k++) {
        float v = a + (b - a) * k / (float)(s.size() - 1);
        Object& o = gScene.objs[s[k]];
        (axis == 0 ? o.pos.x : axis == 1 ? o.pos.y : o.pos.z) = v;
    }
    sceneChanged();
}

void drawToolsLab() {
    R P = panel(780, 520, "Edit Tools  -  " + std::to_string(gSelSet.size()) + " object(s)");
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
    labFooter(P);
}

void drawExamplesLab() {
    R P = panel(640, 460, "Example Scenes");
    static const char* desc[] = {"Eroded snowy island, water and a chrome sphere.", "Terraced red mesas and sandstone rocks under desert haze.",
                                 "A moon rising over a dark lake with a glowing campfire.", "Boolean objects, glass, chrome torus and a spotlight.",
                                 "Generated trees in golden-hour light with ground fog.", "A floating lattice, hologram ring and gas giant on an alien world."};
    int y = P.y + 50;
    for (size_t i = 0; i < exampleNames().size(); i++) {
        if (button({P.x + 20, y, 180, 30}, exampleNames()[i].c_str(), false, gBold)) {
            int n = (int)i;
            closeLab(true);
            defer([n] { loadExample(n); });
        }
        textIn({P.x + 214, y, P.w - 234, 30}, desc[i], TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 38;
    }
    note({P.x + 20, y + 6, P.w - 40, 40}, "Loading an example replaces the current scene. All example content is original to Canyon 3D.");
    labFooter(P, true);
}

// ---------------------------------------------------------------- frame

void drawUI() {
    gIn.shift = GetKeyState(VK_SHIFT) < 0;
    gIn.ctrl = GetKeyState(VK_CONTROL) < 0;
    gIn.alt = GetKeyState(VK_MENU) < 0;
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
        default: break;
        }
    }
}

void doFrame() {
    for (int guard = 0; guard < 3; guard++) {
        gIdc = 0;
        drawUI();
        if (gIn.released) gActive = 0;
        gIn.pressed = gIn.released = false;
        gIn.dx = gIn.dy = 0;
        if (gDeferred.empty()) break;
        auto d = std::move(gDeferred);
        gDeferred.clear();
        for (auto& f : d) f();
    }
    InvalidateRect(gWnd, nullptr, FALSE);
}

void tick() {
    DWORD now = GetTickCount();
    if (gPlaying && now - gPlayLast >= 1000u / (DWORD)std::max(1, gScene.fps)) {
        gPlayLast = now;
        gScene.applyFrame((gScene.frame + 1) % gScene.frames);
        gShowRender = false;
        gNanoDirty = true;
        gLastChange = now - 1000;
        doFrame();
    }
    if (gNanoDirty && now - gLastChange > 120) { gNanoDirty = false; gNanoR.start(gScene, NANO_W, NANO_H, 0); }
    if (gLab != LabNone && gLabDirty && now - gLastChange > 120) { gLabDirty = false; startLabPreview(); }
    if (gAnimRender && !gMainR.busy()) {
        if (gAnimFrame >= 0) {
            if (!gMainR.finished()) gAnimRender = false;
            else {
                char fn[32];
                snprintf(fn, sizeof fn, "_%04d.bmp", gAnimFrame);
                std::vector<uint32_t> px;
                gMainR.copyTo(px);
                if (!saveBMP(gAnimBase + fn, gMainR.width(), gMainR.height(), px)) { gAnimRender = false; gStatus = "Could not write " + gAnimBase + fn; }
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
            if (gAnimRender) snprintf(b, sizeof b, "Animation rendered: %d frames as %s_0000.bmp ... in %.1f s.", gScene.frames, gAnimBase.c_str(), (now - gRenderStart) / 1000.f);
            else snprintf(b, sizeof b, "Animation render stopped at frame %d.", std::max(gAnimFrame, 0));
            if (gStatus.rfind("Could not", 0) != 0) gStatus = b;
            gAnimRender = false;
        }
        doFrame();
    }
    bool mb = gMainR.busy();
    if (gMainWasBusy && !mb && !gAnimRender && gMainR.finished()) {
        char b[96];
        snprintf(b, sizeof b, "Render complete in %.1f s. Click the image to return to the wireframe.", (now - gRenderStart) / 1000.f);
        gStatus = b;
    }
    gMainWasBusy = mb;
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

void handleKey(WPARAM wp) {
    bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
    if (wp == VK_ESCAPE) {
        if (gLab != LabNone) closeLab(true);
        else if (gMainR.busy() || gAnimRender) actRender();
        else gShowRender = false;
        return;
    }
    if (gLab != LabNone) return;
    beginInteraction();
    if (wp == VK_DELETE) actDelete();
    else if (ctrl && wp == 'Z') undoRedo(shift);
    else if (ctrl && wp == 'Y') undoRedo(true);
    else if (ctrl && wp == 'D') actDuplicate();
    else if (ctrl && wp == 'G') actGroup();
    else if (ctrl && wp == 'U') actUngroup();
    else if (ctrl && wp == 'A') { gSelSet.clear(); for (int i = 0; i < (int)gScene.objs.size(); i++) gSelSet.push_back(i); gSel = gSelSet.empty() ? -1 : 0; }
    else if (ctrl && wp == 'R') actRender();
    else if (ctrl && wp == 'S') actSave(false);
    else if (ctrl && wp == 'O') actOpen();
    else if (ctrl && wp == 'N') actNew();
    else if (ctrl && wp == 'L') actLand();
    else if (ctrl && wp == 'K') addKey();
    else if (wp == VK_SPACE) { gPlaying = !gPlaying; gPlayLast = GetTickCount(); }
    else if (wp >= '1' && wp <= '4') { gView = (int)(wp - '1'); gShowRender = false; }
    endInteraction();
}

LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (LOWORD(lp) > 0 && HIWORD(lp) > 0) { resizeCanvas(LOWORD(lp), HIWORD(lp)); doFrame(); }
        return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {1220, 780}; return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        if (gDC) BitBlt(dc, 0, 0, gW, gH, gDC, 0, 0, SRCCOPY);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (gModal) return 0;
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        gIn.dx = x - gIn.mx; gIn.dy = y - gIn.my; gIn.mx = x; gIn.my = y;
        if (gRDrag) { if (gView == 0) orbitCam((float)gIn.dx, (float)gIn.dy); else panView((float)gIn.dx, (float)gIn.dy); }
        if (gMDrag) panView((float)gIn.dx, (float)gIn.dy);
        doFrame();
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (gModal) return 0;
        SetCapture(hw);
        commitRename();
        beginInteraction();
        gIn.down = gIn.pressed = true;
        doFrame();
        return 0;
    case WM_LBUTTONUP:
        if (gModal) return 0;
        if (!gRDrag && !gMDrag) ReleaseCapture();
        gIn.down = false;
        gIn.released = true;
        doFrame();
        endInteraction();
        return 0;
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        if (gModal || gLab != LabNone || !gVP.in(GET_X_LPARAM(lp), GET_Y_LPARAM(lp))) return 0;
        (msg == WM_RBUTTONDOWN ? gRDrag : gMDrag) = true;
        gShowRender = false;
        SetCapture(hw);
        doFrame();
        return 0;
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        (msg == WM_RBUTTONUP ? gRDrag : gMDrag) = false;
        if (!gIn.down && !gRDrag && !gMDrag) ReleaseCapture();
        return 0;
    case WM_MOUSEWHEEL: {
        if (gModal || gLab != LabNone) return 0;
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hw, &pt);
        float d = GET_WHEEL_DELTA_WPARAM(wp) / 120.f;
        if (gListR.in(pt.x, pt.y)) gListScroll -= (int)(d * 2);
        else if (gVP.in(pt.x, pt.y)) { gShowRender = false; zoomView(d); }
        doFrame();
        return 0;
    }
    case WM_CHAR:
        if (gModal || gEditObj < 0) return 0;
        if (wp == 8) { if (!gEditBuf.empty()) gEditBuf.pop_back(); }
        else if (wp == 13) commitRename();
        else if (wp == 27) gEditObj = -1;
        else if (wp >= 32 && wp < 256 && gEditBuf.size() < 60) gEditBuf += (char)wp;
        doFrame();
        return 0;
    case WM_KEYDOWN:
        if (gModal || gEditObj >= 0) return 0;
        handleKey(wp);
        doFrame();
        return 0;
    case WM_TIMER: tick(); return 0;
    case WM_CLOSE:
        if (gModal || !confirmDiscard()) return 0;
        DestroyWindow(hw);
        return 0;
    case WM_DESTROY:
        gMainR.cancel(); gNanoR.cancel(); gLabR.cancel();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

HFONT makeFont(int px, int weight) {
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_SWISS, "Segoe UI");
}

// --render out.bmp [w h [scene.c3d]] | --example N out.bmp [w h [quality]] | --export-example N out.c3d
int headless(const char* cmd) {
    char out[MAX_PATH] = "", scene[MAX_PATH] = "";
    int w = 800, h = 500, q = -1, ex = 0;
    Scene s;
    if (std::strncmp(cmd, "--export-example", 16) == 0) {
        if (sscanf(cmd, "--export-example %d %259s", &ex, out) != 2) return 2;
        return makeExample(ex).save(out) ? 0 : 4;
    }
    if (std::strncmp(cmd, "--example", 9) == 0) {
        if (sscanf(cmd, "--example %d %259s %d %d %d", &ex, out, &w, &h, &q) < 2) return 2;
        s = makeExample(ex);
    } else {
        if (sscanf(cmd, "--render %259s %d %d %259s", out, &w, &h, scene) < 1) return 2;
        s = makeDefaultScene();
        if (scene[0] && !s.load(scene)) return 3;
    }
    if (w < 1 || h < 1 || w > 16384 || h > 16384) return 2;
    std::vector<uint32_t> px((size_t)w * h);
    renderImage(s, w, h, px.data(), q);
    return saveBMP(out, w, h, px) ? 0 : 4;
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int show) {
    if (cmd && std::strncmp(cmd, "--", 2) == 0) return headless(cmd);
    SetProcessDPIAware();
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    gLibPath = exe;
    gLibPath = gLibPath.substr(0, gLibPath.find_last_of("\\/") + 1) + "canyon3d_library.txt";
    gLib.load(gLibPath);
    gScene = makeDefaultScene();
    sceneReplaced();
    buildIcons();
    gFont = makeFont(15, FW_NORMAL);
    gSmall = makeFont(13, FW_NORMAL);
    gBold = makeFont(15, FW_SEMIBOLD);
    gTitle = makeFont(19, FW_BOLD);

    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = "Canyon3DWnd";
    RegisterClassA(&wc);
    RECT wa;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int ww = std::min<int>(1440, wa.right - wa.left), wh = std::min<int>(900, wa.bottom - wa.top);
    gWnd = CreateWindowExA(0, wc.lpszClassName, "Canyon 3D - Bryce-style landscape studio", WS_OVERLAPPEDWINDOW,
                           wa.left + (wa.right - wa.left - ww) / 2, wa.top + (wa.bottom - wa.top - wh) / 2, ww, wh, nullptr, nullptr, inst, nullptr);
    if (!gWnd) return 1;
    bool small = ww < 1440 || wh < 900;
    ShowWindow(gWnd, show == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : (small && (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT) ? SW_SHOWMAXIMIZED : show));
    SetTimer(gWnd, 1, 33, nullptr);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
