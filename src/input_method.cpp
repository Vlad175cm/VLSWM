#include "vlswm/input_method.h"
#include "vlswm/server.h"
#include "vlswm/view.h"

#include <algorithm>

static struct wlr_text_input_v3 *focused_text_input(Server *server) {
    for (auto *ti : server->text_inputs) {
        if (ti->input->focused_surface) return ti->input;
    }
    return nullptr;
}

static void send_im_state(Server *server, struct wlr_text_input_v3 *input) {
    struct wlr_input_method_v2 *im = server->input_method;
    if (!im) return;

    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_SURROUNDING_TEXT) {
        wlr_input_method_v2_send_surrounding_text(im,
            input->current.surrounding.text ? input->current.surrounding.text : "",
            input->current.surrounding.cursor, input->current.surrounding.anchor);
    }
    wlr_input_method_v2_send_text_change_cause(im, input->current.text_change_cause);
    if (input->active_features & WLR_TEXT_INPUT_V3_FEATURE_CONTENT_TYPE) {
        wlr_input_method_v2_send_content_type(im, input->current.content_type.hint,
                                              input->current.content_type.purpose);
    }
    wlr_input_method_v2_send_done(im);
}

static View *view_for_surface(Server *server, struct wlr_surface *surface) {
    for (auto *v : server->stack) {
        if (v->wlr_surface() == surface) return v;
    }
    return nullptr;
}

static void position_popup(Server *server, InputPopup *popup) {
    if (!popup->scene || !popup->popup->surface) return;

    struct wlr_text_input_v3 *input = focused_text_input(server);
    if (!input || !input->focused_surface) {
        wlr_scene_node_set_enabled(&popup->scene->node, false);
        return;
    }

    View *view = view_for_surface(server, input->focused_surface);
    int px = 0, py = 0;
    if (view) wlr_scene_node_coords(&view->scene_tree->node, &px, &py);

    struct wlr_box cursor = input->current.cursor_rectangle;
    int pw = popup->popup->surface->current.width;
    int ph = popup->popup->surface->current.height;
    int x = px + cursor.x;
    int y = py + cursor.y + cursor.height;

    int ow = 0, oh = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, &ow, &oh);
    }
    if (ow > 0 && x + pw > ow) x = ow - pw;
    if (oh > 0 && y + ph > oh) y = py + cursor.y - ph;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    wlr_scene_node_set_position(&popup->scene->node, x, y);
    wlr_scene_node_set_enabled(&popup->scene->node, true);

    if (input->current.features & WLR_TEXT_INPUT_V3_FEATURE_CURSOR_RECTANGLE) {
        struct wlr_box box = {px + cursor.x - x, py + cursor.y - y,
                              cursor.width, cursor.height};
        wlr_input_popup_surface_v2_send_text_input_rectangle(popup->popup, &box);
    }
}

static void popup_commit(wl_listener *listener, void *data) {
    (void)data;
    InputPopup *popup = wl_container_of(listener, popup, commit);
    position_popup(popup->server, popup);
}

static void popup_map(wl_listener *listener, void *data) {
    (void)data;
    InputPopup *popup = wl_container_of(listener, popup, map);
    position_popup(popup->server, popup);
}

static void popup_unmap(wl_listener *listener, void *data) {
    (void)data;
    InputPopup *popup = wl_container_of(listener, popup, unmap);
    if (popup->scene) wlr_scene_node_set_enabled(&popup->scene->node, false);
}

static void popup_destroy(wl_listener *listener, void *data) {
    (void)data;
    InputPopup *popup = wl_container_of(listener, popup, destroy);
    Server *server = popup->server;

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->map.link);
    wl_list_remove(&popup->unmap.link);
    wl_list_remove(&popup->destroy.link);
    if (popup->scene) wlr_scene_node_destroy(&popup->scene->node);

    auto it = std::find(server->input_popups.begin(), server->input_popups.end(), popup);
    if (it != server->input_popups.end()) server->input_popups.erase(it);
    delete popup;
}

