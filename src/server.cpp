#include "vlswm/wlroots.h"
#include "vlswm/server.h"
#include "vlswm/view.h"
#include "vlswm/launcher.h"
#include "vlswm/theme.h"

#include <xkbcommon/xkbcommon.h>
#include <linux/input.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

extern char **environ;

static const uint32_t MODS_MASK = WLR_MODIFIER_LOGO | WLR_MODIFIER_SHIFT |
                                  WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT;

View *desktop_view_at(Server *server, double lx, double ly,
                      struct wlr_surface **surface, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) return nullptr;
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) return nullptr;
    *surface = scene_surface->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree->node.data == nullptr) {
        tree = tree->node.parent;
    }
    return tree ? (View *)tree->node.data : nullptr;
}

static std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t");
    return s.substr(b, e - b + 1);
}

static std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string expand_home(const std::string &path) {
    if (path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        const char *home = std::getenv("HOME");
        return std::string(home ? home : "") + path.substr(1);
    }
    return path;
}

static xkb_keysym_t parse_key(const std::string &name) {
    std::string n = lower(name);
    if (n == "enter" || n == "return") return XKB_KEY_Return;
    if (n == "esc" || n == "escape") return XKB_KEY_Escape;
    if (n == "space") return XKB_KEY_space;
    if (n == "tab") return XKB_KEY_Tab;
    return xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
}

static bool parse_combo(const std::string &text, uint32_t *mods, xkb_keysym_t *sym) {
    *mods = 0;
    std::vector<std::string> parts;
    size_t start = 0;
    std::string rest = trim(text);
    while (true) {
        size_t p = rest.find('+', start);
        if (p == std::string::npos) {
            parts.push_back(trim(rest.substr(start)));
            break;
        }
        parts.push_back(trim(rest.substr(start, p - start)));
        start = p + 1;
    }
    if (parts.empty()) return false;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        std::string m = lower(parts[i]);
        if (m == "super" || m == "logo" || m == "mod4") *mods |= WLR_MODIFIER_LOGO;
        else if (m == "shift") *mods |= WLR_MODIFIER_SHIFT;
        else if (m == "ctrl" || m == "control") *mods |= WLR_MODIFIER_CTRL;
        else if (m == "alt" || m == "mod1") *mods |= WLR_MODIFIER_ALT;
        else return false;
    }
    *sym = parse_key(parts.back());
    return *sym != XKB_KEY_NoSymbol;
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

pid_t Server::spawn_argv(const std::vector<std::string> &argv) {
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    std::vector<char *> args;
    for (const auto &a : argv) args.push_back((char *)a.c_str());
    args.push_back(nullptr);
    pid_t pid = 0;
    int r = posix_spawnp(&pid, args[0], nullptr, &attr, args.data(), environ);
    posix_spawnattr_destroy(&attr);
    if (r != 0) {
        wlr_log(WLR_ERROR, "cannot run '%s': %s", args[0], strerror(r));
        return 0;
    }
    children.push_back(pid);
    return pid;
}

void Server::spawn(const std::string &cmd) {
    spawn_argv({"/bin/sh", "-c", cmd});
}

void Server::spawn_file_manager() {
    if (settings.terminal.empty() || settings.file_manager.empty()) return;
    pid_t pid = spawn_argv({settings.terminal, "-e", settings.file_manager});
    if (pid > 0) extra_terminals.push_back(pid);
}

bool Server::take_extra_terminal(pid_t pid) {
    for (auto it = extra_terminals.begin(); it != extra_terminals.end(); ++it) {
        if (*it == pid) {
            extra_terminals.erase(it);
            return true;
        }
    }
    return false;
}

static void reap_children(Server *server) {
    auto &kids = server->children;
    kids.erase(std::remove_if(kids.begin(), kids.end(), [](pid_t pid) {
        return waitpid(pid, nullptr, WNOHANG) != 0;
    }), kids.end());
}

static int on_sigchld(int signal_number, void *data) {
    (void)signal_number;
    reap_children((Server *)data);
    return 0;
}

static int on_respawn_timer(void *data) {
    Server *server = (Server *)data;
    server->start_terminal();
    return 0;
}

static int on_repeat_timer(void *data) {
    Server *server = (Server *)data;
    const Repeat &r = server->repeat;
    if (r.kind == RepeatKind::Binding && r.binding >= 0) {
        server->run_action(r.binding);
    } else if (r.kind == RepeatKind::LauncherKey && server->launcher->open) {
        server->launcher->key(r.sym, r.utf8);
    } else {
        return 0;
    }
    wl_event_source_timer_update(server->repeat_timer, 1000 / server->settings.repeat_rate);
    return 0;
}

