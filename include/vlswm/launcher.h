#pragma once

#include "vlswm/animation.h"

#include <xkbcommon/xkbcommon.h>

#include <string>

struct Server;
struct wlr_scene_buffer;

struct Launcher {
    Server *server = nullptr;
    struct wlr_scene_buffer *buf = nullptr;
    bool open = false;
    std::string input;
    FadeAnim fade;

    void init(Server *server);
    void toggle();
    void close();
    void key(xkb_keysym_t sym, const std::string &utf8);
    void render();
    void relayout();
    void animate(uint64_t now);
};
