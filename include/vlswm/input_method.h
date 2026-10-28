#pragma once

#include "vlswm/wlroots.h"

struct Server;

struct TextInput {
    Server *server;
    struct wlr_text_input_v3 *input;
    struct wl_listener enable;
    struct wl_listener commit;
    struct wl_listener disable;
    struct wl_listener destroy;
};

struct InputPopup {
    Server *server;
    struct wlr_input_popup_surface_v2 *popup;
    struct wlr_scene_tree *scene;
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
};

void input_method_init(Server *server);
void input_method_finish(Server *server);
void input_method_set_focus(Server *server, struct wlr_surface *surface);