void Server::start_repeat(const Repeat &r) {
    if (!repeat_timer) {
        repeat_timer = wl_event_loop_add_timer(wl_display_get_event_loop(wl_display),
                                               on_repeat_timer, this);
    }
    repeat = r;
    wl_event_source_timer_update(repeat_timer, settings.repeat_delay);
}

void Server::stop_repeat(uint32_t keycode) {
    if (repeat.kind == RepeatKind::None || keycode != repeat.keycode) return;
    repeat = Repeat{};
    if (repeat_timer) wl_event_source_timer_update(repeat_timer, 0);
}

void Server::start_terminal() {
    for (auto *v : stack) {
        if (v->terminal) return;
    }
    if (settings.terminal.empty()) return;
    pid_t pid = spawn_argv({settings.terminal});
    if (pid > 0) base_pid = pid;
}

void Server::terminal_gone() {
    base_pid = 0;
    if (!respawn_timer) {
        respawn_timer = wl_event_loop_add_timer(wl_display_get_event_loop(wl_display),
                                                on_respawn_timer, this);
    }
    wl_event_source_timer_update(respawn_timer, 500);
}

void Server::start_satellite() {
    if (settings.satellite.empty()) return;
    setenv("DISPLAY", settings.satellite_display.c_str(), true);
    satellite_pid = spawn_argv({settings.satellite, settings.satellite_display});
}

static void apply_repeat_settings(Server *server) {
    if (server->keyboard) {
        wlr_keyboard_set_repeat_info(server->keyboard, server->settings.repeat_rate,
                                     server->settings.repeat_delay);
    }
}

static void keyboard_handle_key(wl_listener *listener, void *data) {
    Keyboard *kb = wl_container_of(listener, kb, key);
    Server *server = kb->server;
    struct wlr_keyboard *keyboard = wlr_keyboard_from_input_device(kb->device);
    struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;

    uint32_t keycode = event->keycode + 8;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard);
    bool consumed = kb->swallowed && kb->swallowed_keycode == event->keycode;

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        xkb_layout_index_t layout = xkb_state_key_get_layout(keyboard->xkb_state, keycode);
        const xkb_keysym_t *base = nullptr;
        int n = xkb_keymap_key_get_syms_by_level(keyboard->keymap, keycode, layout, 0, &base);
        kb->swallowed = false;

        for (int i = 0; i < n && !kb->swallowed; i++) {
            int idx = server->find_binding(base[i], modifiers);
            if (idx < 0) continue;
            server->run_action(idx);
            kb->swallowed = true;
            kb->swallowed_keycode = event->keycode;
            if (server->bindings[idx].repeat) {
                Repeat r;
                r.kind = RepeatKind::Binding;
                r.binding = idx;
                r.keycode = event->keycode;
                server->start_repeat(r);
            }
        }

        if (!kb->swallowed && server->launcher->open &&
            !(modifiers & (WLR_MODIFIER_LOGO | WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT))) {
            char buf[32] = {0};
            xkb_state_key_get_utf8(keyboard->xkb_state, keycode, buf, sizeof(buf));
            std::string utf8 = buf;
            xkb_keysym_t sym = n > 0 ? base[0] : XKB_KEY_NoSymbol;
            server->launcher->key(sym, utf8);
            kb->swallowed = true;
            kb->swallowed_keycode = event->keycode;
            Repeat r;
            r.kind = RepeatKind::LauncherKey;
            r.sym = sym;
            r.utf8 = utf8;
            r.keycode = event->keycode;
            server->start_repeat(r);
        }
    } else if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED) {
        server->stop_repeat(event->keycode);
    }

    wlr_seat_set_keyboard(server->seat, keyboard);

    if (consumed) {
        kb->swallowed = false;
        return;
    }
    if (kb->swallowed && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        return;
    }

    wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
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
    struct xkb_rule_names names = {};
    names.layout = server->settings.layout.empty() ? nullptr : server->settings.layout.c_str();
    names.options = server->settings.layout_options.empty() ? nullptr
                                                            : server->settings.layout_options.c_str();
    struct xkb_keymap *keymap =
        xkb_keymap_new_from_names(context, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(keyboard, server->settings.repeat_rate,
                                 server->settings.repeat_delay);

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
        libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
        if (libinput_device_config_tap_get_button_map(dev) != LIBINPUT_CONFIG_TAP_MAP_LRM) {
            libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
        }
    }
    if (libinput_device_config_scroll_get_methods(dev) & LIBINPUT_CONFIG_SCROLL_2FG) {
        libinput_device_config_scroll_set_method(dev, LIBINPUT_CONFIG_SCROLL_2FG);
        if (libinput_device_config_scroll_has_natural_scroll(dev)) {
            libinput_device_config_scroll_set_natural_scroll_enabled(dev, false);
        }
    }
}

