#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>

#include "desktop.h"

#include <algorithm>
#include <cstdlib>

namespace {

HINSTANCE instance;
int showMode;
HWND window;
HDC canvas;
HBITMAP bitmap;
HFONT fonts[4];
int width, height;
desktop::Color penColor = UINT32_MAX;
const char* appTitle;

COLORREF nativeColor(desktop::Color c) { return RGB(desktop::red(c), desktop::green(c), desktop::blue(c)); }

void setPen(desktop::Color c) {
    if (penColor == c) return;
    SetDCPenColor(canvas, nativeColor(c));
    penColor = c;
}

HFONT makeFont(int pixels, int weight) {
    return CreateFontA(-pixels, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
}

int keyCode(WPARAM key) {
    switch (key) {
    case VK_BACK: return desktop::KeyCode::Backspace;
    case VK_RETURN: return desktop::KeyCode::Enter;
    case VK_ESCAPE: return desktop::KeyCode::Escape;
    case VK_DELETE: return desktop::KeyCode::Delete;
    case VK_OEM_4: return desktop::KeyCode::LeftBracket;
    case VK_OEM_6: return desktop::KeyCode::RightBracket;
    case VK_F1: return desktop::KeyCode::Help;
    default: return key == VK_SPACE || (key >= '0' && key <= '9') || (key >= 'A' && key <= 'Z') ? (int)key : 0;
    }
}

LRESULT CALLBACK windowProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCCREATE:
        window = hw;
        break;
    case WM_SIZE:
        studio::resized(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO:
        reinterpret_cast<MINMAXINFO*>(lp)->ptMinTrackSize = {1220, 780};
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw, &ps);
        if (bitmap) BitBlt(dc, 0, 0, width, height, canvas, 0, 0, SRCCOPY);
        EndPaint(hw, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
        studio::pointerMoved(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        studio::pointerButton(desktop::Mouse::Left, msg == WM_LBUTTONDOWN, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        studio::pointerButton(desktop::Mouse::Middle, msg == WM_MBUTTONDOWN, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        studio::pointerButton(desktop::Mouse::Right, msg == WM_RBUTTONDOWN, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        return 0;
    case WM_MOUSEWHEEL: {
        POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hw, &pt);
        studio::pointerWheel(GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA, pt.x, pt.y);
        return 0;
    }
    case WM_CHAR:
        if (wp >= 32 && wp < 256) {
            char ch = (char)wp;
            studio::textInput({&ch, 1});
        }
        return 0;
    case WM_KEYDOWN:
        if (int key = keyCode(wp)) studio::keyDown(key);
        return 0;
    case WM_TIMER:
        studio::timer();
        return 0;
    case WM_CLOSE:
        if (studio::shouldClose()) DestroyWindow(hw);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hw, msg, wp, lp);
}

}  // namespace

namespace desktop {

int run(const char* title) {
    SetProcessDPIAware();
    appTitle = title;
    studio::initialize();
    fonts[0] = makeFont(15, FW_NORMAL);
    fonts[1] = makeFont(13, FW_NORMAL);
    fonts[2] = makeFont(15, FW_SEMIBOLD);
    fonts[3] = makeFont(19, FW_BOLD);
    WNDCLASSA wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = "BrycescapeWnd";
    if (!RegisterClassA(&wc)) return 1;
    RECT wa{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int w = std::min<int>(1440, wa.right - wa.left), h = std::min<int>(900, wa.bottom - wa.top);
    window = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - w) / 2,
                             wa.top + (wa.bottom - wa.top - h) / 2, w, h, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;
    int show = showMode;
    if (show == SW_SHOWMINIMIZED) show = SW_SHOWNORMAL;
    else if ((w < 1440 || h < 900) && (show == SW_SHOWNORMAL || show == SW_SHOWDEFAULT)) show = SW_SHOWMAXIMIZED;
    ShowWindow(window, show);
    SetTimer(window, 1, 33, nullptr);
    MSG msg;
    int result;
    while ((result = GetMessageA(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (canvas) DeleteDC(canvas);
    if (bitmap) DeleteObject(bitmap);
    for (auto font : fonts) if (font) DeleteObject(font);
    return result < 0 ? 1 : 0;
}

std::string libraryPath() {
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string path = exe;
    return path.substr(0, path.find_last_of("\\/") + 1) + "canyon3d_library.txt";
}

uint32_t* resizeCanvas(int w, int h) {
    if (!canvas) {
        canvas = CreateCompatibleDC(nullptr);
        if (!canvas) return nullptr;
        SelectObject(canvas, GetStockObject(DC_PEN));
        SetBkMode(canvas, TRANSPARENT);
    }
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP next = CreateDIBSection(canvas, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!next) return nullptr;
    SelectObject(canvas, next);
    if (bitmap) DeleteObject(bitmap);
    bitmap = next;
    width = w;
    height = h;
    return static_cast<uint32_t*>(pixels);
}

void beginPixelWrite() { GdiFlush(); }
void present() { if (window) InvalidateRect(window, nullptr, FALSE); }

void fill(Rect r, Color c) {
    SetDCBrushColor(canvas, nativeColor(c));
    RECT region{r.x, r.y, r.x + r.w, r.y + r.h};
    FillRect(canvas, &region, (HBRUSH)GetStockObject(DC_BRUSH));
}

void line(int x0, int y0, int x1, int y1, Color c) {
    setPen(c);
    MoveToEx(canvas, x0, y0, nullptr);
    LineTo(canvas, x1, y1);
}

void circle(int x, int y, int radius, Color c) {
    auto previous = SelectObject(canvas, GetStockObject(HOLLOW_BRUSH));
    setPen(c);
    Ellipse(canvas, x - radius, y - radius, x + radius, y + radius);
    SelectObject(canvas, previous);
}

void setClip(Rect r) { IntersectClipRect(canvas, r.x, r.y, r.x + r.w, r.y + r.h); }
void clearClip() { SelectClipRgn(canvas, nullptr); }

void textIn(Rect r, const char* text, Color c, Font font, unsigned flags) {
    if (r.w <= 0 || r.h <= 0) return;
    SelectObject(canvas, fonts[(int)font]);
    SetTextColor(canvas, nativeColor(c));
    RECT region{r.x, r.y, r.x + r.w, r.y + r.h};
    UINT align = flags & TextLeft ? DT_LEFT : (flags & TextRight ? DT_RIGHT : DT_CENTER);
    UINT mode = flags & TextWrap ? DT_WORDBREAK : DT_SINGLELINE;
    if (flags & TextMiddle) mode |= DT_VCENTER;
    DrawTextA(canvas, text, -1, &region, align | mode | DT_NOPREFIX | DT_END_ELLIPSIS);
}

Modifiers modifiers() { return {GetKeyState(VK_SHIFT) < 0, GetKeyState(VK_CONTROL) < 0, GetKeyState(VK_MENU) < 0}; }

void capturePointer(bool capture) {
    if (capture && GetCapture() != window) SetCapture(window);
    else if (!capture && GetCapture() == window) ReleaseCapture();
}

void showResizeCursor(bool resize) {
    static HCURSOR sizing = LoadCursor(nullptr, IDC_SIZEWE);
    if (resize) SetCursor(sizing);
    else if (GetCursor() == sizing) SetCursor(LoadCursor(nullptr, IDC_ARROW));
}

bool chooseColor(Color& color) {
    static COLORREF custom[16];
    CHOOSECOLORA cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = window;
    cc.rgbResult = nativeColor(color);
    cc.lpCustColors = custom;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorA(&cc)) return false;
    color = rgb(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
    return true;
}

bool confirm(const char* message) { return MessageBoxA(window, message, appTitle, MB_YESNO | MB_ICONQUESTION) == IDYES; }

bool chooseFile(bool save, const char* filters, const char* extension, std::string& out) {
    char path[MAX_PATH] = "";
    OPENFILENAMEA dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = filters;
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = extension;
    dialog.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!(save ? GetSaveFileNameA(&dialog) : GetOpenFileNameA(&dialog))) return false;
    out = path;
    return true;
}

}  // namespace desktop

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    instance = inst;
    showMode = show;
    return studio::entry(__argc, __argv);
}
#endif
