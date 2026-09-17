
#include "vlswm/server.h"
#include "vlswm/wallpaper.h"
#include "vlswm/ini.h"

#include <string>

static void output_frame(wl_listener *listener, void *data) {
    (void)data;
    Output *output = wl_container_of(listener, output, frame);
    struct wlr_scene_output *scene_output = output->scene_output;

    Server *server = (Server *)output->wlr_output->data;
    server->animate(anim_now_ns());

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct wlr_scene_output_state_options options = {};
    wlr_scene_output_commit(scene_output, &options);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(wl_listener *listener, void *data) {
    Output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event =
        (const struct wlr_output_event_request_state *)data;
    wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(wl_listener *listener, void *data) {
    (void)data;
    Output *output = wl_container_of(listener, output, destroy);
    Server *server = (Server *)output->wlr_output->data;

    if (output->background) {
        wlr_scene_node_destroy(output->background);
        output->background = nullptr;
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);

    server->outputs.remove(output);
    delete output;
}

Output *create_output(Server *server, struct wlr_output *wlr_output) {
    auto *output = new Output;
    output->wlr_output = wlr_output;
    wlr_output->data = server;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        wlr_output_state_set_mode(&state, mode);
    }
    wlr_output_state_set_scale(&state, 1.0f);
    wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);

    if (!wlr_output_commit_state(wlr_output, &state)) {
        wlr_log(WLR_ERROR, "Failed to commit output %s", wlr_output->name);
        wlr_output_state_finish(&state);
        delete output;
        return nullptr;
    }
    wlr_output_state_finish(&state);

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);

    output->request_state.notify = output_request_state;
    wl_signal_add(&wlr_output->events.request_state, &output->request_state);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);


    int bw, bh;
    wlr_output_effective_resolution(wlr_output, &bw, &bh);
    double ox = 0.0, oy = 0.0;
    wlr_output_layout_output_coords(server->output_layout, wlr_output, &ox, &oy);

    std::string wp;
    if (server->config) wp = server->config->get("wallpaper", "path");
    struct wlr_scene_buffer *wallpaper =
        wp.empty() ? nullptr : wallpaper_create(server, wp.c_str(), bw, bh);
    if (wallpaper) {
        wlr_scene_node_set_position(&wallpaper->node, ox, oy);
        output->background = &wallpaper->node;
    } else {
        float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        struct wlr_scene_rect *bg =
            wlr_scene_rect_create(server->scene_background, bw, bh, white);
        wlr_scene_node_set_position(&bg->node, ox, oy);
        output->background = &bg->node;
    }

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);

    server->outputs.push_back(output);
    return output;
}
