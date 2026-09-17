
#include "vlswm/corners.h"
#include "vlswm/ini.h"
#include "vlswm/server.h"
#include "vlswm/view.h"
#include "vlswm/wallpaper.h"

#include <drm_fourcc.h>
#include <wlr/types/wlr_scene.h>

#include <cstdlib>


struct CornerMaskBuffer {
    struct wlr_buffer base;
    unsigned char *data;
};

static void corner_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct CornerMaskBuffer *cb = (struct CornerMaskBuffer *)wlr_buffer;
    free(cb->data);
    delete cb;
}

static bool corner_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
    uint32_t flags, void **data, uint32_t *format, size_t *stride) {
    struct CornerMaskBuffer *cb = (struct CornerMaskBuffer *)wlr_buffer;
    (void)flags;
    *data = cb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)wlr_buffer->width * 4;
    return true;
}

static void corner_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static bool corner_buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
                                     struct wlr_dmabuf_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static bool corner_buffer_get_shm(struct wlr_buffer *wlr_buffer,
                                  struct wlr_shm_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static const struct wlr_buffer_impl corner_buffer_impl = {
    .destroy = corner_buffer_destroy,
    .get_dmabuf = corner_buffer_get_dmabuf,
    .get_shm = corner_buffer_get_shm,
    .begin_data_ptr_access = corner_buffer_begin_data_ptr_access,
    .end_data_ptr_access = corner_buffer_end_data_ptr_access,
};


static bool corner_accepts_input(struct wlr_scene_buffer *buffer,
                                 double *sx, double *sy) {
    (void)buffer;
    (void)sx;
    (void)sy;
    return false;
}

enum Corner { TL, TR, BL, BR };




static void corner_fill(unsigned char *data, int r, Server *server, View *view,
                        Corner corner) {
    const int gx = view->gx, gy = view->gy, gw = view->gw, gh = view->gh;


    int cx, cy, ox, oy;
    switch (corner) {
        case TL: cx = 0;      cy = 0;      ox = 0;        oy = 0;        break;
        case TR: cx = r - 1;  cy = 0;      ox = gw - r;   oy = 0;        break;
        case BL: cx = 0;      cy = r - 1;  ox = 0;        oy = gh - r;   break;
        default: cx = r - 1;  cy = r - 1;  ox = gw - r;   oy = gh - r;   break;
    }
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int dx = i - cx, dy = j - cy;
            unsigned char *px = data + ((size_t)j * r + i) * 4;
            if (dx * dx + dy * dy <= r * r) {
                int sx = gx + ox + i;
                int sy = gy + oy + j;
                unsigned char bgr[3];
                wallpaper_sample(server, sx, sy, bgr);
                px[0] = bgr[0];
                px[1] = bgr[1];
                px[2] = bgr[2];
                px[3] = 0xFF;
            } else {
                px[0] = px[1] = px[2] = px[3] = 0;
            }
        }
    }
}

int corner_radius(Server *server, View *view) {
    if (!server->config || !view->mapped || view->fullscreen) return 0;
    int r = server->config->get_int("wm", "corner_radius", 0);
    return r < 0 ? 0 : r;
}

void corners_destroy(View *view) {
    struct wlr_scene_buffer *bufs[4] = {view->c_tl, view->c_tr,
                                        view->c_bl, view->c_br};
    for (struct wlr_scene_buffer *sb : bufs) {
        if (sb) wlr_scene_node_destroy(&sb->node);
    }
    view->c_tl = view->c_tr = view->c_bl = view->c_br = nullptr;
    view->c_x = view->c_y = view->c_w = view->c_h = -1;
    view->c_r = 0;
    view->c_wp_rev = -1;
}

void corners_update(Server *server, View *view) {
    const int r = corner_radius(server, view);
    const int gx = view->gx, gy = view->gy, gw = view->gw, gh = view->gh;


    if (view->c_r == r && view->c_wp_rev == server->wp_rev &&
        view->c_x == gx && view->c_y == gy &&
        view->c_w == gw && view->c_h == gh) {
        return;
    }

    corners_destroy(view);
    view->c_x = gx;
    view->c_y = gy;
    view->c_w = gw;
    view->c_h = gh;
    view->c_r = r;
    view->c_wp_rev = server->wp_rev;

    if (r <= 0 || gw < 2 * r || gh < 2 * r) return;

    static const Corner order[4] = {TL, TR, BL, BR};
    for (Corner corner : order) {
        auto *cb = new CornerMaskBuffer;
        cb->data = (unsigned char *)malloc((size_t)r * r * 4);
        if (!cb->data) {
            delete cb;
            continue;
        }
        corner_fill(cb->data, r, server, view, corner);

        wlr_buffer_init(&cb->base, &corner_buffer_impl, r, r);
        struct wlr_scene_buffer *sb =
            wlr_scene_buffer_create(view->scene_tree, &cb->base);
        if (!sb) {
            wlr_buffer_drop(&cb->base);
            continue;
        }
        wlr_buffer_drop(&cb->base);
        sb->point_accepts_input = corner_accepts_input;

        int px, py;
        switch (corner) {
            case TL: px = 0;      py = 0;      break;
            case TR: px = gw - r; py = 0;      break;
            case BL: px = 0;      py = gh - r; break;
            default: px = gw - r; py = gh - r; break;
        }
        wlr_scene_node_set_position(&sb->node, px, py);

        switch (corner) {
            case TL: view->c_tl = sb; break;
            case TR: view->c_tr = sb; break;
            case BL: view->c_bl = sb; break;
            default: view->c_br = sb; break;
        }
    }
}

void corners_opacity(View *view, float opacity) {
    struct wlr_scene_buffer *bufs[4] = {view->c_tl, view->c_tr,
                                        view->c_bl, view->c_br};
    for (struct wlr_scene_buffer *sb : bufs) {
        if (sb) wlr_scene_buffer_set_opacity(sb, opacity);
    }
}
