// vlswm — minimal, fast Wayland compositor.
// v0.1: backend + renderer + allocator + scene-graph + output + seat + xdg windows.
#pragma once

#include "vlswm/wlroots.h"
#include "vlswm/xwayland.h"
#include "vlswm/animation.h"

#include <list>
#include <string>

struct IniConfig;
struct View;

struct Output {
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
    struct wlr_scene_node *background = nullptr;  // per-output wallpaper node
};

struct Server {
    struct IniConfig *config = nullptr;  // loaded INI config (may be null = defaults)

    struct wl_display *wl_display = nullptr;
    struct wlr_backend *backend = nullptr;
    struct wlr_renderer *renderer = nullptr;
    struct wlr_allocator *allocator = nullptr;

    struct wlr_compositor *compositor = nullptr;
    struct wlr_xdg_shell *xdg_shell = nullptr;
    struct wlr_layer_shell_v1 *layer_shell = nullptr;
    struct wlr_linux_dmabuf_v1 *linux_dmabuf = nullptr;

    struct wlr_xwayland *xwayland = nullptr;
    struct wlr_xdg_activation_v1 *xdg_activation = nullptr;

    struct wlr_scene *scene = nullptr;
    struct wlr_scene_tree *scene_shell = nullptr;   // toplevel windows
    struct wlr_scene_tree *scene_background = nullptr;
    struct wlr_scene_tree *scene_layers = nullptr;  // layer-shell surfaces (fuzzel, etc.)

    struct wlr_seat *seat = nullptr;
    struct wlr_cursor *cursor = nullptr;
    struct wlr_xcursor_manager *cursor_mgr = nullptr;

    struct wlr_output_layout *output_layout = nullptr;

    std::list<View *> views;
    std::list<Output *> outputs;

    // CPU copy of the wallpaper raster (BGRA little-endian ARGB8888, premultiplied),
    // used for rounded-corner masks: cut pixels are painted with the wallpaper
    // color that sits underneath the window corner.
    unsigned char *wp_pixels = nullptr;
    int wp_w = 0, wp_h = 0;
    int wp_rev = 0;  // bumped on every (re)load so corner masks refresh

    struct wlr_keyboard *keyboard = nullptr;
    struct wlr_input_device *pointer_device = nullptr;

    int current_workspace = 1;

    // Suppressed while switching workspaces so windows jump instead of gliding.
    bool suppress_anims = false;

    struct wl_listener new_output;
    struct wl_listener new_input;
    struct wl_listener new_xdg_surface;
    struct wl_listener new_layer_surface;
    struct wl_listener xwayland_ready;
    struct wl_listener xwayland_new_surface;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    struct wl_listener request_set_selection;
    struct wl_listener request_set_primary_selection;
    struct wl_listener request_start_drag;
    struct wl_listener request_activate;

    bool init(IniConfig *config);
    void run();
    void finish();

    View *focused_view();
    void focus_view(View *view, struct wlr_surface *surface);

    void arrange();               // recompute tiled layout
    void move_focus(int dx);      // +1 / -1 cycle tiled focus
    void swap_focus(int dx);      // SUPER+ALT+arrows: swap focused with neighbor
    void toggle_floating();       // SUPER+SPACE
    void toggle_fullscreen();     // SUPER+F
    void move_to_master();        // promote focused to master / cycle stack

    void switch_workspace(int id);      // jump/create a workspace
    void move_focused_to_workspace(int id); // move focused window to workspace
    void workspace_refresh();           // re-render all views on current workspace
    
    void reload_config();         // SUPER+R hot-reload config without restart

    // --- animations ---
    void animate(uint64_t now);   // advance all in-flight animations (per frame)
    void close_view(View *view);  // animated (or instant) window close

    bool animations_enabled() const;
    uint64_t animation_duration_ns() const;
    Easing easing() const;
};

Output *create_output(Server *server, struct wlr_output *wlr_output);
View *desktop_view_at(Server *server, double lx, double ly,
                      struct wlr_surface **surface, double *sx, double *sy);
struct wlr_scene_tree *scene_tree_for_surface(struct wlr_surface *surface);
