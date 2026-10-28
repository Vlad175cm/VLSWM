#pragma once

#include "vlswm/wlroots.h"
#include "vlswm/animation.h"

#include <sys/types.h>

struct Server;

struct View {
    Server *server;
    struct wlr_xdg_surface *xdg_surface = nullptr;
    struct wlr_scene_tree *scene_tree = nullptr;

    bool mapped = false;
    bool closing = false;
    bool terminal = false;
    bool ghostty = false;
    pid_t pid = 0;
    bool initial_configured = false;
    FadeAnim fade;
    FadeAnim slide;
    float opacity = 1.0f;
    float slide_y = 0.0f;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;

    View(Server *server, struct wlr_xdg_surface *xdg_surface, struct wlr_scene_tree *scene_tree);

    struct wlr_surface *wlr_surface();
    void set_opacity(float o);
    void configure_size();

    static void xdg_map(wl_listener *listener, void *data);
    static void xdg_unmap(wl_listener *listener, void *data);
    static void xdg_destroy(wl_listener *listener, void *data);
    static void surface_commit(wl_listener *listener, void *data);
};
