#pragma once

#include "vlswm/overlay.h"

#include <string>
#include <vector>

#include <xkbcommon/xkbcommon.h>

struct Server;

struct Clipboard {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool open = false;
    std::string input;
    std::vector<std::string> items;
    std::vector<int> filtered;
    int selected = 0;

    void init(Server *s) { server = s; }
    void toggle();
    void close();
    void key(xkb_keysym_t sym, const std::string &utf8);
    void reload();
    void refilter();
    void choose();
    void render();
};
