#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include <cstdio>
#include <cstring>
#include <functional>
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

const int TOP = 98, LEFT = 170, RIGHT = 236, STATUS = 24;
const int NANO_W = 150, NANO_H = 112, ICW = 60, ICH = 40, STW = 74, STH = 40;
const int VIEW_ID = 1000000;

HWND gWnd;
int gW = 1440, gH = 900;
HDC gDC;
HBITMAP gBmp;
uint32_t* gBits;
HFONT gFont, gSmall, gBold, gTitle;

struct Input {
    int mx = 0, my = 0, dx = 0, dy = 0;
    bool down = false, pressed = false, released = false, shift = false, ctrl = false;
} gIn;
int gActive = 0, gIdc = 0;
bool gBlocked = false, gModal = false;
std::vector<std::function<void()>> gDeferred;

Scene gScene;
int gSel = -1, gTab = 0, gView = 0, gTool = 0;
bool gShowRender = false, gAA = true;
Renderer gMainR, gNanoR, gLabR;
enum Lab { LabNone, LabMat, LabSky, LabTerrain } gLab = LabNone;
Material gMatBackup;
Sky gSkyBackup;
Heightfield gHfBackup;
bool gNanoDirty = true, gLabDirty = false, gMainWasBusy = false;
DWORD gLastChange = 0, gRenderStart = 0;
std::string gStatus = "Welcome to Canyon 3D. Create objects from the Create palette, then press Render.", gPath;
V3 gOrthoC[4];
float gOrthoS[4] = {40, 40, 40, 40};
int gCounter[(int)ObjType::COUNT] = {};
R gVP{0, 0, 1, 1}, gListR{0, 0, 0, 0};
int gListScroll = 0;
uint32_t gSeed = 100;
float gBrushSize = 0.06f, gBrushStr = 0.5f, gRough = 0.5f;
bool gBrushLower = false;
enum Drag { DragNone, DragObj, DragCam } gDrag = DragNone;
bool gRDrag = false, gMDrag = false;
std::vector<uint32_t> gIcons[(int)ObjType::COUNT], gSkyThumbs[16];

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

