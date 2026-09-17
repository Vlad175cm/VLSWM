
#include "vlswm/server.h"
#include "vlswm/view.h"
#include "vlswm/ini.h"
#include "vlswm/wallpaper.h"
#include "vlswm/corners.h"

#include <xkbcommon/xkbcommon.h>
#include <unistd.h>
#include <linux/input.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_xdg_activation_v1.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <memory>
#include <string>

extern char **environ;

Output *create_output(Server *server, struct wlr_output *wlr_output);





View *desktop_view_at(Server *server, double lx, double ly,
                      struct wlr_surface **surface, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene_shell->node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return nullptr;
    }
    *surface = scene_surface->surface;


    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree->node.data == nullptr) {
        tree = tree->node.parent;
    }
    return tree ? (View *)tree->node.data : nullptr;
}





struct Keyboard {
    Server *server;
    struct wlr_input_device *device;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;


    bool swallowed = false;
    uint32_t swallowed_keycode = 0;
};

static void spawn_cmd(const char *cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        execlp("/bin/sh", "/bin/sh", "-c", cmd, (char *)nullptr);
        wlr_log(WLR_ERROR, "failed to exec '%s': %s", cmd, strerror(errno));
        _exit(1);
    } else if (pid < 0) {
        wlr_log(WLR_ERROR, "failed to fork '%s': %s", cmd, strerror(errno));
    }
}




static View *primary_view(Server *server) {
    for (auto *v : server->views) {
        if (v->mapped && !v->closing && !v->floating &&
            v->workspace == server->current_workspace) {
            return v;
        }
    }
    return nullptr;
}

static bool handle_key(Server *server, xkb_keysym_t sym, uint32_t modifiers) {
    bool super = modifiers & WLR_MODIFIER_LOGO;
    bool ctrl = modifiers & WLR_MODIFIER_CTRL;
    bool alt = modifiers & WLR_MODIFIER_ALT;
    bool shift = modifiers & WLR_MODIFIER_SHIFT;

    if ((super || ctrl) && sym == XKB_KEY_Escape) {
        wl_display_terminate(server->wl_display);
        return true;
    }

    if (super && !shift && sym == XKB_KEY_Return) {
        std::string term;
        if (server->config) term = server->config->get("launcher", "terminal");
        if (term.empty()) {
            const char *t = std::getenv("TERMINAL");
            term = (t && *t) ? t : "/usr/bin/kitty";
        }
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            execlp("/bin/sh", "/bin/sh", "-c", term.c_str(), (char *)nullptr);
            wlr_log(WLR_ERROR, "failed to exec terminal '%s': %s", term.c_str(), strerror(errno));
            _exit(1);
        }
        if (pid < 0) {
            wlr_log(WLR_ERROR, "failed to fork terminal: %s", strerror(errno));
        }
        return true;
    }

    if (super && sym == XKB_KEY_q) {


        View *v = primary_view(server);
        if (!v) v = server->focused_view();
        if (v) server->close_view(v);
        return true;
    }

    if (alt && sym == XKB_KEY_Tab) {
        if (server->views.size() > 1) {
            auto it = std::find(server->views.begin(), server->views.end(),
                                server->focused_view());
            if (it == server->views.end()) {
                it = server->views.begin();
            } else {
                ++it;
                if (it == server->views.end()) it = server->views.begin();
            }
            struct wlr_surface *s = (*it)->wlr_surface();
            server->focus_view(*it, s);
        }
        return true;
    }


    switch (sym) {
        case XKB_KEY_XF86MonBrightnessDown: spawn_cmd("brightnessctl -q set 5%-"); return true;
        case XKB_KEY_XF86MonBrightnessUp:   spawn_cmd("brightnessctl -q set 5%+"); return true;
        case XKB_KEY_XF86KbdBrightnessDown:
            spawn_cmd(server->config
                ? server->config->get("keybinds", "kbd_brightness_down",
                    "brightnessctl --device='smc::kbd_backlight' set 10%-").c_str()
                : "brightnessctl --device='smc::kbd_backlight' set 10%-");
            return true;
        case XKB_KEY_XF86KbdBrightnessUp:
            spawn_cmd(server->config
                ? server->config->get("keybinds", "kbd_brightness_up",
                    "brightnessctl --device='smc::kbd_backlight' set +10%").c_str()
                : "brightnessctl --device='smc::kbd_backlight' set +10%");
            return true;
        case XKB_KEY_XF86AudioLowerVolume:  spawn_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-"); return true;
        case XKB_KEY_XF86AudioRaiseVolume:  spawn_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+"); return true;
        case XKB_KEY_XF86AudioMute:         spawn_cmd("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"); return true;
        default: break;
    }

    if (super) {
        switch (sym) {
            case XKB_KEY_F1: spawn_cmd("brightnessctl -q set 5%-"); return true;
            case XKB_KEY_F2: spawn_cmd("brightnessctl -q set 5%+"); return true;
            case XKB_KEY_F3: spawn_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%-"); return true;
            case XKB_KEY_F4: spawn_cmd("wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+"); return true;
            default: break;
        }
    }


    if (super) {
        switch (sym) {
            case XKB_KEY_Left:
                if (alt) server->swap_focus(-1);
                else server->move_focus(-1);
                return true;
            case XKB_KEY_Right:
                if (alt) server->swap_focus(1);
                else server->move_focus(1);
                return true;
            case XKB_KEY_space:
                server->toggle_floating();
                return true;
            case XKB_KEY_f:
                server->toggle_fullscreen();
                return true;
            case XKB_KEY_r:
                server->reload_config();
                return true;
            case XKB_KEY_d: {
                pid_t pid = fork();
                if (pid == 0) {
                    setsid();
                    execlp("/usr/bin/fuzzel", "fuzzel", (char *)nullptr);
                    wlr_log(WLR_ERROR, "failed to exec fuzzel: %s", strerror(errno));
                    _exit(1);
                }
                return true;
            }
            case XKB_KEY_Return:
                if (shift) server->move_to_master();
                return true;
            default:
                break;
        }

        if (sym >= XKB_KEY_1 && sym <= XKB_KEY_9) {
            int ws = sym - XKB_KEY_1 + 1;
            if (shift) server->move_focused_to_workspace(ws);
            else server->switch_workspace(ws);
            return true;
        }
    }

    return false;
}