static void handle_new_input(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = (struct wlr_input_device *)data;

    switch (device->type) {
        case WLR_INPUT_DEVICE_POINTER:
            server->pointer_device = device;
            configure_libinput(device);
            wlr_cursor_attach_input_device(server->cursor, device);
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
    if (!output) return;

    wlr_output_layout_add_auto(server->output_layout, wlr_output);
    wlr_scene_output_layout_add_output(
        wlr_scene_attach_output_layout(server->scene, server->output_layout),
        wlr_output_layout_get(server->output_layout, wlr_output), output->scene_output);
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);

    int w = 0, h = 0;
    wlr_output_effective_resolution(wlr_output, &w, &h);
    wlr_scene_rect_set_size(server->background, w, h);
    server->launcher->relayout();
    server->configure_all();
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
        wlr_output_effective_resolution(layer->output, &full_area.width, &full_area.height);
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
    ls->server->focus_top();
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
}

struct PopupWatcher {
    Server *server;
    struct wlr_scene_tree *parent;
    struct wl_listener new_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void create_xdg_popup(Server *server, struct wlr_xdg_surface *xdg_surface);

static void popup_watcher_on_commit(wl_listener *listener, void *data) {
    PopupWatcher *w = wl_container_of(listener, w, commit);
    struct wlr_surface *surf = (struct wlr_surface *)data;
    struct wlr_xdg_surface *s = wlr_xdg_surface_try_from_wlr_surface(surf);
    if (!s || s->role != WLR_XDG_SURFACE_ROLE_POPUP || !s->popup || !s->initial_commit) return;
    if (!w->server->outputs.empty() && w->parent) {
        int ow = 0, oh = 0;
        wlr_output_effective_resolution(w->server->outputs.front()->wlr_output, &ow, &oh);
        int px = 0, py = 0;
        wlr_scene_node_coords(&w->parent->node, &px, &py);
        struct wlr_box box = {-px, -py, ow, oh};
        wlr_xdg_popup_unconstrain_from_box(s->popup, &box);
    }
    wlr_xdg_surface_schedule_configure(s);
}

static void popup_watcher_on_new_popup(wl_listener *listener, void *data) {
    PopupWatcher *w = wl_container_of(listener, w, new_popup);
    struct wlr_xdg_popup *popup = (struct wlr_xdg_popup *)data;
    create_xdg_popup(w->server, popup->base);
}

static void popup_watcher_on_destroy(wl_listener *listener, void *data) {
    (void)data;
    PopupWatcher *w = wl_container_of(listener, w, destroy);
    wl_list_remove(&w->new_popup.link);
    wl_list_remove(&w->commit.link);
    wl_list_remove(&w->destroy.link);
    delete w;
}

static void watch_xdg_popups(Server *server, struct wlr_xdg_surface *xdg_surface,
                             struct wlr_scene_tree *parent) {
    auto *w = new PopupWatcher;
    w->server = server;
    w->parent = parent;
    w->new_popup.notify = popup_watcher_on_new_popup;
    w->commit.notify = popup_watcher_on_commit;
    w->destroy.notify = popup_watcher_on_destroy;
    wl_signal_add(&xdg_surface->events.new_popup, &w->new_popup);
    wl_signal_add(&xdg_surface->surface->events.commit, &w->commit);
    wl_signal_add(&xdg_surface->events.destroy, &w->destroy);
}

static void create_xdg_popup(Server *server, struct wlr_xdg_surface *xdg_surface) {
    if (!xdg_surface || xdg_surface->role != WLR_XDG_SURFACE_ROLE_POPUP) return;
    struct wlr_xdg_popup *popup = xdg_surface->popup;
    struct wlr_scene_tree *parent = server->scene_apps;

    struct wlr_surface *anc = popup ? popup->parent : nullptr;
    while (anc) {
        struct wlr_xdg_surface *as = wlr_xdg_surface_try_from_wlr_surface(anc);
        if (as && as->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
            for (auto *v : server->stack) {
                if (v->xdg_surface == as && v->scene_tree) {
                    parent = v->scene_tree;
                    break;
                }
            }
            break;
        }
        if (as && as->popup) {
            anc = as->popup->parent;
            continue;
        }
        break;
    }

    struct wlr_scene_tree *node = wlr_scene_xdg_surface_create(parent, xdg_surface);
    if (!node) return;
    xdg_surface->data = node;
    watch_xdg_popups(server, xdg_surface, parent);
}

static void handle_new_xdg_toplevel(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_xdg_surface);
    struct wlr_xdg_toplevel *toplevel = (struct wlr_xdg_toplevel *)data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    struct wlr_scene_tree *scene_tree = wlr_scene_xdg_surface_create(server->scene_apps, xdg_surface);
    auto *view = new View(server, xdg_surface, scene_tree);
    scene_tree->node.data = view;
    wlr_scene_node_set_enabled(&scene_tree->node, false);

