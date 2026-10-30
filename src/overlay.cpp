#include "vlswm/overlay.h"
#include "vlswm/wlroots.h"

#include <drm_fourcc.h>

#include <cstdlib>
#include <cstring>

struct PixBuf {
    struct wlr_buffer base;
    unsigned char *data;
};

static void pixbuf_destroy(struct wlr_buffer *wlr_buffer) {
    auto *pb = (PixBuf *)wlr_buffer;
    free(pb->data);
    delete pb;
}

static bool pixbuf_begin(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data,
                         uint32_t *format, size_t *stride) {
    auto *pb = (PixBuf *)wlr_buffer;
    (void)flags;
    *data = pb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)wlr_buffer->width * 4;
    return true;
}

static void pixbuf_end(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static bool pixbuf_get_dmabuf(struct wlr_buffer *wlr_buffer,
                              struct wlr_dmabuf_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static bool pixbuf_get_shm(struct wlr_buffer *wlr_buffer,
                           struct wlr_shm_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static const struct wlr_buffer_impl pixbuf_impl = {
    .destroy = pixbuf_destroy,
    .get_dmabuf = pixbuf_get_dmabuf,
    .get_shm = pixbuf_get_shm,
    .begin_data_ptr_access = pixbuf_begin,
    .end_data_ptr_access = pixbuf_end,
};

OverlayBuffer::~OverlayBuffer() {
    destroy();
}

void OverlayBuffer::destroy() {
    if (node) {
        wlr_scene_node_destroy(&node->node);
        node = nullptr;
    }
    if (surf) {
        cairo_surface_destroy(surf);
        surf = nullptr;
    }
    if (pix) {
        wlr_buffer_drop(&pix->base);
        pix = nullptr;
    }
    width = 0;
    height = 0;
}

bool OverlayBuffer::ensure(struct wlr_scene_tree *parent, int w, int h) {
    if (node && width == w && height == h) return true;
    destroy();
    if (w <= 0 || h <= 0) return false;

    pix = new PixBuf;
    pix->data = (unsigned char *)malloc((size_t)w * h * 4);
    if (!pix->data) {
        delete pix;
        pix = nullptr;
        return false;
    }
    wlr_buffer_init(&pix->base, &pixbuf_impl, w, h);
    surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    node = wlr_scene_buffer_create(parent, &pix->base);
    if (!surf || !node) {
        destroy();
        return false;
    }
    width = w;
    height = h;
    return true;
}

void OverlayBuffer::commit(int draw_w, int draw_h) {
    if (!node || !pix || !surf) return;
    cairo_surface_flush(surf);
    const unsigned char *src = cairo_image_surface_get_data(surf);
    int src_stride = cairo_image_surface_get_stride(surf);
    int dst_stride = pix->base.width * 4;
    for (int y = 0; y < draw_h; y++) {
        memcpy(pix->data + (size_t)y * dst_stride, src + (size_t)y * src_stride,
               (size_t)draw_w * 4);
    }

    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 0, 0, draw_w, draw_h);
    wlr_scene_buffer_set_buffer_with_damage(node, &pix->base, &damage);
    pixman_region32_fini(&damage);

    struct wlr_fbox box = {0, 0, (double)draw_w, (double)draw_h};
    wlr_scene_buffer_set_source_box(node, &box);
    wlr_scene_buffer_set_dest_size(node, draw_w, draw_h);
}
