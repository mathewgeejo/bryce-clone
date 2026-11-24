#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace desktop {

struct Rect {
    int x, y, w, h;
    bool in(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

using Color = uint32_t;
constexpr Color rgb(unsigned r, unsigned g, unsigned b) { return (r << 16) | (g << 8) | b; }
constexpr uint8_t red(Color c) { return (c >> 16) & 255; }
constexpr uint8_t green(Color c) { return (c >> 8) & 255; }
constexpr uint8_t blue(Color c) { return c & 255; }

enum class Font { Regular, Small, Bold, Title };
enum TextFlags { TextCenter = 0, TextLeft = 1, TextRight = 2, TextMiddle = 4, TextWrap = 8 };
enum class Mouse { Left, Middle, Right };
enum KeyCode { Backspace = 8, Enter = 13, Escape = 27, Delete = 127, LeftBracket = '[', RightBracket = ']' };
struct Modifiers { bool shift = false, ctrl = false, alt = false; };

int run(const char* title);
std::string libraryPath();
uint32_t* resizeCanvas(int width, int height);
void beginPixelWrite();
void present();
void fill(Rect r, Color c);
void line(int x0, int y0, int x1, int y1, Color c);
void circle(int x, int y, int radius, Color c);
void setClip(Rect r);
void clearClip();
void textIn(Rect r, const char* text, Color c, Font font, unsigned flags = TextMiddle);
Modifiers modifiers();
void capturePointer(bool capture);
void showResizeCursor(bool resize);
bool chooseColor(Color& color);
bool confirm(const char* message);
bool chooseFile(bool save, const char* filters, const char* extension, std::string& out);

}  // namespace desktop

namespace studio {

int entry(int argc, char** argv);
void initialize();
void resized(int width, int height);
void timer();
void pointerMoved(int x, int y);
void pointerButton(desktop::Mouse button, bool down, int x, int y);
void pointerWheel(float steps, int x, int y);
void keyDown(int key);
void textInput(std::string_view text);
bool shouldClose();

}  // namespace studio