static void handle_new_popup(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, im_new_popup);
    auto *surface = (struct wlr_input_popup_surface_v2 *)data;

    auto *popup = new InputPopup;
    popup->server = server;
    popup->popup = surface;
    popup->scene = wlr_scene_tree_create(server->scene_layers);
    if (!popup->scene || !wlr_scene_subsurface_tree_create(popup->scene, surface->surface)) {
        if (popup->scene) wlr_scene_node_destroy(&popup->scene->node);
        delete popup;
        return;
    }
    wlr_scene_node_set_enabled(&popup->scene->node, false);

    popup->commit.notify = popup_commit;
    wl_signal_add(&surface->surface->events.commit, &popup->commit);
    popup->map.notify = popup_map;
    wl_signal_add(&surface->surface->events.map, &popup->map);
    popup->unmap.notify = popup_unmap;
    wl_signal_add(&surface->surface->events.unmap, &popup->unmap);
    popup->destroy.notify = popup_destroy;
    wl_signal_add(&surface->events.destroy, &popup->destroy);

    server->input_popups.push_back(popup);
    position_popup(server, popup);
}

static void text_input_enable(wl_listener *listener, void *data) {
    (void)data;
    TextInput *ti = wl_container_of(listener, ti, enable);
    Server *server = ti->server;

    if (!ti->input->focused_surface) {
        struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
        if (focused && wl_resource_get_client(ti->input->resource) ==
                       wl_resource_get_client(focused->resource)) {
            wlr_text_input_v3_send_enter(ti->input, focused);
        }
    }
    if (ti->input->focused_surface && server->input_method) {
        wlr_input_method_v2_send_activate(server->input_method);
        send_im_state(server, ti->input);
    }
}

static void text_input_commit(wl_listener *listener, void *data) {
    (void)data;
    TextInput *ti = wl_container_of(listener, ti, commit);
    Server *server = ti->server;
    if (!ti->input->focused_surface || !ti->input->current_enabled) return;
    if (!server->input_method) return;

    send_im_state(server, ti->input);
    for (auto *popup : server->input_popups) position_popup(server, popup);
}

static void text_input_disable(wl_listener *listener, void *data) {
    (void)data;
    TextInput *ti = wl_container_of(listener, ti, disable);
    Server *server = ti->server;
    if (!ti->input->focused_surface || !server->input_method) return;

    wlr_input_method_v2_send_deactivate(server->input_method);
    send_im_state(server, ti->input);
}

static void text_input_destroy(wl_listener *listener, void *data) {
    (void)data;
    TextInput *ti = wl_container_of(listener, ti, destroy);
    Server *server = ti->server;

    if (ti->input->current_enabled && server->input_method) {
        wlr_input_method_v2_send_deactivate(server->input_method);
        send_im_state(server, ti->input);
    }
    wl_list_remove(&ti->enable.link);
    wl_list_remove(&ti->commit.link);
    wl_list_remove(&ti->disable.link);
    wl_list_remove(&ti->destroy.link);

    auto it = std::find(server->text_inputs.begin(), server->text_inputs.end(), ti);
    if (it != server->text_inputs.end()) server->text_inputs.erase(it);
    delete ti;
}

static void handle_new_text_input(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_text_input);
    auto *input = (struct wlr_text_input_v3 *)data;

    auto *ti = new TextInput;
    ti->server = server;
    ti->input = input;

    ti->enable.notify = text_input_enable;
    wl_signal_add(&input->events.enable, &ti->enable);
    ti->commit.notify = text_input_commit;
    wl_signal_add(&input->events.commit, &ti->commit);
    ti->disable.notify = text_input_disable;
    wl_signal_add(&input->events.disable, &ti->disable);
    ti->destroy.notify = text_input_destroy;
    wl_signal_add(&input->events.destroy, &ti->destroy);

    server->text_inputs.push_back(ti);
}

static void im_commit(wl_listener *listener, void *data) {
    (void)data;
    Server *server = wl_container_of(listener, server, im_commit);
    struct wlr_input_method_v2 *im = server->input_method;
    struct wlr_text_input_v3 *input = focused_text_input(server);
    if (!im || !input) return;

    if (im->current.preedit.text) {
        wlr_text_input_v3_send_preedit_string(input, im->current.preedit.text,
            im->current.preedit.cursor_begin, im->current.preedit.cursor_end);
    }
    if (im->current.commit_text) {
        wlr_text_input_v3_send_commit_string(input, im->current.commit_text);
    }
    if (im->current.delete_surrounding_text.before_length ||
        im->current.delete_surrounding_text.after_length) {
        wlr_text_input_v3_send_delete_surrounding_text(input,
            im->current.delete_surrounding_text.before_length,
            im->current.delete_surrounding_text.after_length);
    }
    wlr_text_input_v3_send_done(input);
}