    watch_xdg_popups(server, xdg_surface, nullptr);
}

struct DecorationWatch {
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener request_mode;
    struct wl_listener destroy;
};

static void decoration_force_server_side(struct wlr_xdg_toplevel_decoration_v1 *decoration) {
    bool ready = decoration->toplevel && decoration->toplevel->base &&
                 decoration->toplevel->base->initialized;
    if (ready) {
        wlr_xdg_toplevel_decoration_v1_set_mode(decoration,
            WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        decoration->scheduled_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    }
}

static void decoration_request_mode(wl_listener *listener, void *data) {
    (void)data;
    DecorationWatch *w = wl_container_of(listener, w, request_mode);
    decoration_force_server_side(w->decoration);
}

static void decoration_destroy(wl_listener *listener, void *data) {
    (void)data;
    DecorationWatch *w = wl_container_of(listener, w, destroy);
    wl_list_remove(&w->request_mode.link);
    wl_list_remove(&w->destroy.link);
    delete w;
}

static void handle_new_decoration(wl_listener *listener, void *data) {
    (void)listener;
    auto *decoration = (struct wlr_xdg_toplevel_decoration_v1 *)data;
    auto *w = new DecorationWatch;
    w->decoration = decoration;
    w->request_mode.notify = decoration_request_mode;
    w->destroy.notify = decoration_destroy;
    wl_signal_add(&decoration->events.request_mode, &w->request_mode);
    wl_signal_add(&decoration->events.destroy, &w->destroy);
    decoration_force_server_side(decoration);
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
    for (auto *v : server->stack) {
        if (v->wlr_surface() == event->surface) {
            server->focus_view(v, event->surface);
            return;
        }
    }
}

static void process_cursor_motion(Server *server, uint32_t time_msec) {
    double sx = 0, sy = 0;
    struct wlr_surface *surface = nullptr;
    View *view = desktop_view_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (view && surface) {
        if (surface != server->seat->pointer_state.focused_surface) {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
            wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        }
        wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
    } else if (server->seat->pointer_state.focused_surface) {
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
        struct libinput_device *ldev = wlr_libinput_get_device_handle(server->pointer_device);
        if (ldev && libinput_device_get_id_vendor(ldev) == 0x05ac &&
            libinput_device_get_id_product(ldev) == 0x0291) {
            return;
        }
    }

    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        double sx = 0, sy = 0;
        struct wlr_surface *surface = nullptr;
        View *view = desktop_view_at(server, server->cursor->x, server->cursor->y,
                                     &surface, &sx, &sy);
        if (view && surface) server->focus_view(view, view->wlr_surface());
    }

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void handle_cursor_axis(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = (struct wlr_pointer_axis_event *)data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
                                 event->delta * server->settings.scroll_speed,
                                 event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void handle_cursor_frame(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, cursor_frame);
    (void)data;
    wlr_seat_pointer_notify_frame(server->seat);
}

bool Server::init(std::unique_ptr<IniConfig> cfg) {
    float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    const char *socket_name = nullptr;
    config = std::move(cfg);
    check_config();
    load_settings();
    load_bindings();

    wl_display = wl_display_create();
    if (!wl_display) return false;

    struct wlr_session *session = nullptr;
    backend = wlr_backend_autocreate(wl_display_get_event_loop(wl_display), &session);
    if (!backend) {
        wlr_log(WLR_ERROR, "failed to create backend");
        goto fail;
    }

    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "failed to create renderer");
        goto fail;
    }
    wlr_renderer_init_wl_display(renderer, wl_display);

    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "failed to create allocator");
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
    decoration_mgr = wlr_xdg_decoration_manager_v1_create(wl_display);
    wlr_pointer_gestures_v1_create(wl_display);
    wlr_text_input_manager_v3_create(wl_display);
    wlr_presentation_create(wl_display, backend, 1);
    xdg_activation = wlr_xdg_activation_v1_create(wl_display);

    wlr_data_device_manager_create(wl_display);
    wlr_data_control_manager_v1_create(wl_display);
    wlr_primary_selection_v1_device_manager_create(wl_display);

    output_layout = wlr_output_layout_create(wl_display);
    wlr_xdg_output_manager_v1_create(wl_display, output_layout);

    scene = wlr_scene_create();
    scene_background = wlr_scene_tree_create(&scene->tree);
    background = wlr_scene_rect_create(scene_background, 1, 1, black);
    scene_apps = wlr_scene_tree_create(&scene->tree);
    scene_overlay = wlr_scene_tree_create(&scene->tree);
    scene_layers = wlr_scene_tree_create(&scene->tree);

    launcher = new Launcher;
    launcher->init(this);

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
    if (xdg_activation) {
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
    new_decoration.notify = handle_new_decoration;
    wl_signal_add(&decoration_mgr->events.new_toplevel_decoration, &new_decoration);

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

    wl_event_loop_add_signal(wl_display_get_event_loop(wl_display), SIGCHLD, on_sigchld, this);

    socket_name = wl_display_add_socket_auto(wl_display);
    if (!socket_name) {
        wlr_log(WLR_ERROR, "failed to add Wayland socket");
        goto fail;
    }
    setenv("WAYLAND_DISPLAY", socket_name, true);
    setenv("XDG_CURRENT_DESKTOP", "vlswm", true);
    setenv("DESKTOP_SESSION", "vlswm", true);

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "failed to start backend");
        goto fail;
    }

    ghostty_setup_theme();
    start_satellite();
    start_terminal();
    return true;

