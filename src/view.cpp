#include "vlswm/wlroots.h"
#include "vlswm/view.h"
#include "vlswm/server.h"


#include <cstring>

static const char *TERMINAL_APP_ID = "com.mitchellh.ghostty";

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

void View::configure_size() {
    if (!xdg_surface || !xdg_surface->toplevel) return;
    int w = 0, h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, &w, &h);
    }
    wlr_xdg_toplevel_set_size(xdg_surface->toplevel, w, h);
    wlr_xdg_toplevel_set_fullscreen(xdg_surface->toplevel, true);
}

void View::set_opacity(float o) {
    if (scene_tree) set_tree_opacity(&scene_tree->node, o);
}

void View::xdg_map(wl_listener *listener, void *data) {
    (void)data;
    View *view = wl_container_of(listener, view, map);
    Server *server = view->server;

    const char *app_id = view->xdg_surface->toplevel ? view->xdg_surface->toplevel->app_id : nullptr;
    bool is_terminal = app_id && strcmp(app_id, TERMINAL_APP_ID) == 0;
    if (is_terminal) {
        pid_t pid = 0;
        wl_client_get_credentials(wl_resource_get_client(view->xdg_surface->resource),
                                  &pid, nullptr, nullptr);
        view->ghostty = true;
        view->pid = pid;

        bool base_alive = false;
        for (auto *v : server->stack) {
            if (v->terminal) base_alive = true;
        }

        view->terminal = !base_alive;
        if (!base_alive && server->base_pid == 0) server->base_pid = pid;
    }

    view->mapped = true;
    view->closing = false;
    view->configure_size();
    server->stack.push_back(view);
    view->set_opacity(0.0f);
    view->fade.begin(anim_now_ns(), server->settings.duration_ns, server->settings.easing, 0.0f, 1.0f);
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
    if (view->xdg_surface && !view->initial_configured && view->xdg_surface->initial_commit) {
        view->initial_configured = true;
        wlr_xdg_surface_schedule_configure(view->xdg_surface);
    }
}
