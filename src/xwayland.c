
#include <wlr/xwayland/xwayland.h>
#include <wlr/xwayland/server.h>
#include <wlr/xwayland/shell.h>

#include "vlswm/xwayland.h"

struct wl_signal *vlswm_xwayland_signal_ready(struct wlr_xwayland *xwayland) {
    return &xwayland->events.ready;
}

struct wl_signal *vlswm_xwayland_signal_new_surface(struct wlr_xwayland *xwayland) {
    return &xwayland->events.new_surface;
}

const char *vlswm_xwayland_display_name(struct wlr_xwayland *xwayland) {
    return xwayland->display_name;
}

struct wl_client *vlswm_xwayland_server_client(struct wlr_xwayland *xwayland) {
    return xwayland->server->client;
}

struct wl_signal *vlswm_xsurface_signal_associate(struct wlr_xwayland_surface *s) {
    return &s->events.associate;
}

struct wl_signal *vlswm_xsurface_signal_dissociate(struct wlr_xwayland_surface *s) {
    return &s->events.dissociate;
}

struct wl_signal *vlswm_xsurface_signal_destroy(struct wlr_xwayland_surface *s) {
    return &s->events.destroy;
}

struct wlr_surface *vlswm_xsurface_surface(struct wlr_xwayland_surface *s) {
    return s->surface;
}

void vlswm_xsurface_geometry(struct wlr_xwayland_surface *s,
    int16_t *x, int16_t *y, uint16_t *w, uint16_t *h) {
    *x = s->x;
    *y = s->y;
    *w = s->width;
    *h = s->height;
}

bool vlswm_xsurface_override_redirect(struct wlr_xwayland_surface *s) {
    return s->override_redirect;
}

uint32_t vlswm_xsurface_window_id(struct wlr_xwayland_surface *s) {
    return s->window_id;
}
