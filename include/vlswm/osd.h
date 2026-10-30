#pragma once

#include "vlswm/overlay.h"

#include <cstdint>

struct Server;

struct Osd {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool visible = false;
    uint64_t hide_at = 0;
    double value = 0.0;

    void init(Server *s) { server = s; }
    void show(double fraction);
    void animate(uint64_t now);
    void render();
};