fail:
    finish();
    return false;
}

void Server::ghostty_setup_theme() {
    theme_base = theme_read_base(settings.ghostty_dir);
    if (theme_base.empty()) {
        wlr_log(WLR_ERROR, "theme: no ghostty colors found in '%s'", settings.ghostty_dir.c_str());
        return;
    }
    theme_ensure_include(settings.ghostty_dir);
    theme_write(settings.ghostty_dir, theme_base, theme_white);
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
        wl_list_remove(&new_decoration.link);
        wl_list_remove(&cursor_motion.link);
        wl_list_remove(&cursor_motion_absolute.link);
        wl_list_remove(&cursor_button.link);
        wl_list_remove(&cursor_axis.link);
        wl_list_remove(&cursor_frame.link);
        wl_list_remove(&request_set_selection.link);
        wl_list_remove(&request_set_primary_selection.link);
        wl_list_remove(&request_start_drag.link);
        if (xdg_activation) wl_list_remove(&request_activate.link);
        if (repeat_timer) {
            wl_event_source_remove(repeat_timer);
            repeat_timer = nullptr;
        }
        if (respawn_timer) {
            wl_event_source_remove(respawn_timer);
            respawn_timer = nullptr;
        }

        for (pid_t pid : children) {
            if (pid > 0) kill(-pid, SIGTERM);
        }
        for (pid_t pid : extra_terminals) {
            if (pid > 0) kill(-pid, SIGTERM);
        }
        if (base_pid > 0) kill(-base_pid, SIGTERM);
        if (satellite_pid > 0) kill(-satellite_pid, SIGTERM);
        for (int i = 0; i < 20; i++) {
            bool alive = false;
            for (pid_t pid : children) {
                if (pid > 0 && waitpid(pid, nullptr, WNOHANG) == 0) alive = true;
            }
            if (!alive) break;
            usleep(10000);
        }

        wl_display_destroy_clients(wl_display);
        wl_display_destroy(wl_display);
        wl_display = nullptr;
    }
    delete launcher;
    launcher = nullptr;
}

