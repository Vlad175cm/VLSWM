#include "vlswm/wlroots.h"
#include "vlswm/view.h"
#include "vlswm/server.h"


#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

static void set_tree_opacity(struct wlr_scene_node *node, float o) {
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(node), o);
        return;
    }
    if (node->type == WLR_SCENE_NODE_TREE) {
        struct wlr_scene_tree *tree = wlr_scene_tree_from_node(node);
        struct wlr_scene_node *child;
        wl_list_for_each(child, &tree->children, link) {
            set_tree_opacity(child, o);
        }
    }
}

View::View(Server *server, struct wlr_xdg_surface *xdg_surface, struct wlr_scene_tree *scene_tree)
    : server(server), xdg_surface(xdg_surface), scene_tree(scene_tree) {
    map.notify = View::xdg_map;
    unmap.notify = View::xdg_unmap;
    destroy.notify = View::xdg_destroy;
    commit.notify = View::surface_commit;

    struct wlr_surface *surface = xdg_surface->surface;
    wl_signal_add(&surface->events.map, &map);
    wl_signal_add(&surface->events.unmap, &unmap);
    wl_signal_add(&surface->events.commit, &commit);
    wl_signal_add(&xdg_surface->events.destroy, &destroy);
}

struct wlr_surface *View::wlr_surface() {
    return xdg_surface ? xdg_surface->surface : nullptr;
}

void View::update_dialog() {
    if (!xdg_surface || !xdg_surface->toplevel) return;
    struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
    bool fixed = toplevel->current.min_width > 0 &&
                 toplevel->current.min_height > 0 &&
                 toplevel->current.min_width == toplevel->current.max_width &&
                 toplevel->current.min_height == toplevel->current.max_height;
    dialog = toplevel->parent != nullptr || fixed;

    if (std::getenv("VLSWM_DEBUG_WINDOWS")) {
        wlr_log(WLR_ERROR,
                "window app_id=%s title=%s parent=%p min=%dx%d max=%dx%d size=%dx%d "
                "surface=%dx%d geometry=%dx%d+%d+%d dialog=%d",
                toplevel->app_id ? toplevel->app_id : "-",
                toplevel->title ? toplevel->title : "-",
                (void *)toplevel->parent,
                toplevel->current.min_width, toplevel->current.min_height,
                toplevel->current.max_width, toplevel->current.max_height,
                toplevel->current.width, toplevel->current.height,
                xdg_surface->surface->current.width, xdg_surface->surface->current.height,
                xdg_surface->geometry.width, xdg_surface->geometry.height,
                xdg_surface->geometry.x, xdg_surface->geometry.y,
                dialog ? 1 : 0);
    }
}

void View::configure_size() {
    if (!xdg_surface || !xdg_surface->toplevel) return;
    struct wlr_xdg_toplevel *toplevel = xdg_surface->toplevel;
    if (dialog) {
        int w = 0, h = 0;
        if (toplevel->current.min_width > 0 &&
            toplevel->current.min_width == toplevel->current.max_width) {
            w = toplevel->current.min_width;
        }
        if (toplevel->current.min_height > 0 &&
            toplevel->current.min_height == toplevel->current.max_height) {
            h = toplevel->current.min_height;
        }
        wlr_xdg_toplevel_set_fullscreen(toplevel, false);
        wlr_xdg_toplevel_set_size(toplevel, w, h);
        return;
    }
    int w = 0, h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, &w, &h);
    }
    wlr_xdg_toplevel_set_size(toplevel, w, h);
    wlr_xdg_toplevel_set_fullscreen(toplevel, true);
}

void View::center() {
    if (!dialog || !xdg_surface || !xdg_surface->surface || server->outputs.empty()) return;
    struct wlr_surface *surface = xdg_surface->surface;
    int scale = surface->current.scale > 0 ? surface->current.scale : 1;
    int w = xdg_surface->geometry.width;
    int h = xdg_surface->geometry.height;
    if (w <= 0 || h <= 0) {
        w = surface->current.width / scale;
        h = surface->current.height / scale;
    }
    if (w <= 0 || h <= 0) return;

    int ow = 0, oh = 0;
    wlr_output_effective_resolution(server->outputs.front()->wlr_output, &ow, &oh);
    base_x = std::max(0, (ow - w) / 2);
    base_y = std::max(0, (oh - h) / 2);
    wlr_scene_node_set_position(&scene_tree->node, base_x,
                                base_y + (int)std::lround(slide_y));
}

void View::set_opacity(float o) {
    if (scene_tree) set_tree_opacity(&scene_tree->node, o);
}

void View::xdg_map(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, map);
    Server *server = view->server;

    const char *app_id = view->xdg_surface->toplevel ? view->xdg_surface->toplevel->app_id : nullptr;
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(view->xdg_surface->resource),
                              &pid, nullptr, nullptr);
    view->pid = pid;
    view->foot = app_id && (strcmp(app_id, "foot") == 0 ||
                            strcmp(app_id, "footclient") == 0);

    if (server->base_pid > 0) {
        view->terminal = pid == server->base_pid;
    } else if (view->foot) {
        bool base_alive = false;
        for (auto *v : server->stack) {
            if (v->terminal) base_alive = true;
        }
        if (!base_alive) {
            view->terminal = true;
            server->base_pid = pid;
        }
    }

    view->mapped = true;
    view->closing = false;
    view->update_dialog();
    view->configure_size();
    view->center();
    server->stack.push_back(view);
    view->set_opacity(0.0f);
    view->opacity = 0.0f;
    view->slide_y = 14.0f;
    wlr_scene_node_set_position(&view->scene_tree->node, view->base_x,
                                view->base_y + 14);
    uint64_t now = anim_now_ns();
    view->fade.begin(now, server->settings.duration_ns, server->settings.easing, 0.0f, 1.0f);
    view->slide.begin(now, server->settings.duration_ns, server->settings.easing, 14.0f, 0.0f);
    server->focus_view(view, view->wlr_surface());
    server->update_visibility();
    server->request_frame();
}

void View::xdg_unmap(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, unmap);
    Server *server = view->server;
    view->mapped = false;
    for (auto it = server->stack.begin(); it != server->stack.end(); ++it) {
        if (*it == view) {
            server->stack.erase(it);
            break;
        }
    }
    server->focus_top();
    server->update_visibility();
    server->request_frame();
}

void View::xdg_destroy(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, destroy);
    Server *server = view->server;

    for (auto it = server->stack.begin(); it != server->stack.end(); ++it) {
        if (*it == view) {
            server->stack.erase(it);
            break;
        }
    }
    bool was_terminal = view->terminal;

    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->commit.link);
    wl_list_remove(&view->destroy.link);

    view->xdg_surface = nullptr;
    delete view;

    server->focus_top();
    server->update_visibility();
    server->request_frame();
    if (was_terminal) server->terminal_gone();
}

void View::surface_commit(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, commit);
    if (view->dialog) view->center();
    if (view->xdg_surface && !view->initial_configured && view->xdg_surface->initial_commit) {
        view->initial_configured = true;
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}