static void keyboard_handle_key(wl_listener *listener, void *data) {
    Keyboard *kb = wl_container_of(listener, kb, key);
    struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(kb->device);
    struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->xkb_state, keycode, &syms);

    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
    bool consumed = kb->swallowed && kb->swallowed_keycode == event->keycode;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED && nsyms > 0) {
        wlr_log(WLR_INFO, "KEY keycode=%u sym=0x%x mods=0x%x (logo=%d ctrl=%d alt=%d shift=%d)",
                event->keycode, (unsigned)syms[0], modifiers,
                !!(modifiers & WLR_MODIFIER_LOGO), !!(modifiers & WLR_MODIFIER_CTRL),
                !!(modifiers & WLR_MODIFIER_ALT), !!(modifiers & WLR_MODIFIER_SHIFT));
        kb->swallowed = false;
        for (int i = 0; i < nsyms; i++) {
            if (handle_key(kb->server, syms[i], modifiers)) {
                kb->swallowed = true;
                kb->swallowed_keycode = event->keycode;
            }
        }
    }

    wlr_seat_set_keyboard(kb->server->seat, keyboard);



    if (consumed) {
        kb->swallowed = false;
        return;
    }
    if (kb->swallowed && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
    }

    wlr_seat_keyboard_notify_key(kb->server->seat, event->time_msec, event->keycode,
                                 event->state);
}

static void keyboard_handle_modifiers(wl_listener *listener, void *data) {
    Keyboard *kb = wl_container_of(listener, kb, modifiers);
    (void)data;
    struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(kb->device);
    wlr_seat_set_keyboard(kb->server->seat, keyboard);
    wlr_seat_keyboard_notify_modifiers(kb->server->seat, &keyboard->modifiers);
}

static void keyboard_handle_destroy(wl_listener *listener, void *data) {
    Keyboard *kb = wl_container_of(listener, kb, destroy);
    (void)data;
    wl_list_remove(&kb->modifiers.link);
    wl_list_remove(&kb->key.link);
    wl_list_remove(&kb->destroy.link);
    if (kb->server->keyboard == wlr_keyboard_from_input_device(kb->device)) {
        kb->server->keyboard = nullptr;
    }
    delete kb;
}

static void server_new_keyboard(Server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(device);

    auto *kb = new Keyboard;
    kb->server = server;
    kb->device = device;

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(keyboard, 25, 600);

    kb->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&keyboard->events.modifiers, &kb->modifiers);
    kb->key.notify = keyboard_handle_key;
    wl_signal_add(&keyboard->events.key, &kb->key);
    kb->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &kb->destroy);

    server->keyboard = keyboard;
    wlr_seat_set_keyboard(server->seat, keyboard);
}

static void configure_libinput(struct wlr_input_device *device) {
    if (!wlr_input_device_is_libinput(device)) return;
    struct libinput_device *dev = wlr_libinput_get_device_handle(device);
    if (!dev) return;





    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
        enum libinput_config_status st =
            libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
        wlr_log(WLR_INFO, "tap enabled: status=%d fingers=%d", (int)st,
                libinput_device_config_tap_get_finger_count(dev));
        enum libinput_config_tap_button_map cur =
            libinput_device_config_tap_get_button_map(dev);
        if (cur != LIBINPUT_CONFIG_TAP_MAP_LRM) {
            st = libinput_device_config_tap_set_button_map(dev,
                LIBINPUT_CONFIG_TAP_MAP_LRM);
            wlr_log(WLR_INFO, "tap map LMR->LRM: status=%d", (int)st);
        } else {
            wlr_log(WLR_INFO, "tap map already LRM");
        }
    }

    if (libinput_device_config_scroll_get_methods(dev) & LIBINPUT_CONFIG_SCROLL_2FG) {
        enum libinput_config_status st =
            libinput_device_config_scroll_set_method(dev, LIBINPUT_CONFIG_SCROLL_2FG);
        wlr_log(WLR_INFO, "scroll 2fg: status=%d", (int)st);
        if (libinput_device_config_scroll_has_natural_scroll(dev)) {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev, false);
        }
    }
    wlr_log(WLR_INFO, "touchpad configured: tap+LRM + two-finger scroll");
}

