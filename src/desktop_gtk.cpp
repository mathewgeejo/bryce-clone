#ifdef __linux__

#include "desktop.h"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <utility>

namespace {

GtkWidget* window;
GtkWidget* area;
cairo_surface_t* surface;
cairo_t* canvas;
PangoLayout* layout;
PangoFontDescription* fonts[4];
GdkCursor* resizeCursor;
bool resizeCursorShown;
bool pixelsDirty;
desktop::Modifiers keys;
const char* appTitle;

void syncPixels() {
    if (!pixelsDirty) return;
    cairo_surface_mark_dirty(surface);
    pixelsDirty = false;
}

void releaseCanvas() {
    if (layout) g_object_unref(layout);
    if (canvas) cairo_destroy(canvas);
    if (surface) cairo_surface_destroy(surface);
    layout = nullptr;
    canvas = nullptr;
    surface = nullptr;
    pixelsDirty = false;
}

void source(desktop::Color c) {
    syncPixels();
    cairo_set_source_rgb(canvas, desktop::red(c) / 255.0, desktop::green(c) / 255.0, desktop::blue(c) / 255.0);
}

void updateKeys(guint state) {
    keys = {bool(state & GDK_SHIFT_MASK), bool(state & GDK_CONTROL_MASK), bool(state & GDK_MOD1_MASK)};
}

gboolean draw(GtkWidget*, cairo_t* cr, gpointer) {
    if (surface) {
        syncPixels();
        cairo_set_source_surface(cr, surface, 0, 0);
        cairo_paint(cr);
    }
    return TRUE;
}

void allocated(GtkWidget*, GtkAllocation* allocation, gpointer) {
    if (surface && cairo_image_surface_get_width(surface) == allocation->width &&
        cairo_image_surface_get_height(surface) == allocation->height) return;
    studio::resized(allocation->width, allocation->height);
}

gboolean motion(GtkWidget*, GdkEventMotion* event, gpointer) {
    updateKeys(event->state);
    studio::pointerMoved((int)event->x, (int)event->y);
    return TRUE;
}

gboolean button(GtkWidget*, GdkEventButton* event, gpointer) {
    using desktop::Mouse;
    if (event->type != GDK_BUTTON_PRESS && event->type != GDK_BUTTON_RELEASE) return TRUE;
    if (event->button < GDK_BUTTON_PRIMARY || event->button > GDK_BUTTON_SECONDARY) return FALSE;
    updateKeys(event->state);
    Mouse mouse = event->button == GDK_BUTTON_PRIMARY ? Mouse::Left :
                  (event->button == GDK_BUTTON_MIDDLE ? Mouse::Middle : Mouse::Right);
    studio::pointerButton(mouse, event->type == GDK_BUTTON_PRESS, (int)event->x, (int)event->y);
    return TRUE;
}

gboolean scroll(GtkWidget*, GdkEventScroll* event, gpointer) {
    updateKeys(event->state);
    double dx, dy;
    double steps = 0;
    if (gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent*>(event), &dx, &dy)) steps = -dy;
    else if (event->direction == GDK_SCROLL_UP) steps = 1;
    else if (event->direction == GDK_SCROLL_DOWN) steps = -1;
    if (steps != 0) studio::pointerWheel((float)steps, (int)event->x, (int)event->y);
    return TRUE;
}

int keyCode(guint key) {
    using desktop::KeyCode;
    switch (key) {
    case GDK_KEY_BackSpace: return KeyCode::Backspace;
    case GDK_KEY_Return: case GDK_KEY_KP_Enter: return KeyCode::Enter;
    case GDK_KEY_Escape: return KeyCode::Escape;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete: return KeyCode::Delete;
    case GDK_KEY_bracketleft: case GDK_KEY_braceleft: return KeyCode::LeftBracket;
    case GDK_KEY_bracketright: case GDK_KEY_braceright: return KeyCode::RightBracket;
    default:
        gunichar c = gdk_keyval_to_unicode(gdk_keyval_to_upper(key));
        return c == ' ' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ? (int)c : 0;
    }
}

gboolean keyEvent(GtkWidget*, GdkEventKey* event, gpointer) {
    updateKeys(event->state);
    bool pressed = event->type == GDK_KEY_PRESS;
    switch (event->keyval) {
    case GDK_KEY_Shift_L: case GDK_KEY_Shift_R: keys.shift = pressed; break;
    case GDK_KEY_Control_L: case GDK_KEY_Control_R: keys.ctrl = pressed; break;
    case GDK_KEY_Alt_L: case GDK_KEY_Alt_R: keys.alt = pressed; break;
    }
    if (pressed) {
        if (int key = keyCode(event->keyval)) studio::keyDown(key);
        gunichar c = gdk_keyval_to_unicode(event->keyval);
        if (!keys.ctrl && !keys.alt && g_unichar_isprint(c)) {
            char utf8[6];
            int bytes = g_unichar_to_utf8(c, utf8);
            studio::textInput({utf8, (size_t)bytes});
        }
    }
    return TRUE;
}