void Server::load_settings() {
    settings.duration_ns = (uint64_t)std::max(config->get_int("animations", "duration", 300), 0) *
                           1000000ULL;
    settings.easing = easing_from_string(config->get("animations", "curve", "ease-out").c_str(),
                                         Easing::EaseOut);
    settings.repeat_delay = std::clamp(config->get_int("input", "repeat_delay", 200), 50, 2000);
    settings.repeat_rate = std::clamp(config->get_int("input", "repeat_rate", 40), 1, 200);
    settings.layout = config->get("input", "layout", "us,ru");
    settings.layout_options = config->get("input", "layout_options", "grp:win_space_toggle");
    try {
        settings.scroll_speed = std::clamp(std::stod(config->get("input", "scroll_speed", "0.7")),
                                           0.1, 2.0);
    } catch (...) {
        settings.scroll_speed = 0.7;
    }
    settings.terminal = config->get("launcher", "terminal", "ghostty");
    settings.file_manager = config->get("launcher", "file_manager", "yazi");
    settings.font = config->get("launcher", "font", "JetBrainsMono Nerd Font 16");
    settings.satellite = config->get("xwayland", "satellite", "xwayland-satellite");
    settings.satellite_display = config->get("xwayland", "display", ":1");
    settings.ghostty_dir = expand_home(config->get("theme", "ghostty", "~/.config/ghostty"));
}

void Server::check_config() {
    static const char *KNOWN[] = {
        "animations.duration", "animations.curve",
        "input.repeat_delay", "input.repeat_rate", "input.repeat",
        "input.layout", "input.layout_options", "input.scroll_speed",
        "launcher.terminal", "launcher.file_manager", "launcher.font",
        "xwayland.satellite", "xwayland.display",
        "theme.ghostty",
    };
    for (const auto &kv : config->values) {
        if (kv.first.rfind("keybinds.", 0) == 0 || kv.first.rfind("commands.", 0) == 0) continue;
        bool known = false;
        for (const char *k : KNOWN) {
            if (kv.first == k) {
                known = true;
                break;
            }
        }
        if (!known) wlr_log(WLR_ERROR, "config: unknown key '%s' (ignored)", kv.first.c_str());
    }
}

static bool is_repeat_action(const std::string &name, const std::string &list) {
    std::stringstream ss(list);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (trim(item) == name) return true;
    }
    return false;
}

void Server::load_bindings() {
    bindings.clear();
    const std::string prefix = "keybinds.";
    const std::string repeat_list = config->get("input", "repeat",
        "focus_next, focus_prev, volume_up, volume_down, brightness_up, brightness_down");

    for (const auto &kv : config->values) {
        if (kv.first.rfind(prefix, 0) != 0) continue;
        std::string action = kv.first.substr(prefix.size());

        BindAction type = BindAction::Spawn;
        std::string cmd;
        if (action == "launcher") type = BindAction::Launcher;
        else if (action == "file_manager") type = BindAction::FileManager;
        else if (action == "close") type = BindAction::Close;
        else if (action == "reload") type = BindAction::Reload;
        else if (action == "exit") type = BindAction::Exit;
        else if (action == "theme") type = BindAction::Theme;
        else if (action == "focus_next") type = BindAction::FocusNext;
        else if (action == "focus_prev") type = BindAction::FocusPrev;
        else if (config->values.count("commands." + action)) {
            cmd = config->values.at("commands." + action);
        } else {
            wlr_log(WLR_ERROR, "config: unknown action '%s' in [keybinds]", action.c_str());
            continue;
        }

        std::stringstream ss(kv.second);
        std::string combo;
        while (std::getline(ss, combo, ',')) {
            Binding b;
            b.action = type;
            b.cmd = cmd;
            b.repeat = is_repeat_action(action, repeat_list);
            if (!parse_combo(combo, &b.mods, &b.sym)) {
                wlr_log(WLR_ERROR, "config: bad key '%s' for '%s'", trim(combo).c_str(),
                        action.c_str());
                continue;
            }
            bindings.push_back(b);
        }
    }
}

int Server::find_binding(xkb_keysym_t sym, uint32_t mods) const {
    for (size_t i = 0; i < bindings.size(); i++) {
        const auto &b = bindings[i];
        if (b.sym == sym && b.mods == (mods & MODS_MASK)) return (int)i;
    }
    return -1;
}

