#pragma once

#include "vlswm/animation.h"
#include "vlswm/overlay.h"

#include <cairo.h>
#include <pango/pango-font.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct Server;

struct DesktopApp {
    std::string name;
    std::string exec;
    std::string match;
    bool terminal = false;
};

struct Launcher {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool open = false;
    bool initialized = false;
    std::string input;
    FadeAnim fade;
    PangoFontDescription *font_desc = nullptr;
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
    void reload();
    void animate(uint64_t now);
    bool busy() const;
    void scan_apps();
    bool apps_changed();
    const DesktopApp *suggestion() const;
    void launch(const DesktopApp &app);
};