gboolean closing(GtkWidget*, GdkEvent*, gpointer) { return !studio::shouldClose(); }
void destroyed(GtkWidget*, gpointer) { gtk_main_quit(); }
gboolean timer(gpointer) { studio::timer(); return G_SOURCE_CONTINUE; }

}  // namespace

namespace desktop {

int run(const char* title) {
    if (!gtk_init_check(nullptr, nullptr)) {
        std::fputs("Could not open a graphical display. Use --help for headless commands.\n", stderr);
        return 1;
    }
    appTitle = title;
    studio::initialize();
    int size[] = {15, 13, 15, 19};
    PangoWeight weight[] = {PANGO_WEIGHT_NORMAL, PANGO_WEIGHT_NORMAL, PANGO_WEIGHT_SEMIBOLD, PANGO_WEIGHT_BOLD};
    for (int i = 0; i < 4; ++i) {
        fonts[i] = pango_font_description_new();
        pango_font_description_set_family(fonts[i], "Sans");
        pango_font_description_set_absolute_size(fonts[i], size[i] * PANGO_SCALE);
        pango_font_description_set_weight(fonts[i], weight[i]);
    }

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 760);
    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, 1180, 680);
    gtk_widget_set_can_focus(area, TRUE);
    gtk_widget_add_events(area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                               GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_container_add(GTK_CONTAINER(window), area);
    g_signal_connect(area, "size-allocate", G_CALLBACK(allocated), nullptr);
    g_signal_connect(area, "draw", G_CALLBACK(draw), nullptr);
    g_signal_connect(area, "motion-notify-event", G_CALLBACK(motion), nullptr);
    g_signal_connect(area, "button-press-event", G_CALLBACK(button), nullptr);
    g_signal_connect(area, "button-release-event", G_CALLBACK(button), nullptr);
    g_signal_connect(area, "scroll-event", G_CALLBACK(scroll), nullptr);
    g_signal_connect(window, "key-press-event", G_CALLBACK(keyEvent), nullptr);
    g_signal_connect(window, "key-release-event", G_CALLBACK(keyEvent), nullptr);
    g_signal_connect(window, "delete-event", G_CALLBACK(closing), nullptr);
    g_signal_connect(window, "destroy", G_CALLBACK(destroyed), nullptr);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(area);
    guint timerId = g_timeout_add(33, timer, nullptr);
    gtk_main();
    g_source_remove(timerId);
    releaseCanvas();
    for (auto* font : fonts) pango_font_description_free(font);
    if (resizeCursor) g_object_unref(resizeCursor);
    return 0;
}

std::string libraryPath() {
    std::filesystem::path config = std::filesystem::path(g_get_user_config_dir()) / "two-hoodoos-and-a-floppy";
    g_mkdir_with_parents(config.c_str(), 0700);
    return (config / "canyon3d_library.txt").string();
}

uint32_t* resizeCanvas(int w, int h) {
    cairo_surface_t* next = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    if (cairo_surface_status(next) != CAIRO_STATUS_SUCCESS ||
        cairo_image_surface_get_stride(next) != (int64_t)w * sizeof(uint32_t)) {
        cairo_surface_destroy(next);
        return nullptr;
    }
    cairo_t* context = cairo_create(next);
    if (cairo_status(context) != CAIRO_STATUS_SUCCESS) {
        cairo_destroy(context);
        cairo_surface_destroy(next);
        return nullptr;
    }
    releaseCanvas();
    surface = next;
    canvas = context;
    layout = pango_cairo_create_layout(canvas);
    cairo_set_line_width(canvas, 1);
    cairo_set_antialias(canvas, CAIRO_ANTIALIAS_NONE);
    return reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(surface));
}

void beginPixelWrite() {
    if (pixelsDirty) return;
    cairo_surface_flush(surface);
    pixelsDirty = true;
}

void present() { gtk_widget_queue_draw(area); }

void fill(Rect r, Color c) {
    if (r.w <= 0 || r.h <= 0) return;
    source(c);
    cairo_rectangle(canvas, r.x, r.y, r.w, r.h);
    cairo_fill(canvas);
}

void line(int x0, int y0, int x1, int y1, Color c) {
    source(c);
    cairo_move_to(canvas, x0 + 0.5, y0 + 0.5);
    cairo_line_to(canvas, x1 + 0.5, y1 + 0.5);
    cairo_stroke(canvas);
}

void circle(int x, int y, int radius, Color c) {
    source(c);
    cairo_arc(canvas, x + 0.5, y + 0.5, radius, 0, 6.283185307179586);
    cairo_stroke(canvas);
}

void setClip(Rect r) {
    cairo_rectangle(canvas, r.x, r.y, r.w, r.h);
    cairo_clip(canvas);
}