static void server_new_pointer(Server *server, struct wlr_input_device *device) {
    server->pointer_device = device;
    configure_libinput(device);
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void handle_new_input(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = (struct wlr_input_device *)data;

    switch (device->type) {
        case WLR_INPUT_DEVICE_POINTER:
            server_new_pointer(server, device);
            break;
        case WLR_INPUT_DEVICE_KEYBOARD:
            server_new_keyboard(server, device);
            break;
        default:
            break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (server->keyboard) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}





static void handle_new_output(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = (struct wlr_output *)data;

    Output *output = create_output(server, wlr_output);
    if (!output) {
        wlr_log(WLR_ERROR, "Failed to create output %s", wlr_output->name);
        return;
    }

    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output_layout_add_output(
        wlr_scene_attach_output_layout(server->scene, server->output_layout),
        wlr_output_layout_get(server->output_layout, wlr_output), output->scene_output);
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
}





struct LayerSurface {
    Server *server;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wl_listener commit;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    bool configured = false;
};

static void arrange_layer_surface(LayerSurface *ls) {
    struct wlr_layer_surface_v1 *layer = ls->scene->layer_surface;
    struct wlr_box full_area = {};
    struct wlr_box usable_area = {};
    if (layer->output) {
        wlr_output_effective_resolution(layer->output,
            &full_area.width, &full_area.height);
        usable_area = full_area;
    }
    wlr_scene_layer_surface_v1_configure(ls->scene, &full_area, &usable_area);
}

static void layer_surface_commit(wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1 *layer = ls->scene->layer_surface;
    if (!ls->configured && layer->initial_commit) {
        ls->configured = true;
        arrange_layer_surface(ls);
    }
}

static void layer_surface_map(wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *ls = wl_container_of(listener, ls, map);
    arrange_layer_surface(ls);


    struct wlr_layer_surface_v1 *layer = ls->scene->layer_surface;
    if (layer->current.keyboard_interactive !=
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE &&
        ls->server->keyboard) {
        struct wlr_keyboard *kb = ls->server->keyboard;
        wlr_seat_keyboard_notify_enter(ls->server->seat, layer->surface,
            kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
}

static void layer_surface_unmap(wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *ls = wl_container_of(listener, ls, unmap);

    for (auto *v : ls->server->views) {
        if (v->mapped && v->wlr_surface() &&
            v->workspace == ls->server->current_workspace) {
            ls->server->focus_view(v, v->wlr_surface());
            break;
        }
    }
}

static void layer_surface_destroy(wl_listener *listener, void *data) {
    (void)data;
    LayerSurface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->destroy.link);
    delete ls;
}

static void handle_new_layer_surface(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = (struct wlr_layer_surface_v1 *)data;

    if (!layer_surface->output && !server->outputs.empty()) {
        layer_surface->output = server->outputs.front()->wlr_output;
    }

    struct wlr_scene_layer_surface_v1 *scene =
        wlr_scene_layer_surface_v1_create(server->scene_layers, layer_surface);
    if (!scene) return;

    auto *ls = new LayerSurface;
    ls->server = server;
    ls->scene = scene;

    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &ls->commit);
    ls->map.notify = layer_surface_map;
    wl_signal_add(&layer_surface->surface->events.map, &ls->map);
    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &ls->unmap);
    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, &ls->destroy);

    wlr_log(WLR_INFO, "new layer surface (namespace=%s)",
            layer_surface->wlr_layer_namespace ? layer_surface->wlr_layer_namespace : "(null)");
}









struct PopupWatcher {
    Server *server;
    struct wl_listener new_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
    bool position_logged = false;
};

static void watch_xdg_popups(Server *server, struct wlr_xdg_surface *xdg_surface);




