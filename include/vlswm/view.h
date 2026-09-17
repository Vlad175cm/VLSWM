// vlswm — view implementation (a mapped xdg toplevel or Xwayland window).
#pragma once

#include "vlswm/wlroots.h"
#include "vlswm/animation.h"

struct Server;
struct wlr_xdg_surface;
struct wlr_xwayland_surface;

struct View {
    Server *server;
    struct wlr_xdg_surface *xdg_surface = nullptr;      // set for Wayland toplevels
    struct wlr_xwayland_surface *xsurface = nullptr;     // set for X11 windows
    struct wlr_scene_tree *scene_tree = nullptr;
    struct wlr_scene_surface *scene_surface = nullptr;   // rendered surface (xdg or xwayland)

    bool mapped = false;
    bool floating = false;
    bool fullscreen = false;
    int workspace = 1;

    // Current applied geometry — used as animation start point for the next arrange.
    int gx = 0, gy = 0, gw = 0, gh = 0;
    GeometryAnim geo;   // transition to new geometry
    FadeAnim fade;      // opacity transition
    bool opening = false;  // fade-in + slide-up in progress
    bool closing = false;  // fade-out in progress
    uint64_t opening_start = 0;  // when the open animation began (for the hold timeout)

    // Rounded-corner masks (children of scene_tree, drawn above the surface).
    struct wlr_scene_buffer *c_tl = nullptr, *c_tr = nullptr;
    struct wlr_scene_buffer *c_bl = nullptr, *c_br = nullptr;
    int c_x = -1, c_y = -1, c_w = -1, c_h = -1; // geometry cache
    int c_r = 0;                                  // radius cache
    int c_wp_rev = -1;                            // wallpaper revision cache

    // Configure throttling: last (x, y, w, h) we told the client (-1 = none).
    int cfg_x = -1, cfg_y = -1, cfg_w = -1, cfg_h = -1;

    // Wayland (xdg) surface listeners.
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener commit;
    bool initial_configured = false;

    // Xwayland listeners (map/unmap via associate/dissociate).
    struct wl_listener associate;
    struct wl_listener dissociate;
    struct wl_listener x_destroy;

    View(Server *server, struct wlr_xdg_surface *xdg_surface,
         struct wlr_scene_tree *scene_tree);
    View(Server *server, struct wlr_xwayland_surface *xsurface,
         struct wlr_scene_tree *scene_tree);

    struct wlr_box surface_geo();
    struct wlr_surface *wlr_surface();   // the backing wl surface (or nullptr)

    // Find the wlr_scene_buffer that displays this view's pixels.
    // Returns nullptr if no buffer is available (pre-map).
    struct wlr_scene_buffer *scene_buffer();

    static void xdg_map(wl_listener *listener, void *data);
    static void xdg_unmap(wl_listener *listener, void *data);
    static void xdg_destroy(wl_listener *listener, void *data);
    static void xdg_commit(wl_listener *listener, void *data);

    static void xwayland_associate(wl_listener *listener, void *data);
    static void xwayland_dissociate(wl_listener *listener, void *data);
    static void xwayland_destroy(wl_listener *listener, void *data);
};

// Apply a geometry to a view immediately (position + size configure).
void view_apply_box(View *view, int x, int y, int w, int h);
