#pragma once

#include <cairo.h>

struct wlr_scene_tree;
struct wlr_scene_buffer;
struct PixBuf;

struct OverlayBuffer {
    struct wlr_scene_buffer *node = nullptr;
    struct PixBuf *pix = nullptr;
    cairo_surface_t *surf = nullptr;
    int width = 0;
    int height = 0;

    OverlayBuffer() = default;
    ~OverlayBuffer();
    OverlayBuffer(const OverlayBuffer &) = delete;
    OverlayBuffer &operator=(const OverlayBuffer &) = delete;

    bool ensure(struct wlr_scene_tree *parent, int w, int h);
    void commit(int draw_w, int draw_h);
    void destroy();
};