static void popup_watcher_on_commit(wl_listener *listener, void *data) {
    PopupWatcher *w = wl_container_of(listener, w, commit);
    struct wlr_surface *surf = (struct wlr_surface *)data;
    struct wlr_xdg_surface *s = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!s || s->role != WLR_XDG_SURFACE_ROLE_POPUP || !s->popup) return;




    if (s->initial_commit) {
        wlr_xdg_surface_schedule_configure(s);
    }

    if (w->position_logged) return;
    w->position_logged = true;
    struct wlr_box g = s->current.geometry;
    int nx = -1, ny = -1;
    int px = -1, py = -1;
    struct wlr_scene_tree *tree = (struct wlr_scene_tree *)s->data;
    if (tree) {
        wlr_scene_node_coords(&tree->node, &nx, &ny);
        if (tree->node.parent)
            wlr_scene_node_coords(&tree->node.parent->node, &px, &py);
    }
    wlr_log(WLR_INFO,
            "popup MAP geo=%dx%d@%d,%d node=(%d,%d) window_tree=(%d,%d) cursor=(%.0f,%.0f)",
            g.width, g.height, g.x, g.y, nx, ny, px, py,
            w->server->cursor ? w->server->cursor->x : -1,
            w->server->cursor ? w->server->cursor->y : -1);
}




static void create_xdg_popup(Server *server, struct wlr_xdg_surface *xdg_surface) {
    if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_POPUP) return;
    struct wlr_xdg_popup *popup = xdg_surface->popup;
    struct wlr_scene_tree *parent = server->scene_shell;

    struct wlr_surface *anc = popup ? popup->parent : nullptr;
    while (anc) {
        struct wlr_xdg_surface *as = wlr_xdg_surface_try_from_wlr_surface(anc);
        if (as && as->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            for (auto *v : server->views)
                if (v->xdg_surface == as && v->scene_tree) { parent = v->scene_tree; break; }
            break;
        }
        if (as && as->popup) { anc = as->popup->parent; continue; }
        break;
    }

    struct wlr_scene_tree *node = wlr_scene_xdg_surface_create(parent, xdg_surface);
    if (!node) {
        wlr_log(WLR_ERROR, "xdg popup: scene create failed");
        return;
    }
    xdg_surface->data = node;

    int px = 0, py = 0;
    wlr_scene_node_coords(&node->node, &px, &py);
    struct wlr_box g = xdg_surface->current.geometry;
    wlr_log(WLR_INFO,
            "xdg popup created: geo=%dx%d@%d,%d node_coords=(%d,%d) parent_tree=%p is_root=%d",
            g.width, g.height, g.x, g.y, px, py, (void *)parent, parent == server->scene_shell);


    watch_xdg_popups(server, xdg_surface);

    wlr_log(WLR_INFO, "xdg popup created (parent=%p tree=%p)", (void *)anc, (void *)parent);
}

static void popup_watcher_on_new_popup(wl_listener *listener, void *data) {
    struct PopupWatcher *w = wl_container_of(listener, w, new_popup);
    struct wlr_xdg_popup *popup = (struct wlr_xdg_popup *)data;
    create_xdg_popup(w->server, popup->base);
}

static void popup_watcher_on_destroy(wl_listener *listener, void *data) {
    struct PopupWatcher *w = wl_container_of(listener, w, destroy);
    wl_list_remove(&w->new_popup.link);
    wl_list_remove(&w->commit.link);
    wl_list_remove(&w->destroy.link);
    delete w;
}

static void watch_xdg_popups(Server *server, struct wlr_xdg_surface *xdg_surface) {
    auto *w = new PopupWatcher;
    w->server = server;
    w->new_popup.notify = popup_watcher_on_new_popup;
    w->commit.notify = popup_watcher_on_commit;
    w->destroy.notify = popup_watcher_on_destroy;
    wl_signal_add(&xdg_surface->events.new_popup, &w->new_popup);
    wl_signal_add(&xdg_surface->surface->events.commit, &w->commit);
    wl_signal_add(&xdg_surface->events.destroy, &w->destroy);
}

static void handle_new_xdg_toplevel(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_toplevel *toplevel = (struct wlr_xdg_toplevel *)data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    struct wlr_scene_tree *scene_tree =
        wlr_scene_xdg_surface_create(server->scene_shell, xdg_surface);
    auto *view = new View(server, xdg_surface, scene_tree);
    scene_tree->node.data = view;
    view->workspace = server->current_workspace;

    server->views.push_back(view);
    wlr_log(WLR_INFO, "new xdg toplevel created (role=%d) ws=%d cur=%d", (int)xdg_surface->role,
            view->workspace, server->current_workspace);


    watch_xdg_popups(server, xdg_surface);
}





static void handle_xwayland_new_surface(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, xwayland_new_surface);
    struct wlr_xwayland_surface *xsurface = (struct wlr_xwayland_surface *)data;

    if (vlswm_xsurface_override_redirect(xsurface)) {
        return;
    }

    struct wlr_scene_tree *scene_tree =
        wlr_scene_tree_create(server->scene_shell);
    auto *view = new View(server, xsurface, scene_tree);
    scene_tree->node.data = view;
    view->workspace = server->current_workspace;

    server->views.push_back(view);
    wlr_log(WLR_INFO, "new xwayland surface (window %u) ws=%d",
            vlswm_xsurface_window_id(xsurface), view->workspace);
}