bool iconButton(R r, const std::vector<uint32_t>& img, int iw, int ih, const char* text, bool on = false) {
    int id = newId();
    press(r, id);
    bool h = hov(r), act = gActive == id && h;
    vgrad(r, on ? rgb(126, 142, 182) : (h ? rgb(134, 134, 142) : rgb(102, 102, 108)), on ? rgb(70, 82, 120) : rgb(66, 66, 72));
    bevel(r, act);
    if (!img.empty()) blit({r.x + (r.w - iw) / 2, r.y + 4, iw, ih}, img.data(), iw, ih);
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

bool dragF(R r, const char* text, float& v, float speed, float minV = -1e9f) {
    int id = newId();
    press(r, id);
    bool ch = false;
    if (gActive == id && gIn.down && gIn.dx) { v = std::max(minV, v + gIn.dx * speed * (gIn.shift ? 0.1f : 1.f)); ch = true; }
    fill(r, gActive == id ? rgb(70, 82, 120) : (hov(r) ? rgb(60, 60, 68) : rgb(46, 46, 52)));
    bevel(r, true);
    char buf[32];
    snprintf(buf, sizeof buf, "%.2f", v);
    textIn({r.x + 4, r.y, r.w - 8, r.h}, text, TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    textIn({r.x + 4, r.y, r.w - 8, r.h}, buf, TXT, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (hov(r)) SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
    return ch;
}

void sceneChanged() {
    gScene.updateAll();
    gNanoDirty = true;
    gLastChange = GetTickCount();
    if (gLab != LabNone) gLabDirty = true;
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

// ---------------------------------------------------------------- views & scene actions

bool validSel() { return gSel >= 0 && gSel < (int)gScene.objs.size(); }

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

void createObject(ObjType t) {
    Object o = makeObject(t, ++gCounter[(int)t]);
    if (!isInfinite(t)) { o.pos.x = gScene.cam.target.x; o.pos.z = gScene.cam.target.z; }
    o.update();
    gScene.objs.push_back(o);
    gSel = (int)gScene.objs.size() - 1;
    gShowRender = false;
    sceneChanged();
    gStatus = "Created " + o.name + ".";
}

void actDelete() {
    if (!validSel()) return;
    gStatus = "Deleted " + gScene.objs[gSel].name + ".";
    gScene.objs.erase(gScene.objs.begin() + gSel);
    gSel = -1;
    sceneChanged();
}

void actDuplicate() {
    if (!validSel()) return;
    Object o = gScene.objs[gSel];
    if (o.hf) o.hf = std::make_shared<Heightfield>(*o.hf);
    o.name += " copy";
    o.pos += V3(2, 0, 2);
    gScene.objs.push_back(o);
    gSel = (int)gScene.objs.size() - 1;
    sceneChanged();
    gStatus = "Duplicated object.";
}

void actLand() {
    if (!validSel()) return;
    Object& o = gScene.objs[gSel];
    if (isInfinite(o.type)) return;
    float minY = 1e30f;
    for (int i = 0; i < 8; i++) {
        V3 c((i & 1) ? 1.f : -1.f, (i & 2) ? 1.f : (o.type == ObjType::Terrain ? 0.f : -1.f), (i & 4) ? 1.f : -1.f);
        minY = std::min(minY, (o.fwd * c).y);
    }
    if (o.type == ObjType::Light) minY = -1;
    Hit h;
    float ground = intersectScene(gScene, {o.pos, V3(0, -1, 0)}, 1e30f, h, gSel) ? h.p.y : 0.f;
    o.pos.y = ground - minY;
    sceneChanged();
    gStatus = "Landed " + o.name + " on the surface below.";
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

void resetCounters() {
    for (int& c : gCounter) c = 0;
    for (auto& o : gScene.objs) gCounter[(int)o.type]++;
}

void actNew() {
    gModal = true;
    int r = MessageBoxA(gWnd, "Discard the current scene and start a new one?", "Canyon 3D", MB_YESNO | MB_ICONQUESTION);
    gModal = false;
    gIn.down = false;
    gActive = 0;
    if (r != IDYES) return;
    gMainR.cancel();
    gScene = makeEmptyScene();
    gSel = -1;
    gPath.clear();
    gShowRender = false;
    resetCounters();
    sceneChanged();
    gStatus = "New scene.";
}

void actOpen() {
    std::string p;
    if (!fileDialog(false, SCENE_FILTER, "c3d", p)) return;
    Scene s;
    if (!s.load(p)) { gStatus = "Could not read scene file: " + p; return; }
    gMainR.cancel();
    gScene = std::move(s);
    gSel = -1;
    gPath = p;
    gShowRender = false;
    resetCounters();
    sceneChanged();
    gStatus = "Opened " + p;
}

void actSave(bool as) {
    std::string p = gPath;
    if ((as || p.empty()) && !fileDialog(true, SCENE_FILTER, "c3d", p)) return;
    if (!gScene.save(p)) { gStatus = "Could not write " + p; return; }
    gPath = p;
    gStatus = "Saved " + p;
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
    if (!fileDialog(true, "Bitmap Image (*.bmp)\0*.bmp\0", "bmp", p)) return;
    std::vector<uint32_t> px;
    gMainR.copyTo(px);
    gStatus = saveBMP(p, gMainR.width(), gMainR.height(), px) ? "Saved image " + p : "Could not write " + p;
}

void actRender() {
    if (gMainR.busy()) {
        gMainWasBusy = false;
        gMainR.cancel();
        gStatus = "Render stopped.";
        return;
    }
    gView = 0;
    gShowRender = true;
    gRenderStart = GetTickCount();
    gMainR.start(gScene, gVP.w, gVP.h, gAA);
    gStatus = "Rendering...";
}

void openLab(Lab l) {
    if (l == LabMat || l == LabTerrain) {
        if (!validSel()) { gStatus = "Select an object first."; return; }
        Object& o = gScene.objs[gSel];
        if (l == LabMat && o.type == ObjType::Light) { gStatus = "Lights have no material; edit color and power in the side panel."; return; }
        if (l == LabTerrain && (o.type != ObjType::Terrain || !o.hf)) { gStatus = "Select a terrain to open the Terrain Editor."; return; }
        if (l == LabMat) gMatBackup = o.mat; else gHfBackup = *o.hf;
    } else gSkyBackup = gScene.sky;
    gLab = l;
    gLabDirty = true;
    gLastChange = 0;
}

void closeLab(bool cancel) {
    if (cancel) {
        if (gLab == LabSky) gScene.sky = gSkyBackup;
        else if (validSel() && gLab == LabMat) gScene.objs[gSel].mat = gMatBackup;
        else if (validSel() && gLab == LabTerrain && gScene.objs[gSel].hf) *gScene.objs[gSel].hf = gHfBackup;
    }
    gLabR.cancel();
    gLab = LabNone;
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
        gLabR.start(s, 280, 280, true);
    } else if (gLab == LabSky) gLabR.start(gScene, 400, 300, false);
    else if (gLab == LabTerrain) gLabR.start(gScene, 320, 240, false);
}

// ---------------------------------------------------------------- panels

void drawTop() {
    R bar{0, 0, gW, TOP};
    vgrad(bar, rgb(108, 108, 116), rgb(60, 60, 66));
    line(0, TOP - 1, gW, TOP - 1, rgb(28, 28, 32));
    const char* tabs[3] = {"Create", "Edit", "Sky & Fog"};
    for (int i = 0; i < 3; i++)
        if (button({10 + i * 96, 4, 92, 22}, tabs[i], gTab == i, gBold)) gTab = i;
    textIn({310, 4, 400, 22}, "CANYON 3D", rgb(240, 240, 245), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    int fx = gW - 4 * 76 - 6;
    if (button({fx, 4, 72, 22}, "New")) defer(actNew);
    if (button({fx + 76, 4, 72, 22}, "Open...")) defer(actOpen);
    if (button({fx + 152, 4, 72, 22}, "Save")) defer([] { actSave(false); });
    if (button({fx + 228, 4, 72, 22}, "Save As...")) defer([] { actSave(true); });

    if (gTab == 0) {
        int x = 10;
        for (int t = 0; t < (int)ObjType::COUNT; t++, x += ICW + 14)
            if (iconButton({x, 30, ICW + 10, 62}, gIcons[t], ICW, ICH, objTypeName((ObjType)t))) createObject((ObjType)t);
        textIn({x + 10, 30, gW - x - 20, 62}, "Click a shape to add it at the camera target.", TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else if (gTab == 1) {
        const char* tools[3] = {"Move", "Rotate", "Resize"};
        for (int i = 0; i < 3; i++)
            if (button({10 + i * 84, 32, 80, 24}, tools[i], gTool == i)) gTool = i;
        int x = 270;
        if (button({x, 32, 112, 24}, "Materials Lab")) openLab(LabMat);
        x += 116;
        if (button({x, 32, 112, 24}, "Terrain Editor")) openLab(LabTerrain);
        x += 116;
        if (button({x, 32, 70, 24}, "Land")) actLand();
        x += 74;
        if (button({x, 32, 84, 24}, "Duplicate")) actDuplicate();
        x += 88;
        if (button({x, 32, 70, 24}, "Delete")) actDelete();
        textIn({x + 82, 32, 520, 24}, "Drag objects in the view. Ctrl = vertical move, Shift = roll / stretch Y.", TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        textIn({10, 64, 110, 24}, "Quick materials:", TXT_DIM, gSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int qx = 120;
        for (auto& m : materialPresets()) {
            if (qx + 92 > gW - 10) break;
            if (button({qx, 64, 88, 24}, m.name.c_str(), validSel() && gScene.objs[gSel].mat.name == m.name, gSmall) && validSel() &&
                gScene.objs[gSel].type != ObjType::Light) {
                gScene.objs[gSel].mat = m;
                sceneChanged();
            }
            qx += 92;
        }
    } else {
        Sky& k = gScene.sky;
        auto& sp = skyPresets();
        for (size_t i = 0; i < sp.size(); i++)
            if (iconButton({10 + (int)i * (STW + 10), 30, STW + 6, 62}, gSkyThumbs[i], STW, STH, sp[i].name.c_str(), k.name == sp[i].name)) {
                k = sp[i];
                sceneChanged();
            }
        int sx = 10 + (int)sp.size() * (STW + 10) + 10;
        bool ch = false;
        ch |= slider({sx, 32, 170, 24}, "Sun Altitude", k.sunAlt, -10, 90, "%.0f");
        ch |= slider({sx + 176, 32, 170, 24}, "Sun Azimuth", k.sunAz, 0, 360, "%.0f");
        ch |= slider({sx + 352, 32, 170, 24}, "Haze", k.haze, 0, 1);
        ch |= slider({sx, 62, 170, 24}, "Fog", k.fog, 0, 1);
        ch |= slider({sx + 176, 62, 170, 24}, "Fog Height", k.fogHeight, 0, 40, "%.1f");
        ch |= slider({sx + 352, 62, 170, 24}, "Clouds", k.clouds, 0, 1);
        if (ch) sceneChanged();
        if (button({sx + 530, 32, 96, 54}, "Sky Lab", false, gBold)) openLab(LabSky);
    }
}

void drawLeft() {
    R p{0, TOP, LEFT, gH - TOP - STATUS};
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
    y += 54;

    int tid = newId();
    R tb{x + 2, y, 84, 84};
    press(tb, tid);
    shadedBall(tb.x + 42, tb.y + 42, 40, V3(0.45f, 0.55f, 0.8f), hov(tb) || gActive == tid);
    textIn({tb.x, tb.y + 30, tb.w, 22}, "Orbit", rgb(255, 255, 255), gBold);
    if (gActive == tid && gIn.down && (gIn.dx || gIn.dy)) { gView = 0; gShowRender = false; orbitCam((float)gIn.dx, (float)gIn.dy); }

    int pid = newId();
    R pp{x + 96, y, 54, 40};
    press(pp, pid);
    vgrad(pp, rgb(120, 120, 128), rgb(70, 70, 78));
    bevel(pp, gActive == pid);
    textIn(pp, "Pan", TXT, gBold);
    if (gActive == pid && gIn.down && (gIn.dx || gIn.dy)) { gShowRender = false; panView((float)gIn.dx, (float)gIn.dy); }

    int zid = newId();
    R zb{x + 96, y + 44, 54, 40};
    press(zb, zid);
    vgrad(zb, rgb(120, 120, 128), rgb(70, 70, 78));
    bevel(zb, gActive == zid);
    textIn(zb, "Zoom", TXT, gBold);
    if (gActive == zid && gIn.down && gIn.dy) { gShowRender = false; zoomView(-gIn.dy * 0.05f); }
    y += 96;

    int rid = newId();
    R rb{x + 75 - 34, y, 68, 68};
    press(rb, rid);
    bool busy = gMainR.busy();
    shadedBall(rb.x + 34, rb.y + 34, 33, busy ? V3(0.8f, 0.25f, 0.2f) : V3(0.25f, 0.65f, 0.3f), hov(rb));
    textIn({x, y + 70, 150, 18}, busy ? "STOP" : "RENDER", rgb(255, 255, 255), gBold);
    if (clicked(rb, rid)) actRender();
    y += 94;
    if (button({x, y, 44, 22}, "AA", gAA)) gAA = !gAA;
    if (button({x + 48, y, 100, 22}, gShowRender ? "Wireframe" : "Show Render", false) && gMainR.hasImage()) gShowRender = !gShowRender;
    y += 26;
    if (button({x, y, 148, 22}, "Save Image...")) defer(actSaveImage);
}

void drawRight() {
    int x0 = gW - RIGHT;
    vgrad({x0, TOP, RIGHT, gH - TOP - STATUS}, rgb(90, 90, 96), rgb(56, 56, 62));
    int x = x0 + 10, w = RIGHT - 20, y = TOP + 8;
    label(x, y, "Scene Objects");
    y += 18;
    const int rows = 11, n = (int)gScene.objs.size();
    gListR = {x, y, w, rows * 18 + 4};
    fill(gListR, rgb(38, 38, 44));
    bevel(gListR, true);
    gListScroll = std::max(0, std::min(gListScroll, n - rows));
    for (int k = 0; k < rows && gListScroll + k < n; k++) {
        int idx = gListScroll + k;
        R row{x + 2, y + 2 + k * 18, w - 4, 18};
        if (idx == gSel) fill(row, rgb(92, 108, 156));
        else if (hov(row)) fill(row, rgb(60, 60, 70));
        const Object& o = gScene.objs[idx];
        textIn({row.x + 6, row.y, row.w - 70, row.h}, o.name.c_str(), TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        textIn({row.x, row.y, row.w - 6, row.h}, objTypeName(o.type), TXT_DIM, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        int id = newId();
        press(row, id);
        if (gActive == id && gIn.pressed) gSel = idx;
    }
    y += gListR.h + 10;
    int cw = (w - 8) / 3;
    auto row3 = [&](const char* title, V3& v, float speed, float minV) {
        label(x, y, title);
        y += 18;
        bool ch = dragF({x, y, cw, 20}, "X", v.x, speed, minV);
        ch |= dragF({x + cw + 4, y, cw, 20}, "Y", v.y, speed, minV);
        ch |= dragF({x + 2 * (cw + 4), y, cw, 20}, "Z", v.z, speed, minV);
        y += 26;
        if (ch) sceneChanged();
    };
    if (validSel()) {
        Object& o = gScene.objs[gSel];
        textIn({x, y, w, 20}, o.name.c_str(), rgb(255, 255, 255), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 26;
        row3("Position", o.pos, 0.05f, -1e9f);
        if (o.type != ObjType::Light) {
            row3("Rotation", o.rot, 0.5f, -1e9f);
            row3("Size", o.scl, 0.02f, 0.01f);
            label(x, y, "Material");
            y += 18;
            textIn({x, y, w, 18}, o.mat.name.c_str(), TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            y += 22;
            if (button({x, y, w, 24}, "Materials Lab...")) openLab(LabMat);
            y += 28;
            if (o.type == ObjType::Terrain) {
                if (button({x, y, w, 24}, "Terrain Editor...")) openLab(LabTerrain);
                y += 28;
            }
        } else {
            swatch({x, y, w, 24}, "Light Color", o.lightColor);
            y += 28;
            if (slider({x, y, w, 24}, "Power", o.lightPower, 0, 5)) sceneChanged();
            y += 30;
        }
        int bw = (w - 8) / 3;
        if (button({x, y, bw, 24}, "Land")) actLand();
        if (button({x + bw + 4, y, bw, 24}, "Duplicate")) actDuplicate();
        if (button({x + 2 * (bw + 4), y, bw, 24}, "Delete")) actDelete();
    } else {
        Camera& c = gScene.cam;
        textIn({x, y, w, 20}, "Camera", rgb(255, 255, 255), gTitle, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        y += 26;
        bool ch = slider({x, y, w, 22}, "Field of View", c.fov, 15, 100, "%.0f");
        y += 28;
        row3("Target", c.target, 0.05f, -1e9f);
        label(x, y, "Orbit");
        y += 18;
        ch |= dragF({x, y, cw, 20}, "Yaw", c.yaw, 0.5f);
        ch |= dragF({x + cw + 4, y, cw, 20}, "Pitch", c.pitch, 0.3f);
        ch |= dragF({x + 2 * (cw + 4), y, cw, 20}, "Dist", c.dist, 0.2f, 1.f);
        c.pitch = clampf(c.pitch, -89, 89);
        y += 28;
        if (ch) sceneChanged();
        if (button({x, y, w, 24}, "Reset Camera")) { c = Camera(); sceneChanged(); }
        y += 34;
        textIn({x, y, w, 60}, "Click an object in the view or list to select it.", TXT_DIM, gSmall, DT_LEFT | DT_WORDBREAK);
    }
}

int pick(int mx, int my) {
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f, cx = gVP.x + gVP.w * 0.5f, cy = gVP.y + hh;
    for (int i = 0; i < (int)gScene.objs.size(); i++) {
        if (gScene.objs[i].type != ObjType::Light) continue;
        V3 c = vb.toCam(gScene.objs[i].pos);
        if (!vb.ortho && c.z < 0.1f) continue;
        float d = vb.ortho ? 1.f : c.z;
        float sx = cx + c.x / (d * vb.k) * hh, sy = cy - c.y / (d * vb.k) * hh;
        if (std::fabs(sx - mx) < 10 && std::fabs(sy - my) < 10) return i;
    }
    Hit h;
    return intersectScene(gScene, vb.ray((mx - cx) / hh, -(my - cy) / hh), 1e30f, h) ? h.obj : -1;
}

void dragObject() {
    Object& o = gScene.objs[gSel];
    ViewBasis vb = curBasis();
    float hh = gVP.h * 0.5f, dx = (float)gIn.dx, dy = (float)gIn.dy;
    if (gTool == 0 || o.type == ObjType::Light) {
        float wpp = (vb.ortho ? 1.f : std::max(0.5f, vb.toCam(o.pos).z)) * vb.k / hh;
        if (gIn.ctrl) o.pos.y -= dy * wpp;
        else o.pos += (vb.r * dx - vb.u * dy) * wpp;
    } else if (gTool == 1) {
        if (gIn.shift) o.rot.z += dx * 0.5f;
        else { o.rot.y += dx * 0.5f; o.rot.x += dy * 0.5f; }
    } else {
        float f = std::exp(-dy * 0.01f + dx * 0.005f);
        if (gIn.shift) o.scl.y = std::max(0.01f, o.scl.y * f);
        else o.scl = V3(std::max(0.01f, o.scl.x * f), std::max(0.01f, o.scl.y * f), std::max(0.01f, o.scl.z * f));
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
            bool sel = i == gSel;
            SetDCPenColor(gDC, o.type == ObjType::Light ? (sel ? rgb(255, 140, 0) : rgb(240, 220, 60))
                                : sel ? rgb(225, 28, 28) : (isInfinite(o.type) ? rgb(58, 78, 122) : rgb(24, 42, 140)));
            s.clear();
            wireframe(o, s);
            for (size_t j = 0; j + 1 < s.size(); j += 2) seg(s[j], s[j + 1]);
        };
        for (int i = 0; i < (int)gScene.objs.size(); i++)
            if (i != gSel) drawObj(i);
        if (validSel()) drawObj(gSel);
        SelectClipRgn(gDC, nullptr);
        const char* vn[4] = {"Camera View", "Top View", "Front View", "Side View"};
        const char* tn[3] = {"Move", "Rotate", "Resize"};
        char buf[128];
        snprintf(buf, sizeof buf, "%s   |   Tool: %s", vn[gView], tn[gTool]);
        textIn({vp.x + 10, vp.y + 6, vp.w - 20, 18}, buf, rgb(240, 240, 245), gBold, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    line(vp.x, vp.y, vp.x + vp.w, vp.y, rgb(28, 28, 32));

    if (hov(vp) && gIn.pressed && gActive == 0) {
        gActive = VIEW_ID;
        if (gShowRender) { gShowRender = false; gDrag = DragNone; }
        else {
            int p = pick(gIn.mx, gIn.my);
            gSel = p;
            gDrag = p >= 0 ? DragObj : DragCam;
            if (p >= 0) gStatus = "Selected " + gScene.objs[p].name + ".";
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

void drawStatus() {
    R sb{0, gH - STATUS, gW, STATUS};
    vgrad(sb, rgb(70, 70, 76), rgb(44, 44, 48));
    line(0, sb.y, gW, sb.y, rgb(28, 28, 32));
    textIn({10, sb.y, gW - 360, sb.h}, gStatus.c_str(), TXT, gFont, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (gMainR.busy()) {
        R pb{gW - 330, sb.y + 5, 320, 14};
        fill(pb, rgb(30, 30, 34));
        vgrad({pb.x, pb.y, (int)(pb.w * gMainR.progress()), pb.h}, rgb(120, 200, 120), rgb(60, 130, 60));
        char buf[64];
        snprintf(buf, sizeof buf, "Rendering %d%%", (int)(gMainR.progress() * 100));
        textIn(pb, buf, rgb(255, 255, 255), gSmall);
    } else
        textIn({gW - 600, sb.y, 590, sb.h}, "LMB select/drag   RMB orbit   MMB pan   Wheel zoom   Ctrl+R render", TXT_DIM, gSmall, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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

void labFooter(R P) {
    if (button({P.x + P.w - 196, P.y + P.h - 40, 88, 28}, "OK", false, gBold)) closeLab(false);
    if (button({P.x + P.w - 100, P.y + P.h - 40, 88, 28}, "Cancel", false, gBold)) closeLab(true);
}

void preview(R pv, const char* caption) {
    fill(pv, rgb(16, 16, 18));
    blitFit(gLabR, pv);
    bevel(pv, true);
    textIn({pv.x, pv.y + pv.h + 4, pv.w, 18}, gLabR.busy() ? "Rendering preview..." : caption, TXT_DIM, gSmall);
}

void drawMatLab() {
    if (!validSel()) { gLab = LabNone; return; }
    Object& o = gScene.objs[gSel];
    Material& m = o.mat;
    bool ch = false;
    R P = panel(1000, 600, "Materials Lab  -  " + o.name);
    int x = P.x + 16, y = P.y + 46;
    label(x, y, "Presets");
    y += 20;
    auto& pr = materialPresets();
    for (size_t i = 0; i < pr.size(); i++)
        if (button({x + (int)(i % 2) * 112, y + (int)(i / 2) * 26, 108, 22}, pr[i].name.c_str(), m.name == pr[i].name)) { m = pr[i]; ch = true; }
    int x2 = P.x + 252;
    y = P.y + 46;
    label(x2, y, "Texture");
    y += 20;
    for (int t = 0; t < (int)TexType::COUNT; t++)
        if (button({x2 + (t % 2) * 112, y + (t / 2) * 26, 108, 22}, texName((TexType)t), m.tex == (TexType)t)) { m.tex = (TexType)t; ch = true; }
    y += (((int)TexType::COUNT + 1) / 2) * 26 + 12;
    label(x2, y, "Colors");
    y += 20;
    swatch({x2, y, 220, 24}, "Color A", m.color1);
    y += 28;
    swatch({x2, y, 220, 24}, "Color B", m.color2);
    int x3 = P.x + 490, sw = 180;
    y = P.y + 46;
    label(x3, y, "Channels");
    y += 20;
    struct S { const char* n; float* v; float lo, hi; const char* fmt; bool lg; } ss[] = {
        {"Diffuse", &m.diffuse, 0, 1, "%.2f", false},      {"Ambient", &m.ambient, 0, 1, "%.2f", false},
        {"Specular", &m.specular, 0, 1, "%.2f", false},    {"Shininess", &m.shininess, 1, 200, "%.0f", false},
        {"Reflection", &m.reflect, 0, 1, "%.2f", false},   {"Transparency", &m.transparency, 0, 1, "%.2f", false},
        {"Refraction", &m.ior, 1, 2.5f, "%.2f", false},    {"Bump", &m.bump, 0, 2, "%.2f", false},
        {"Scale", &m.texScale, 0.01f, 20, "%.3f", true},   {"Metallic", &m.metal, 0, 1, "%.2f", false},
    };
    for (auto& s : ss) { ch |= slider({x3, y, sw, 24}, s.n, *s.v, s.lo, s.hi, s.fmt, s.lg); y += 30; }
    preview({P.x + P.w - 316, P.y + 46, 300, 300}, m.name.c_str());
    textIn({P.x + P.w - 316, P.y + 380, 300, 120},
           "Pick a preset, then tune the texture, colors and channels. Changes apply live to the selected object; Cancel restores it.",
           TXT_DIM, gSmall, DT_LEFT | DT_WORDBREAK);
    if (ch) sceneChanged();
    labFooter(P);
}

void drawSkyLab() {
    Sky& k = gScene.sky;
    bool ch = false;
    R P = panel(1000, 600, "Sky Lab");
    int x = P.x + 16, y = P.y + 46, w = 220;
    label(x, y, "Presets");
    y += 20;
    auto& sp = skyPresets();
    for (size_t i = 0; i < sp.size(); i++)
        if (button({x + (int)(i % 2) * 112, y + (int)(i / 2) * 26, 108, 22}, sp[i].name.c_str(), k.name == sp[i].name)) { k = sp[i]; ch = true; }
    y += (int)((sp.size() + 1) / 2) * 26 + 10;
    label(x, y, "Sun / Moon");
    y += 20;
    ch |= slider({x, y, w, 24}, "Altitude", k.sunAlt, -10, 90, "%.0f"); y += 28;
    ch |= slider({x, y, w, 24}, "Azimuth", k.sunAz, 0, 360, "%.0f"); y += 28;
    ch |= slider({x, y, w, 24}, "Disc Size", k.sunSize, 0.2f, 6, "%.1f"); y += 34;
    label(x, y, "Colors");
    y += 20;
    swatch({x, y, w, 24}, "Zenith", k.zenith); y += 28;
    swatch({x, y, w, 24}, "Horizon", k.horizon); y += 28;
    swatch({x, y, w, 24}, "Sun", k.sunColor);
    int x2 = P.x + 252;
    y = P.y + 46;
    label(x2, y, "Atmosphere");
    y += 20;
    ch |= slider({x2, y, w, 24}, "Haze", k.haze, 0, 1); y += 28;
    ch |= slider({x2, y, w, 24}, "Fog Density", k.fog, 0, 1); y += 28;
    ch |= slider({x2, y, w, 24}, "Fog Height", k.fogHeight, 0, 40, "%.1f"); y += 28;
    ch |= slider({x2, y, w, 24}, "Cloud Cover", k.clouds, 0, 1); y += 28;
    ch |= slider({x2, y, w, 24}, "Cloud Scale", k.cloudScale, 0.1f, 5, "%.2f", true); y += 28;
    ch |= slider({x2, y, w, 24}, "Ambient", k.ambient, 0, 1.5f); y += 28;
    ch |= slider({x2, y, w, 24}, "Stars", k.stars, 0, 1); y += 28;
    if (button({x2, y, w, 24}, k.shadows ? "Shadows: On" : "Shadows: Off", k.shadows)) { k.shadows = !k.shadows; ch = true; }
    y += 34;
    swatch({x2, y, w, 24}, "Haze Color", k.hazeColor); y += 28;
    swatch({x2, y, w, 24}, "Fog Color", k.fogColor); y += 28;
    swatch({x2, y, w, 24}, "Cloud Color", k.cloudColor); y += 28;
    swatch({x2, y, w, 24}, "Ambient Color", k.ambientColor);
    preview({P.x + P.w - 416, P.y + 46, 400, 300}, "Camera view preview");
    if (ch) sceneChanged();
    labFooter(P);
}

void drawTerrainLab() {
    if (!validSel() || !gScene.objs[gSel].hf) { gLab = LabNone; return; }
    Object& o = gScene.objs[gSel];
    Heightfield& f = *o.hf;
    bool ch = false;
    R P = panel(1000, 600, "Terrain Editor  -  " + o.name);
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
        bool lower = gBrushLower != gIn.shift;
        hfBrush(f, (gIn.mx - hm.x) / (float)hm.w, (gIn.my - hm.y) / (float)hm.h, gBrushSize, (lower ? -1.f : 1.f) * gBrushStr * 0.03f);
        ch = true;
    }
    textIn({hm.x, hm.y + hm.h + 6, hm.w, 36}, "Paint on the height map to sculpt (Shift inverts the brush). Bright = high, dark = low.",
           TXT_DIM, gSmall, DT_LEFT | DT_WORDBREAK);

    int x = P.x + 432, y = P.y + 46;
    label(x, y, "Generate / Filter");
    y += 20;
    struct Op { const char* n; std::function<void()> fn; } ops[] = {
        {"Fractal", [&] { hfFractal(f, ++gSeed, gRough, false); }}, {"Ridged", [&] { hfFractal(f, ++gSeed, gRough, true); }},
        {"Erode", [&] { hfErode(f, f.res * f.res, ++gSeed); }},      {"Smooth", [&] { hfSmooth(f); }},
        {"Raise", [&] { hfAdd(f, 0.05f); }},                         {"Lower", [&] { hfAdd(f, -0.05f); }},
        {"Invert", [&] { hfInvert(f); }},                            {"Plateau", [&] { hfPlateau(f, 0.6f); }},
        {"Equalize", [&] { hfNormalize(f); }},                       {"Clear", [&] { hfInit(f, f.res); }},
    };
    for (int i = 0; i < 10; i++)
        if (button({x + (i % 2) * 112, y + (i / 2) * 26, 108, 22}, ops[i].n)) { ops[i].fn(); ch = true; }
    y += 5 * 26 + 10;
    label(x, y, "Resolution");
    y += 20;
    const int resv[3] = {129, 257, 513};
    for (int i = 0; i < 3; i++) {
        char b[16];
        snprintf(b, sizeof b, "%d", resv[i] - 1);
        if (button({x + i * 74, y, 70, 22}, b, f.res == resv[i]) && f.res != resv[i]) { hfResample(f, resv[i]); ch = true; }
    }
    y += 34;
    label(x, y, "Settings");
    y += 20;
    slider({x, y, 220, 24}, "Roughness", gRough, 0.3f, 0.7f); y += 28;
    slider({x, y, 220, 24}, "Brush Size", gBrushSize, 0.01f, 0.3f, "%.2f", true); y += 28;
    slider({x, y, 220, 24}, "Brush Strength", gBrushStr, 0.05f, 1); y += 28;
    if (button({x, y, 108, 22}, "Brush: Raise", !gBrushLower)) gBrushLower = false;
    if (button({x + 112, y, 108, 22}, "Brush: Lower", gBrushLower)) gBrushLower = true;
    preview({P.x + P.w - 336, P.y + 46, 320, 240}, "Camera view preview");
    if (ch) sceneChanged();
    labFooter(P);
}

// ---------------------------------------------------------------- frame

void drawUI() {
    gIn.shift = GetKeyState(VK_SHIFT) < 0;
    gIn.ctrl = GetKeyState(VK_CONTROL) < 0;
    gVP = {LEFT, TOP, std::max(1, gW - LEFT - RIGHT), std::max(1, gH - TOP - STATUS)};
    gBlocked = gLab != LabNone || gModal;
    drawTop();
    drawLeft();
    drawRight();
    drawViewport();
    drawStatus();
    if (gLab != LabNone) {
        dimAll();
        gBlocked = gModal;
        if (gLab == LabMat) drawMatLab();
        else if (gLab == LabSky) drawSkyLab();
        else drawTerrainLab();
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
    if (gNanoDirty && now - gLastChange > 120) { gNanoDirty = false; gNanoR.start(gScene, NANO_W, NANO_H, false); }
    if (gLab != LabNone && gLabDirty && now - gLastChange > 120) { gLabDirty = false; startLabPreview(); }
    bool mb = gMainR.busy();
    if (gMainWasBusy && !mb) {
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

void buildIcons() {
    for (int t = 0; t < (int)ObjType::COUNT; t++) {
        auto& img = gIcons[t];
        img.assign(ICW * ICH, 0);
        ObjType type = (ObjType)t;
        if (type == ObjType::Light) {
            for (int y = 0; y < ICH; y++)
                for (int x = 0; x < ICW; x++) {
                    float d = std::sqrt((float)((x - ICW / 2) * (x - ICW / 2) + (y - ICH / 2) * (y - ICH / 2)));
                    V3 c = lerp(V3(0.12f, 0.12f, 0.16f), V3(1, 0.92f, 0.5f), clampf(1.4f - d / 12.f, 0, 1));
                    img[y * ICW + x] = ((uint32_t)(c.x * 255) << 16) | ((uint32_t)(c.y * 255) << 8) | (uint32_t)(c.z * 255);
                }
            continue;
        }
        Scene s;
        s.sky = skyPresets()[0];
        s.sky.clouds = 0;
        s.sky.haze = 0;
        Object o;
        o.type = type;
        o.mat = findMaterial("Default Gray");
        s.cam.target = {0, 0, 0};
        s.cam.dist = 4.6f; s.cam.pitch = 22; s.cam.yaw = 35; s.cam.fov = 40;
        if (type == ObjType::Terrain) {
            o.hf = std::make_shared<Heightfield>();
            hfInit(*o.hf, 65);
            hfFractal(*o.hf, 7, 0.5f, false);
            o.mat = findMaterial("Snowy Peaks");
            o.pos = {0, -0.4f, 0};
            s.cam.dist = 3.6f; s.cam.pitch = 30;
        } else if (type == ObjType::Ground) { o.mat = findMaterial("Earth"); s.cam.dist = 8; s.cam.pitch = 14; s.cam.target = {0, 1, 0}; }
        else if (type == ObjType::Water) { o.mat = findMaterial("Water"); s.cam.dist = 8; s.cam.pitch = 14; s.cam.target = {0, 1, 0}; }
        else if (type == ObjType::Cloud) {
            o.mat = findMaterial("Clouds");
            o.mat.texScale = 0.25f;
            o.pos = {0, 4, 0};
            s.cam.target = {0, 4, -6}; s.cam.dist = 8; s.cam.pitch = -30;
        }
        o.update();
        s.objs.push_back(o);
        renderImage(s, ICW, ICH, img.data(), true);
    }
    auto& sp = skyPresets();
    for (size_t i = 0; i < sp.size() && i < 16; i++) {
        Scene s;
        s.sky = sp[i];
        Object g = makeObject(ObjType::Ground, 0);
        s.objs.push_back(g);
        s.cam.target = {0, 3, 0}; s.cam.dist = 5; s.cam.pitch = -6; s.cam.yaw = sp[i].sunAz + 160; s.cam.fov = 70;
        gSkyThumbs[i].assign(STW * STH, 0);
        renderImage(s, STW, STH, gSkyThumbs[i].data(), false);
    }
}

// ---------------------------------------------------------------- window

LRESULT CALLBACK wndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE:
        if (LOWORD(lp) > 0 && HIWORD(lp) > 0) { resizeCanvas(LOWORD(lp), HIWORD(lp)); doFrame(); }
        return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO*)lp)->ptMinTrackSize = {1200, 760}; return 0;
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
        gIn.down = gIn.pressed = true;
        doFrame();
        return 0;
    case WM_LBUTTONUP:
        if (gModal) return 0;
        if (!gRDrag && !gMDrag) ReleaseCapture();
        gIn.down = false;
        gIn.released = true;
        doFrame();
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
    case WM_KEYDOWN: {
        if (gModal) return 0;
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (wp == VK_ESCAPE) {
            if (gLab != LabNone) closeLab(true);
            else if (gMainR.busy()) actRender();
            else gShowRender = false;
        } else if (gLab != LabNone) return 0;
        else if (wp == VK_DELETE) actDelete();
        else if (ctrl && wp == 'D') actDuplicate();
        else if (ctrl && wp == 'R') actRender();
        else if (ctrl && wp == 'S') actSave(false);
        else if (ctrl && wp == 'O') actOpen();
        else if (ctrl && wp == 'N') actNew();
        else if (wp >= '1' && wp <= '4') { gView = (int)(wp - '1'); gShowRender = false; }
        doFrame();
        return 0;
    }
    case WM_TIMER: tick(); return 0;
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

int headlessRender(const char* cmd) {
    char out[MAX_PATH] = "", scene[MAX_PATH] = "";
    int w = 800, h = 500;
    if (sscanf(cmd, "--render %259s %d %d %259s", out, &w, &h, scene) < 1 || w < 1 || h < 1) return 2;
    Scene s = makeDefaultScene();
    if (scene[0] && !s.load(scene)) return 3;
    if (scene[0] == 0) { s.cam.target = {0, 4, 0}; }
    std::vector<uint32_t> px((size_t)w * h);
    renderImage(s, w, h, px.data(), true);
    return saveBMP(out, w, h, px) ? 0 : 4;
}

}  // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int show) {
    if (cmd && std::strncmp(cmd, "--render", 8) == 0) return headlessRender(cmd);
    SetProcessDPIAware();
    gScene = makeDefaultScene();
    resetCounters();
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
    gWnd = CreateWindowExA(0, wc.lpszClassName, "Canyon 3D - Bryce-style landscape studio", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           1440, 900, nullptr, nullptr, inst, nullptr);
    if (!gWnd) return 1;
    ShowWindow(gWnd, show == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : show);
    SetTimer(gWnd, 1, 33, nullptr);
    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    return 0;
}
