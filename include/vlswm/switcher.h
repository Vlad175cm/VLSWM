#pragma once

#include "vlswm/overlay.h"

#include <xkbcommon/xkbcommon.h>

#include <vector>

struct Server;
struct View;

struct Switcher {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool open = false;
    int selected = 0;
    std::vector<View *> entries;

    void init(Server *s) { server = s; }
    void reload();
    void toggle();
    void close();
    void choose();
    void key(xkb_keysym_t sym);
    void render();
};