static void handle_xwayland_ready(wl_listener *listener, void *data) {
    (void)data;
    Server *server = wl_container_of(listener, server, xwayland_ready);
    setenv("DISPLAY", vlswm_xwayland_display_name(server->xwayland), true);
    wlr_xwayland_set_seat(server->xwayland, server->seat);
    wlr_log(WLR_INFO, "Xwayland ready (DISPLAY=%s)",
            vlswm_xwayland_display_name(server->xwayland));
}





View *Server::focused_view() {
    if (!seat) return nullptr;
    struct wlr_surface *focused = seat->keyboard_state.focused_surface;
    if (!focused) return nullptr;

    auto match = [&](struct wlr_surface *s) -> View * {
        for (auto *v : views)
            if (v->wlr_surface() == s) return v;
        return nullptr;
    };
    if (View *v = match(focused)) return v;





    struct wlr_surface *s = focused;
    for (int depth = 0; depth < 8 && s; depth++) {
        struct wlr_xdg_surface *xs = wlr_xdg_surface_try_from_wlr_surface(s);
        if (!xs || xs->role != WLR_XDG_SURFACE_ROLE_POPUP ||
            !xs->popup || !xs->popup->parent)
            break;
        s = xs->popup->parent;
        if (View *v = match(s)) return v;
    }
    return nullptr;
}

void Server::focus_view(View *view, struct wlr_surface *surface) {
    if (!view || !view->wlr_surface() || !surface) {
        wlr_seat_keyboard_clear_focus(seat);
        return;
    }
    struct wlr_surface *prev = seat->keyboard_state.focused_surface;
    if (prev == surface) return;


    if (prev) {
        for (auto *v : views) {
            if (v != view && v->wlr_surface() == prev) {
                if (v->xdg_surface && v->xdg_surface->toplevel) {
                    wlr_xdg_toplevel_set_activated(v->xdg_surface->toplevel, false);
                } else if (v->xsurface) {
                    wlr_xwayland_surface_activate(v->xsurface, false);
                }
            }
        }
    }


    if (view->xdg_surface && view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
    } else if (view->xsurface) {
        wlr_xwayland_surface_activate(view->xsurface, true);
    }
    wlr_scene_node_raise_to_top(&view->scene_tree->node);






    if (view->wlr_surface() == surface) {
        struct wlr_keyboard *kb = keyboard;
        if (kb) {
            wlr_seat_keyboard_notify_enter(seat, surface,
                                           kb->keycodes, kb->num_keycodes, &kb->modifiers);
        }
    }
}

void Server::move_focus(int dx) {
    View *cur = focused_view();
    if (!cur || views.empty()) return;


    std::list<View *> order;
    for (auto *v : views) {
        if (v->mapped && !v->floating && !v->fullscreen &&
            v->workspace == current_workspace) order.push_back(v);
    }
    if (order.empty()) return;

    auto it = std::find(order.begin(), order.end(), cur);
    if (it == order.end()) it = order.begin();

    if (dx > 0) {
        ++it;
        if (it == order.end()) it = order.begin();
    } else {
        if (it == order.begin()) it = order.end();
        --it;
    }

    focus_view(*it, (*it)->xdg_surface->surface);

    arrange();
}

void Server::swap_focus(int dx) {
    View *cur = focused_view();
    if (!cur || views.size() < 2) return;


    std::list<View *> order;
    for (auto *v : views) {
        if (v->mapped && !v->floating && !v->fullscreen &&
            v->workspace == current_workspace) order.push_back(v);
    }
    if (order.size() < 2) return;

    auto it = std::find(order.begin(), order.end(), cur);
    if (it == order.end()) return;

    auto other = it;
    if (dx > 0) {
        ++other;
        if (other == order.end()) other = order.begin();
    } else {
        if (other == order.begin()) other = order.end();
        --other;
    }
    if (other == it) return;



    View *a = cur, *b = *other;
    auto ia = std::find(views.begin(), views.end(), a);
    auto ib = std::find(views.begin(), views.end(), b);
    if (ia == views.end() || ib == views.end()) return;
    std::iter_swap(ia, ib);


    focus_view(cur, cur->xdg_surface->surface);
    arrange();
}

void Server::move_to_master() {
    View *cur = focused_view();
    if (!cur) return;
    views.remove(cur);
    views.push_front(cur);
    arrange();
}

void Server::toggle_floating() {
    View *cur = focused_view();
    if (!cur) return;
    cur->floating = !cur->floating;
    arrange();
    corners_update(this, cur);
}

void Server::toggle_fullscreen() {
    View *cur = focused_view();
    if (!cur) return;
    cur->fullscreen = !cur->fullscreen;
    arrange();
    corners_update(this, cur);
}

void Server::switch_workspace(int id) {
    current_workspace = id;
    wlr_log(WLR_INFO, "switch_workspace -> %d", id);

    for (auto *v : views) {
        if (v->mapped && v->workspace == id && v->xdg_surface) {
            focus_view(v, v->xdg_surface->surface);
            break;
        }
    }
    suppress_anims = true;
    arrange();
    suppress_anims = false;
}

