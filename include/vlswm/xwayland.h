// vlswm — C++-safe bridge over wlroots' Xwayland headers.
//
// wlroots' xwayland headers use `class`/`instance` as struct field names,
// which are reserved words in C++ (and their transitive includes drag in
// <cmath>/<type_traits> which use `enum class`). So we keep those headers out
// of C++ and expose only opaque types, a handful of re-declared C functions,
// and accessor helpers implemented in C (src/xwayland.c).
#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wl_display;
struct wl_client;
struct wl_global;
struct wl_signal;
struct wl_listener;
struct wlr_compositor;
struct wlr_seat;
struct wlr_surface;

// Opaque wlroots Xwayland types.
struct wlr_xwayland;
struct wlr_xwayland_surface;
struct wlr_xwayland_shell_v1;

// --- functions (re-declared so we can link without the C++-hostile headers) ---
struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
    struct wlr_compositor *compositor, bool lazy);
void wlr_xwayland_set_seat(struct wlr_xwayland *xwayland, struct wlr_seat *seat);
void wlr_xwayland_surface_configure(struct wlr_xwayland_surface *surface,
    int16_t x, int16_t y, uint16_t width, uint16_t height);
void wlr_xwayland_surface_activate(struct wlr_xwayland_surface *surface, bool activated);
void wlr_xwayland_surface_close(struct wlr_xwayland_surface *surface);

struct wlr_xwayland_shell_v1 *wlr_xwayland_shell_v1_create(
    struct wl_display *display, uint32_t version);
void wlr_xwayland_shell_v1_set_client(struct wlr_xwayland_shell_v1 *shell,
    struct wl_client *client);

// --- accessors (implemented in src/xwayland.c) ---
struct wl_signal *vlswm_xwayland_signal_ready(struct wlr_xwayland *xwayland);
struct wl_signal *vlswm_xwayland_signal_new_surface(struct wlr_xwayland *xwayland);
const char *vlswm_xwayland_display_name(struct wlr_xwayland *xwayland);

struct wl_signal *vlswm_xsurface_signal_associate(struct wlr_xwayland_surface *s);
struct wl_signal *vlswm_xsurface_signal_dissociate(struct wlr_xwayland_surface *s);
struct wl_signal *vlswm_xsurface_signal_destroy(struct wlr_xwayland_surface *s);
struct wlr_surface *vlswm_xsurface_surface(struct wlr_xwayland_surface *s);
void vlswm_xsurface_geometry(struct wlr_xwayland_surface *s,
    int16_t *x, int16_t *y, uint16_t *w, uint16_t *h);
bool vlswm_xsurface_override_redirect(struct wlr_xwayland_surface *s);
uint32_t vlswm_xsurface_window_id(struct wlr_xwayland_surface *s);

#ifdef __cplusplus
}
#endif

