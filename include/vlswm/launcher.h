#pragma once

#include "vlswm/animation.h"

#include <cairo.h>
#include <pango/pango-font.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct Server;
struct wlr_scene_buffer;
struct PixBuf;

struct DesktopApp {
    std::string name;
    std::string exec;
    std::string match;
    bool terminal = false;
};

struct Launcher {
    Server *server = nullptr;
    struct wlr_scene_buffer *buf = nullptr;
    bool open = false;
    std::string input;
    std::string error;
    uint64_t error_until = 0;
    FadeAnim fade;
    struct PixBuf *pix = nullptr;
    cairo_surface_t *surf = nullptr;
    PangoFontDescription *font_desc = nullptr;
    int width = 0;
    int height = 0;
    std::vector<DesktopApp> apps;
    std::vector<std::string> app_dirs;
    std::vector<time_t> app_mtimes;

    ~Launcher();
    void init(Server *server);
    void toggle();
    void close();
    void key(xkb_keysym_t sym, const std::string &utf8);
    void render();
    void relayout();
    void animate(uint64_t now);
    bool busy() const;
    void scan_apps();
    bool apps_changed();
    bool launch(const std::string &text);
};