void Server::move_focused_to_workspace(int id) {
    View *cur = focused_view();
    if (!cur) return;
    cur->workspace = id;

    bool any_left = false;
    for (auto *v : views) {
        if (v->workspace == current_workspace) { any_left = true; break; }
    }
    if (!any_left) current_workspace = id;
    suppress_anims = true;
    arrange();
    suppress_anims = false;
}

void Server::workspace_refresh() {
    suppress_anims = true;
    arrange();
    suppress_anims = false;
}






static void process_cursor_motion(Server *server, uint32_t time_msec) {
    double sx, sy;
    struct wlr_surface *surface = nullptr;
    View *view = desktop_view_at(server, server->cursor->x, server->cursor->y,
                                 &surface, &sx, &sy);

    if (view && surface) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
    } else {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

static void handle_cursor_motion(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = (struct wlr_pointer_motion_event *)data;
    wlr_cursor_move(server->cursor, server->pointer_device, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_motion_absolute(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event =
        (struct wlr_pointer_motion_absolute_event *)data;
    wlr_cursor_warp_absolute(server->cursor, server->pointer_device, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void handle_cursor_button(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = (struct wlr_pointer_button_event *)data;





    if (event->button == BTN_MIDDLE && server->pointer_device &&
        wlr_input_device_is_libinput(server->pointer_device)) {
        struct libinput_device *ldev =
            wlr_libinput_get_device_handle(server->pointer_device);
        if (ldev && libinput_device_get_id_vendor(ldev) == 0x05ac &&
            libinput_device_get_id_product(ldev) == 0x0291) {
            wlr_log(WLR_DEBUG, "POINTER button=274 (3-finger tap) dropped on touchpad");
            return;
        }
    }

    double sx, sy;
    struct wlr_surface *surface = nullptr;
    View *view = nullptr;
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        view = desktop_view_at(server, server->cursor->x, server->cursor->y,
                               &surface, &sx, &sy);
        wlr_log(WLR_INFO, "POINTER button=%u down at(%.0f,%.0f) surface=%p view=%p offset(%.0f,%.0f)",
                event->button, server->cursor->x, server->cursor->y,
                (void *)surface, (void *)view, sx, sy);
        if (view && surface) {
            server->focus_view(view, surface);
        }
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button,
                                   event->state);
}

static void handle_cursor_axis(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = (struct wlr_pointer_axis_event *)data;
    wlr_log(WLR_DEBUG, "POINTER axis orient=%d delta=%.3f discrete=%d source=%d",
            event->orientation, event->delta, event->delta_discrete, event->source);
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
                                 event->delta, event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void handle_cursor_frame(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_frame);
    (void)data;
    wlr_seat_pointer_notify_frame(server->seat);
}





static void handle_request_set_selection(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event =
        (struct wlr_seat_request_set_selection_event *)data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static void handle_request_set_primary_selection(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event =
        (struct wlr_seat_request_set_primary_selection_event *)data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

static void handle_request_start_drag(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event =
        (struct wlr_seat_request_start_drag_event *)data;
    if (wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial)) {
        wlr_seat_start_drag(server->seat, event->drag, event->serial);
    } else {
        wlr_data_source_destroy(event->drag->source);
    }
}

static void handle_request_activate(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event =
        (struct wlr_xdg_activation_v1_request_activate_event *)data;
    if (!event->token || !event->surface) return;

    for (auto *v : server->views) {
        if (v->wlr_surface() == event->surface) {
            server->focus_view(v, event->surface);
            return;
        }
    }
}





bool Server::init(IniConfig *config) {
    this->config = config;
    this->xdg_activation = nullptr;
    const char *socket_name;

    wl_display = wl_display_create();
    if (!wl_display) return false;

    struct wlr_session *session = nullptr;
    backend = wlr_backend_autocreate(wl_display_get_event_loop(wl_display), &session);
    if (!backend) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        goto fail;
    }

    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "Failed to create renderer");
        goto fail;
    }
    wlr_renderer_init_wl_display(renderer, wl_display);

    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "Failed to create allocator");
        goto fail;
    }

compositor = wlr_compositor_create(wl_display, 6, renderer);
    xdg_shell = wlr_xdg_shell_create(wl_display, 6);
    layer_shell = wlr_layer_shell_v1_create(wl_display, 4);
    wlr_subcompositor_create(wl_display);
    linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(wl_display, 4, renderer);
    wlr_viewporter_create(wl_display);
    wlr_fractional_scale_manager_v1_create(wl_display, 1);
    wlr_content_type_manager_v1_create(wl_display, 1);
    wlr_single_pixel_buffer_manager_v1_create(wl_display);
    wlr_xdg_decoration_manager_v1_create(wl_display);
    wlr_pointer_gestures_v1_create(wl_display);
    wlr_text_input_manager_v3_create(wl_display);
    this->xdg_activation = wlr_xdg_activation_v1_create(wl_display);

    xwayland = wlr_xwayland_create(wl_display, compositor, false );

    wlr_data_device_manager_create(wl_display);
    wlr_data_control_manager_v1_create(wl_display);
    wlr_primary_selection_v1_device_manager_create(wl_display);

    output_layout = wlr_output_layout_create(wl_display);
    wlr_xdg_output_manager_v1_create(wl_display, output_layout);

    scene = wlr_scene_create();
    scene_background = wlr_scene_tree_create(&scene->tree);
    scene_shell = wlr_scene_tree_create(&scene->tree);
    scene_layers = wlr_scene_tree_create(&scene->tree);

    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, output_layout);
    cursor_mgr = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(cursor_mgr, 1.0f);

    seat = wlr_seat_create(wl_display, "seat0");

    request_set_selection.notify = handle_request_set_selection;
    wl_signal_add(&seat->events.request_set_selection, &request_set_selection);
    request_set_primary_selection.notify = handle_request_set_primary_selection;
    wl_signal_add(&seat->events.request_set_primary_selection, &request_set_primary_selection);
    request_start_drag.notify = handle_request_start_drag;
    wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
    if (this->xdg_activation) {
        request_activate.notify = handle_request_activate;
        wl_signal_add(&xdg_activation->events.request_activate, &request_activate);
    }

    new_output.notify = handle_new_output;
    wl_signal_add(&backend->events.new_output, &new_output);
    new_input.notify = handle_new_input;
    wl_signal_add(&backend->events.new_input, &new_input);
    new_xdg_surface.notify = handle_new_xdg_toplevel;
    wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_surface);
    new_layer_surface.notify = handle_new_layer_surface;
    wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);
    xwayland_ready.notify = handle_xwayland_ready;
    wl_signal_add(vlswm_xwayland_signal_ready(xwayland), &xwayland_ready);
    xwayland_new_surface.notify = handle_xwayland_new_surface;
    wl_signal_add(vlswm_xwayland_signal_new_surface(xwayland), &xwayland_new_surface);

    cursor_motion.notify = handle_cursor_motion;
    wl_signal_add(&cursor->events.motion, &cursor_motion);
    cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
    cursor_button.notify = handle_cursor_button;
    wl_signal_add(&cursor->events.button, &cursor_button);
    cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&cursor->events.axis, &cursor_axis);
    cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&cursor->events.frame, &cursor_frame);


    socket_name = wl_display_add_socket_auto(wl_display);
    if (!socket_name) {
        wlr_log(WLR_ERROR, "Failed to add Wayland socket (XDG_RUNTIME_DIR=%s)",
                std::getenv("XDG_RUNTIME_DIR") ?: "unset");
        finish();
        return false;
    }
    setenv("WAYLAND_DISPLAY", socket_name, true);
    setenv("XDG_CURRENT_DESKTOP", "vlswm:wlroots", true);
    setenv("DESKTOP_SESSION", "vlswm", true);
    wlr_log(WLR_INFO, "vlswm listening on %s (XDG_RUNTIME_DIR=%s)",
            socket_name, std::getenv("XDG_RUNTIME_DIR") ?: "unset");

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        finish();
        return false;
    }

    return true;

