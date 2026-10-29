#include "vlswm/wlroots.h"
#include "vlswm/server.h"


#include <ctime>

static void damage_scene(Server *server) {
    if (!server->background) return;
    float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float *current = server->theme_white ? white : black;
    float *other = server->theme_white ? black : white;
    // wlroots only renders when the scene has pending damage. A screencopy
    // frame waits for an output commit, so damage the full-screen background
    // to force a frame while a capture is pending.
    wlr_scene_rect_set_color(server->background, other);
    wlr_scene_rect_set_color(server->background, current);
}

static void output_frame(wl_listener *listener, void *data) {
    (void)data;
    Output *output = wl_container_of(listener, output, frame);
    Server *server = (Server *)output->wlr_output->data;

    server->animate(anim_now_ns());
    if (server->animations_running()) {
        wlr_output_schedule_frame(output->wlr_output);
    }

    bool pending = server->screencopy && !wl_list_empty(&server->screencopy->frames);
    if (pending) damage_scene(server);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct wlr_scene_output_state_options options = {};
    wlr_scene_output_commit(output->scene_output, &options);
    wlr_scene_output_send_frame_done(output->scene_output, &now);
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
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    for (auto it = server->outputs.begin(); it != server->outputs.end(); ++it) {
        if (*it == output) {
            server->outputs.erase(it);
            break;
        }
    }
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
        wlr_log(WLR_ERROR, "failed to commit output %s", wlr_output->name);
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

    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    server->outputs.push_back(output);
    return output;
}
