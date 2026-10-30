#pragma once

#include "vlswm/overlay.h"

#include <cstdint>
#include <string>

struct Server;

struct Osd {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool visible = false;
    uint64_t hide_at = 0;
    std::string label;
    double value = 0.0;

    void init(Server *s) { server = s; }
    void show(const std::string &text, double fraction);
    void animate(uint64_t now);
    void render();
};
