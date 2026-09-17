
#include "vlswm/view.h"
#include "vlswm/server.h"
#include "vlswm/xwayland.h"
#include "vlswm/corners.h"

#include <cmath>





static void wire_xdg(View *view, struct wlr_xdg_surface *xdg_surface) {
    view->map.notify = View::xdg_map;
    view->unmap.notify = View::xdg_unmap;
    view->destroy.notify = View::xdg_destroy;
    view->commit.notify = View::xdg_commit;

    struct wlr_surface *surface = xdg_surface->surface;
    wl_signal_add(&surface->events.map, &view->map);
    wl_signal_add(&surface->events.unmap, &view->unmap);
    wl_signal_add(&surface->events.commit, &view->commit);
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
}

static void wire_xwayland(View *view, struct wlr_xwayland_surface *xsurface) {
    view->associate.notify = View::xwayland_associate;
    view->dissociate.notify = View::xwayland_dissociate;
    view->x_destroy.notify = View::xwayland_destroy;

    wl_signal_add(vlswm_xsurface_signal_associate(xsurface), &view->associate);
    wl_signal_add(vlswm_xsurface_signal_dissociate(xsurface), &view->dissociate);
    wl_signal_add(vlswm_xsurface_signal_destroy(xsurface), &view->x_destroy);
}

View::View(Server *server, struct wlr_xdg_surface *xdg_surface,
           struct wlr_scene_tree *scene_tree)
    : server(server), xdg_surface(xdg_surface), scene_tree(scene_tree) {
    wire_xdg(this, xdg_surface);
}

View::View(Server *server, struct wlr_xwayland_surface *xsurface,
           struct wlr_scene_tree *scene_tree)
    : server(server), xsurface(xsurface), scene_tree(scene_tree) {
    wire_xwayland(this, xsurface);
}

struct wlr_surface *View::wlr_surface() {
    if (xdg_surface) return xdg_surface->surface;
    if (xsurface) return vlswm_xsurface_surface(xsurface);
    return nullptr;
}

struct wlr_box View::surface_geo() {
    struct wlr_box geo = {0, 0, 0, 0};
    if (xdg_surface) {
        geo = xdg_surface->geometry;
    } else if (xsurface) {
        int16_t x, y; uint16_t w, h;
        vlswm_xsurface_geometry(xsurface, &x, &y, &w, &h);
        geo.x = x;
        geo.y = y;
        geo.width = w;
        geo.height = h;
    }
    return geo;
}





static struct wlr_scene_buffer *find_scene_buffer(struct wlr_scene_node *node) {
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        return wlr_scene_buffer_from_node(node);
    }
    if (node->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
        struct wlr_scene_node *child;
        wl_list_for_each(child, &tree->children, link) {
            struct wlr_scene_buffer *buf = find_scene_buffer(child);
            if (buf) return buf;
        }
    }
    return nullptr;
}

struct wlr_scene_buffer *View::scene_buffer() {
    if (scene_surface) return scene_surface->buffer;
    if (scene_tree) return find_scene_buffer(&scene_tree->node);
    return nullptr;
}

void view_apply_box(View *view, int x, int y, int w, int h) {
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);


    if (view->xdg_surface && view->xdg_surface->toplevel) {
        if (w != view->cfg_w || h != view->cfg_h) {
            view->cfg_w = w;
            view->cfg_h = h;
            wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, w, h);
            wlr_xdg_surface_schedule_configure(view->xdg_surface);
        }
    } else if (view->xsurface) {
        if (x != view->cfg_x || y != view->cfg_y ||
            w != view->cfg_w || h != view->cfg_h) {
            view->cfg_x = x;
            view->cfg_y = y;
            view->cfg_w = w;
            view->cfg_h = h;
            wlr_xwayland_surface_configure(view->xsurface,
                (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h);
        }
    }
}





static const int OPEN_SLIDE = 30;

static void on_map(Server *server, View *view) {
    server->arrange();

    if (server->animations_enabled() && !server->suppress_anims && !view->closing) {


        const int sx = view->gx;
        const int sy = view->gy + OPEN_SLIDE;
        const int sw = view->gw;
        const int sh = view->gh;
        uint64_t dur = server->animation_duration_ns();
        Easing e = server->easing();

        wlr_scene_node_set_position(&view->scene_tree->node, sx, sy);
        struct wlr_scene_buffer *buf = view->scene_buffer();
        if (buf) wlr_scene_buffer_set_opacity(buf, 0.0f);

        view->opening = true;
        view->opening_start = anim_now_ns();


        view->geo.begin(anim_now_ns(), dur, e, sx, sy, sw, sh,
                        view->gx, view->gy, view->gw, view->gh);
    }

    corners_update(server, view);
    corners_opacity(view, view->opening ? 0.0f : 1.0f);

    server->focus_view(view, view->wlr_surface());
}





void View::xdg_map(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, map);
    view->mapped = true;
    on_map(view->server, view);
    wlr_log(WLR_DEBUG, "view mapped");
}

void View::xdg_unmap(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, unmap);
    view->mapped = false;
    corners_destroy(view);
    view->server->arrange();
}

void View::xdg_destroy(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, destroy);
    Server *server = view->server;
    server->views.remove(view);
    wlr_log(WLR_DEBUG, "view destroyed");

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);



    view->xdg_surface = nullptr;
    delete view;
    server->arrange();
    server->workspace_refresh();

    for (auto *v : server->views) {
        if (v->mapped && v->wlr_surface() && v->workspace == server->current_workspace) {
            server->focus_view(v, v->wlr_surface());
            break;
        }
    }
}

void View::xdg_commit(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, commit);
    if (!view->initial_configured && view->xdg_surface &&
        view->xdg_surface->initial_commit) {
        view->initial_configured = true;
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
        wlr_log(WLR_INFO, "sent initial configure to toplevel");
    }
}





void View::xwayland_associate(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, associate);
    struct wlr_surface *surface = view->xsurface ? vlswm_xsurface_surface(view->xsurface) : nullptr;
    if (!surface) return;
    if (view->scene_surface) {
        wlr_scene_node_destroy(&view->scene_surface->buffer->node);
        view->scene_surface = nullptr;
    }
    view->scene_surface = wlr_scene_surface_create(view->scene_tree, surface);
    view->mapped = true;
    on_map(view->server, view);
    wlr_log(WLR_INFO, "xwayland surface associated (window %u)",
            vlswm_xsurface_window_id(view->xsurface));
}

void View::xwayland_dissociate(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, dissociate);
    if (view->scene_surface) {
        wlr_scene_node_destroy(&view->scene_surface->buffer->node);
        view->scene_surface = nullptr;
    }
    view->mapped = false;
    corners_destroy(view);
    view->server->arrange();
}

void View::xwayland_destroy(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, x_destroy);
    Server *server = view->server;
    server->views.remove(view);

    wl_list_remove(&view->associate.link);
    wl_list_remove(&view->dissociate.link);
    wl_list_remove(&view->x_destroy.link);

    if (view->scene_surface) {
        wlr_scene_node_destroy(&view->scene_surface->buffer->node);
        view->scene_surface = nullptr;
    }
    corners_destroy(view);
    view->xsurface = nullptr;
    delete view;
    server->arrange();
    server->workspace_refresh();

    for (auto *v : server->views) {
        if (v->mapped && v->wlr_surface() && v->workspace == server->current_workspace) {
            server->focus_view(v, v->wlr_surface());
            break;
        }
    }
}