static void grab_destroy(wl_listener *listener, void *data) {
    (void)data;
    Server *server = wl_container_of(listener, server, grab_destroy);
    struct wlr_input_method_keyboard_grab_v2 *grab = server->keyboard_grab;
    wl_list_remove(&server->grab_destroy.link);
    server->keyboard_grab = nullptr;

    if (grab && grab->keyboard) {
        wlr_seat_set_keyboard(server->seat, grab->keyboard);
        wlr_seat_keyboard_notify_modifiers(server->seat, &grab->keyboard->modifiers);
    }
}

static void im_grab_keyboard(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, im_grab_keyboard);
    auto *grab = (struct wlr_input_method_keyboard_grab_v2 *)data;

    server->keyboard_grab = grab;
    if (server->keyboard) wlr_input_method_keyboard_grab_v2_set_keyboard(grab, server->keyboard);
    server->grab_destroy.notify = grab_destroy;
    wl_signal_add(&grab->events.destroy, &server->grab_destroy);
}

static void im_destroy(wl_listener *listener, void *data) {
    (void)data;
    Server *server = wl_container_of(listener, server, im_destroy);
    wl_list_remove(&server->im_commit.link);
    wl_list_remove(&server->im_grab_keyboard.link);
    wl_list_remove(&server->im_new_popup.link);
    wl_list_remove(&server->im_destroy.link);
    server->input_method = nullptr;
}

static void handle_new_input_method(wl_listener *listener, void *data) {
    Server *server = wl_container_of(listener, server, new_input_method);
    auto *im = (struct wlr_input_method_v2 *)data;

    if (server->input_method) {
        wlr_input_method_v2_send_unavailable(im);
        return;
    }
    server->input_method = im;

    server->im_commit.notify = im_commit;
    wl_signal_add(&im->events.commit, &server->im_commit);
    server->im_grab_keyboard.notify = im_grab_keyboard;
    wl_signal_add(&im->events.grab_keyboard, &server->im_grab_keyboard);
    server->im_new_popup.notify = handle_new_popup;
    wl_signal_add(&im->events.new_popup_surface, &server->im_new_popup);
    server->im_destroy.notify = im_destroy;
    wl_signal_add(&im->events.destroy, &server->im_destroy);

    for (auto *ti : server->text_inputs) {
        if (ti->input->focused_surface && ti->input->current_enabled) {
            wlr_input_method_v2_send_activate(im);
            send_im_state(server, ti->input);
            break;
        }
    }
}

void input_method_set_focus(Server *server, struct wlr_surface *surface) {
    for (auto *ti : server->text_inputs) {
        if (ti->input->focused_surface) {
            if (ti->input->focused_surface == surface) continue;
            wlr_text_input_v3_send_leave(ti->input);
            if (server->input_method) wlr_input_method_v2_send_deactivate(server->input_method);
        }
        if (surface && wl_resource_get_client(ti->input->resource) ==
                       wl_resource_get_client(surface->resource)) {
            wlr_text_input_v3_send_enter(ti->input, surface);
        }
    }
    for (auto *popup : server->input_popups) position_popup(server, popup);
}

static void drop_listener(struct wl_listener *listener) {
    if (listener->link.prev && listener->link.next) wl_list_remove(&listener->link);
}

void input_method_init(Server *server) {
    if (!server->text_input_mgr || !server->input_method_mgr) return;
    server->new_text_input.notify = handle_new_text_input;
    wl_signal_add(&server->text_input_mgr->events.new_text_input, &server->new_text_input);
    server->new_input_method.notify = handle_new_input_method;
    wl_signal_add(&server->input_method_mgr->events.new_input_method, &server->new_input_method);
}

void input_method_finish(Server *server) {
    drop_listener(&server->new_text_input);
    drop_listener(&server->new_input_method);
    if (server->input_method) {
        drop_listener(&server->im_commit);
        drop_listener(&server->im_grab_keyboard);
        drop_listener(&server->im_new_popup);
        drop_listener(&server->im_destroy);
        server->input_method = nullptr;
    }
    if (server->keyboard_grab) {
        drop_listener(&server->grab_destroy);
        server->keyboard_grab = nullptr;
    }
}
