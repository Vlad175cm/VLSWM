#pragma once

#include "vlswm/overlay.h"

#include <cstdint>

struct Server;

struct Rec {
    Server *server = nullptr;
    OverlayBuffer ov;
    bool visible = false;
    bool filled = false;
    uint64_t hide_at = 0;

    void init(Server *s) { server = s; }
    void flash(bool start);
    void animate(uint64_t now);
    void render();
};