void Server::run_action(int idx) {
    const Binding &b = bindings[idx];
    switch (b.action) {
        case BindAction::Close: close_top(); break;
        case BindAction::Exit: wl_display_terminate(wl_display); break;
        case BindAction::Reload: reload_config(); break;
        case BindAction::FocusNext: cycle_next(); break;
        case BindAction::FocusPrev: cycle_prev(); break;
        case BindAction::Theme: set_theme(!theme_white); break;
        case BindAction::Launcher: launcher->toggle(); break;
        case BindAction::FileManager: spawn_file_manager(); break;
        case BindAction::Spawn: spawn(b.cmd); break;
    }
}

View *Server::top_view() {
    return stack.empty() ? nullptr : stack.back();
}

View *Server::focused_view() {
    if (!seat) return nullptr;
    struct wlr_surface *focused = seat->keyboard_state.focused_surface;
    if (!focused) return nullptr;
    for (auto *v : stack) {
        if (v->wlr_surface() == focused) return v;
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

    for (auto *v : stack) {
        if (v != view && v->wlr_surface() == prev && v->xdg_surface->toplevel) {
            wlr_xdg_toplevel_set_activated(v->xdg_surface->toplevel, false);
        }
    }
    if (view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
    }
    wlr_scene_node_raise_to_top(&view->scene_tree->node);

    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes,
                                       keyboard->num_keycodes, &keyboard->modifiers);
    }
}

void Server::focus_top() {
    View *top = top_view();
    if (top && top->wlr_surface()) focus_view(top, top->wlr_surface());
    else wlr_seat_keyboard_clear_focus(seat);
}

void Server::update_visibility() {
    size_t n = stack.size();
    for (size_t i = 0; i < n; i++) {
        bool top = i + 1 == n;
        bool under_closing = i + 2 == n && stack[n - 1]->closing;
        wlr_scene_node_set_enabled(&stack[i]->scene_tree->node, top || under_closing);
    }
}

void Server::cycle_next() {
    if (stack.size() < 2 || stack.back()->closing) return;
    View *top = stack.back();
    stack.pop_back();
    stack.insert(stack.begin(), top);
    focus_top();
    update_visibility();
    request_frame();
}

void Server::cycle_prev() {
    if (stack.size() < 2 || stack.back()->closing) return;
    View *bottom = stack.front();
    stack.erase(stack.begin());
    stack.push_back(bottom);
    focus_top();
    update_visibility();
    request_frame();
}

void Server::close_top() {
    View *top = top_view();
    if (top) close_view(top);
}

void Server::close_view(View *view) {
    if (!view || view->terminal || view->closing) return;
    view->closing = true;
    if (seat->keyboard_state.focused_surface == view->wlr_surface()) {
        wlr_seat_keyboard_clear_focus(seat);
    }
    view->fade.begin(anim_now_ns(), settings.duration_ns, settings.easing, 1.0f, 0.0f);
    update_visibility();
    request_frame();
}

void Server::set_theme(bool white) {
    theme_white = white;
    float c = white ? 1.0f : 0.0f;
    float color[4] = {c, c, c, 1.0f};
    wlr_scene_rect_set_color(background, color);
    launcher->relayout();
    if (!theme_base.empty()) {
        theme_write(settings.ghostty_dir, theme_base, white);
        if (base_pid > 0) kill(base_pid, SIGUSR2);
    }
    request_frame();
}

void Server::reload_config() {
    if (!config) return;
    config = std::make_unique<IniConfig>(IniConfig::load_or_empty(config->path()));
    check_config();
    load_settings();
    load_bindings();
    apply_repeat_settings(this);
    set_theme(theme_white);
    launcher->relayout();
    configure_all();
}

void Server::configure_all() {
    for (auto *v : stack) v->configure_size();
}

void Server::request_frame() {
    for (auto *o : outputs) {
        if (o->wlr_output) wlr_output_schedule_frame(o->wlr_output);
    }
}

bool Server::animations_running() const {
    if (launcher && launcher->fade.active) return true;
    for (auto *v : stack) {
        if (v->fade.active) return true;
    }
    return false;
}

void Server::animate(uint64_t now) {
    if (launcher) launcher->animate(now);

    for (size_t i = 0; i < stack.size(); i++) {
        View *v = stack[i];
        if (!v->fade.active) continue;
        float o = 1.0f;
        bool done = v->fade.step(now, &o);
        v->set_opacity(o);
        if (done && v->closing && v->xdg_surface && v->xdg_surface->toplevel) {
            wlr_xdg_toplevel_send_close(v->xdg_surface->toplevel);
        }
    }
}
