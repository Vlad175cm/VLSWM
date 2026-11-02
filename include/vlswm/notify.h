#pragma once

#include "vlswm/overlay.h"

#include <cstdint>
#include <string>
#include <vector>

#include <xkbcommon/xkbcommon.h>

struct Server;
struct sd_bus;
struct wl_event_source;

struct Note {
    uint32_t id = 0;
    std::string app;
    std::string summary;
    std::string body;
    uint64_t expires = 0;
};

struct Notifications {
    Server *server = nullptr;
    OverlayBuffer ov;
    std::vector<Note> history;
    std::vector<Note> cards;
    bool panel = false;

    sd_bus *bus = nullptr;
    wl_event_source *bus_source = nullptr;
    uint32_t next_id = 1;

    void init(Server *s) { server = s; }
    void dbus_init();
    void dbus_process();
    uint32_t push(const std::string &app, const std::string &summary,
                  const std::string &body, int timeout_ms, uint32_t replaces_id);
    void close_note(uint32_t id);
    void toggle_panel();
    void close_panel();
    void key(xkb_keysym_t sym, const std::string &utf8);
    void animate(uint64_t now);
    void render();
};