fail:
    finish();
    return false;
}

void Server::run() {
    wl_display_run(wl_display);
}

void Server::finish() {
    if (wl_display) {

        wl_list_remove(&new_output.link);
        wl_list_remove(&new_input.link);
        wl_list_remove(&new_xdg_surface.link);
        wl_list_remove(&new_layer_surface.link);
        wl_list_remove(&xwayland_ready.link);
        wl_list_remove(&xwayland_new_surface.link);
        wl_list_remove(&cursor_motion.link);
        wl_list_remove(&cursor_motion_absolute.link);
        wl_list_remove(&cursor_button.link);
        wl_list_remove(&cursor_axis.link);
        wl_list_remove(&cursor_frame.link);
        wl_list_remove(&request_set_selection.link);
        wl_list_remove(&request_set_primary_selection.link);
        wl_list_remove(&request_start_drag.link);
        if (this->xdg_activation) wl_list_remove(&request_activate.link);

        wl_display_destroy_clients(wl_display);
        wl_display_destroy(wl_display);
        wl_display = nullptr;
    }
}

void Server::reload_config() {
    wlr_log(WLR_INFO, "Reloading configuration...");


    std::string config_path;
    if (config && !config->path().empty()) {
        config_path = config->path();
    } else {
        const char *xdg = std::getenv("XDG_CONFIG_HOME");
        const char *home = std::getenv("HOME");
        if (xdg) {
            config_path = std::string(xdg) + "/vlswm/config";
        } else if (home) {
            config_path = std::string(home) + "/.config/vlswm/config";
        }
    }

    if (config_path.empty()) {
        wlr_log(WLR_ERROR, "No config path available for reload");
        return;
    }


    auto new_cfg = IniConfig::load(config_path);
    if (!new_cfg) {
        wlr_log(WLR_ERROR, "Failed to reload config from %s", config_path.c_str());
        return;
    }


    IniConfig *old_cfg = config;


    config = new IniConfig(std::move(*new_cfg));


    std::string wp = config->get("wallpaper", "path");
    for (auto *output : outputs) {
        struct wlr_output *wlr_output = output->wlr_output;
        int bw, bh;
        wlr_output_effective_resolution(wlr_output, &bw, &bh);
        double ox = 0.0, oy = 0.0;
        wlr_output_layout_output_coords(output_layout, wlr_output, &ox, &oy);

        if (output->background) {
            wlr_scene_node_destroy(output->background);
            output->background = nullptr;
        }

        struct wlr_scene_buffer *wallpaper =
            wp.empty() ? nullptr : wallpaper_create(this, wp.c_str(), bw, bh);
        if (wallpaper) {
            wlr_scene_node_set_position(&wallpaper->node, ox, oy);
            output->background = &wallpaper->node;
        } else {
            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            struct wlr_scene_rect *bg =
                wlr_scene_rect_create(scene_background, bw, bh, white);
            wlr_scene_node_set_position(&bg->node, ox, oy);
            output->background = &bg->node;
        }
    }


    arrange();


    for (auto *v : views) {
        corners_update(this, v);
    }


    delete old_cfg;

    wlr_log(WLR_INFO, "Configuration reloaded successfully from %s", config_path.c_str());
}