void clearClip() { cairo_reset_clip(canvas); }

void textIn(Rect r, const char* text, Color c, Font font, unsigned flags) {
    if (r.w <= 0 || r.h <= 0) return;
    bool wrap = flags & TextWrap;
    pango_layout_set_font_description(layout, fonts[(int)font]);
    gchar* repaired = g_utf8_validate(text, -1, nullptr) ? nullptr : g_utf8_make_valid(text, -1);
    pango_layout_set_text(layout, repaired ? repaired : text, -1);
    g_free(repaired);
    pango_layout_set_alignment(layout, flags & TextLeft ? PANGO_ALIGN_LEFT : (flags & TextRight ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_CENTER));
    pango_layout_set_single_paragraph_mode(layout, !wrap);
    pango_layout_set_width(layout, r.w * PANGO_SCALE);
    pango_layout_set_height(layout, wrap ? r.h * PANGO_SCALE : -1);
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
    int h = 0;
    if (flags & TextMiddle) pango_layout_get_pixel_size(layout, nullptr, &h);
    source(c);
    cairo_save(canvas);
    cairo_rectangle(canvas, r.x, r.y, r.w, r.h);
    cairo_clip(canvas);
    cairo_move_to(canvas, r.x, r.y + (flags & TextMiddle ? (r.h - h) / 2 : 0));
    pango_cairo_show_layout(canvas, layout);
    cairo_restore(canvas);
}

Modifiers modifiers() { return keys; }

void capturePointer(bool) {
    // GDK's implicit button grab delivers drag events until the button is released.
}

void showResizeCursor(bool resize) {
    auto* drawable = gtk_widget_get_window(area);
    if (!drawable || resize == resizeCursorShown) return;
    if (resize && !resizeCursor)
        resizeCursor = gdk_cursor_new_from_name(gtk_widget_get_display(area), "ew-resize");
    gdk_window_set_cursor(drawable, resize ? resizeCursor : nullptr);
    resizeCursorShown = resize;
}

bool chooseColor(Color& color) {
    GtkWidget* dialog = gtk_color_chooser_dialog_new("Choose Color", GTK_WINDOW(window));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    GtkColorChooser* chooser = GTK_COLOR_CHOOSER(dialog);
    GdkRGBA rgba{red(color) / 255.0, green(color) / 255.0, blue(color) / 255.0, 1};
    gtk_color_chooser_set_use_alpha(chooser, FALSE);
    gtk_color_chooser_set_rgba(chooser, &rgba);
    bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK;
    if (accepted) {
        gtk_color_chooser_get_rgba(chooser, &rgba);
        color = rgb(std::lround(rgba.red * 255), std::lround(rgba.green * 255), std::lround(rgba.blue * 255));
    }
    gtk_widget_destroy(dialog);
    return accepted;
}

bool confirm(const char* message) {
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                               "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), appTitle);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_NO);
    bool yes = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_YES;
    gtk_widget_destroy(dialog);
    return yes;
}

bool chooseFile(bool save, const char* filters, const char* extension, std::string& out) {
    GtkFileChooserNative* dialog = gtk_file_chooser_native_new(save ? "Save File" : "Open File", GTK_WINDOW(window),
        save ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN, save ? "_Save" : "_Open", "_Cancel");
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);
    gtk_native_dialog_set_modal(GTK_NATIVE_DIALOG(dialog), TRUE);
    for (const char* name = filters; *name;) {
        const char* pattern = name + std::strlen(name) + 1;
        GtkFileFilter* filter = gtk_file_filter_new();
        gtk_file_filter_set_name(filter, name);
        gtk_file_filter_add_pattern(filter, !std::strcmp(pattern, "*.*") ? "*" : pattern);
        gtk_file_chooser_add_filter(chooser, filter);
        name = pattern + std::strlen(pattern) + 1;
    }
    if (save) {
        std::string suggestion = std::string("Untitled.") + extension;
        gtk_file_chooser_set_current_name(chooser, suggestion.c_str());
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
    }
    bool accepted = gtk_native_dialog_run(GTK_NATIVE_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
    char* selected = accepted ? gtk_file_chooser_get_filename(chooser) : nullptr;
    g_object_unref(dialog);
    if (!selected) return false;
    std::string path = selected;
    g_free(selected);
    if (save && std::filesystem::path(path).extension().empty()) {
        path += std::string(".") + extension;
        if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS) || g_file_test(path.c_str(), G_FILE_TEST_IS_SYMLINK)) {
            std::string prompt = "Replace the existing file " + std::filesystem::path(path).filename().string() + "?";
            if (!confirm(prompt.c_str())) return false;
        }
    }
    out = std::move(path);
    return true;
}

}  // namespace desktop

int main(int argc, char** argv) { return studio::entry(argc, argv); }
#endif