static const int CLOSE_SLIDE = 20;

bool Server::animations_enabled() const {
    return config ? config->get_bool("animations", "enabled", true) : true;
}

uint64_t Server::animation_duration_ns() const {
    int d = config ? config->get_int("animations", "duration", 300) : 300;
    return (uint64_t)std::max(d, 0) * 1000000ULL;
}

Easing Server::easing() const {
    if (!config) return Easing::EaseOut;
    return easing_from_string(config->get("animations", "curve", "ease-out").c_str(),
                              Easing::EaseOut);
}

void Server::animate(uint64_t now) {
    for (auto *v : views) {
        if (v->geo.active) {
            int x, y, w, h;
            bool done = v->geo.step(now, &x, &y, &w, &h);
            if (v->closing) {


                wlr_scene_node_set_position(&v->scene_tree->node, x, y);
            } else {
                view_apply_box(v, x, y, w, h);
            }
            v->gx = x; v->gy = y; v->gw = w; v->gh = h;
            corners_update(this, v);
            if (done) {
                v->geo.active = false;
            }
        }
        if (v->opening && !v->closing) {



            struct wlr_scene_buffer *buf = v->scene_buffer();
            bool ready = buf && buf->buffer &&
                         buf->buffer->width == v->gw &&
                         buf->buffer->height == v->gh;
            uint64_t dur = animation_duration_ns();
            bool stuck = v->opening_start &&
                         (now - v->opening_start) > 2 * dur;
            if (!ready && !stuck) {
                if (buf) wlr_scene_buffer_set_opacity(buf, 0.0f);
                corners_opacity(v, 0.0f);
                continue;
            }
            if (!v->fade.active) {
                v->fade.begin(now, dur, easing(), 0.0f, 1.0f);
            }
        }
        if (v->fade.active) {
            float o;
            bool done = v->fade.step(now, &o);
            struct wlr_scene_buffer *buf = v->scene_buffer();
            if (buf) wlr_scene_buffer_set_opacity(buf, o);
            corners_opacity(v, o);
            if (done) {
                v->fade.active = false;
                if (v->closing) {
                    v->closing = false;
                    v->opening = false;
                    if (v->xdg_surface && v->xdg_surface->toplevel) {
                        wlr_xdg_toplevel_send_close(v->xdg_surface->toplevel);
                    } else if (v->xsurface) {
                        wlr_xwayland_surface_close(v->xsurface);
                    }
                } else {
                    v->opening = false;
                }
            }
        }
    }
}

void Server::close_view(View *view) {
    if (!view || view->closing) return;

    if (!animations_enabled()) {
        if (view->xdg_surface && view->xdg_surface->toplevel) {
            wlr_xdg_toplevel_send_close(view->xdg_surface->toplevel);
        } else if (view->xsurface) {
            wlr_xwayland_surface_close(view->xsurface);
        }
        return;
    }

    view->closing = true;
    view->opening = false;
    view->geo.active = false;

    uint64_t dur = animation_duration_ns();
    Easing e = easing();
    view->fade.begin(anim_now_ns(), dur, e, 1.0f, 0.0f);
    view->geo.begin(anim_now_ns(), dur, e,
                    view->gx, view->gy, view->gw, view->gh,
                    view->gx, view->gy + CLOSE_SLIDE, view->gw, view->gh);

    struct wlr_surface *focused = seat->keyboard_state.focused_surface;
    if (focused && focused == view->wlr_surface()) {
        wlr_seat_keyboard_clear_focus(seat);
    }





    for (auto *o : outputs) {
        if (o && o->wlr_output) {
            wlr_output_schedule_frame(o->wlr_output);
        }
    }




    arrange();
}
