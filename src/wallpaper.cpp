
#include "vlswm/wallpaper.h"
#include "vlswm/server.h"

#include <drm_fourcc.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include <cstdlib>
#include <cstring>


struct WallpaperBuffer {
    struct wlr_buffer base;
    unsigned char *data;
};

static void wallpaper_buffer_destroy(struct wlr_buffer *wlr_buffer) {
    struct WallpaperBuffer *wb = (struct WallpaperBuffer *)wlr_buffer;
    free(wb->data);
    delete wb;
}

static bool wallpaper_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
    uint32_t flags, void **data, uint32_t *format, size_t *stride) {
    struct WallpaperBuffer *wb = (struct WallpaperBuffer *)wlr_buffer;
    (void)flags;
    *data = wb->data;
    *format = DRM_FORMAT_ARGB8888;
    *stride = (size_t)wlr_buffer->width * 4;
    return true;
}

static void wallpaper_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
    (void)wlr_buffer;
}

static bool wallpaper_buffer_get_dmabuf(struct wlr_buffer *wlr_buffer,
                                        struct wlr_dmabuf_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static bool wallpaper_buffer_get_shm(struct wlr_buffer *wlr_buffer,
                                     struct wlr_shm_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static const struct wlr_buffer_impl wallpaper_buffer_impl = {
    .destroy = wallpaper_buffer_destroy,
    .get_dmabuf = wallpaper_buffer_get_dmabuf,
    .get_shm = wallpaper_buffer_get_shm,
    .begin_data_ptr_access = wallpaper_buffer_begin_data_ptr_access,
    .end_data_ptr_access = wallpaper_buffer_end_data_ptr_access,
};

void wallpaper_set_raster(Server *server, unsigned char *data, int w, int h) {
    free(server->wp_pixels);
    size_t sz = (size_t)w * h * 4;
    server->wp_pixels = (unsigned char *)malloc(sz);
    if (server->wp_pixels && data) {
        memcpy(server->wp_pixels, data, sz);
    }
    server->wp_w = w;
    server->wp_h = h;
    server->wp_rev++;
}

void wallpaper_clear_raster(Server *server) {
    free(server->wp_pixels);
    server->wp_pixels = nullptr;
    server->wp_w = 0;
    server->wp_h = 0;
    server->wp_rev++;
}

void wallpaper_sample(Server *server, int x, int y, unsigned char out[3]) {
    if (server->wp_pixels && server->wp_w > 0 && server->wp_h > 0) {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= server->wp_w) x = server->wp_w - 1;
        if (y >= server->wp_h) y = server->wp_h - 1;
        const unsigned char *px = server->wp_pixels + ((size_t)y * server->wp_w + x) * 4;
        out[0] = px[0];
        out[1] = px[1];
        out[2] = px[2];
    } else {
        out[0] = 0xFF;
        out[1] = 0xFF;
        out[2] = 0xFF;
    }
}


static std::string expand_home(const char *path) {
    if (!path || path[0] != '~' || path[1] != '/') return path ? path : "";
    const char *home = std::getenv("HOME");
    return std::string(home ? home : "") + (path + 1);
}

struct wlr_scene_buffer *wallpaper_create(Server *server, const char *path,
                                          int width, int height) {
    if (!path || !*path) return nullptr;

    std::string full = expand_home(path);
    GError *err = nullptr;
    GdkPixbuf *src = gdk_pixbuf_new_from_file(full.c_str(), &err);
    if (!src) {
        wlr_log(WLR_ERROR, "wallpaper: failed to load '%s': %s", full.c_str(),
                err ? err->message : "unknown error");
        if (err) g_error_free(err);
        return nullptr;
    }

    int iw = gdk_pixbuf_get_width(src);
    int ih = gdk_pixbuf_get_height(src);


    double scale_x = (double)width / iw;
    double scale_y = (double)height / ih;

    GdkPixbuf *scaled = gdk_pixbuf_new(GDK_COLORSPACE_RGB, true, 8, width, height);
    if (!scaled) {
        g_object_unref(src);
        wlr_log(WLR_ERROR, "wallpaper: failed to allocate scaled pixbuf");
        return nullptr;
    }
    gdk_pixbuf_scale(src, scaled, 0, 0, width, height, 0.0, 0.0,
                     scale_x, scale_y, GDK_INTERP_BILINEAR);
    g_object_unref(src);



    int dst_rowstride = width * 4;
    auto *wb = new WallpaperBuffer;
    wb->data = (unsigned char *)malloc((size_t)height * dst_rowstride);
    if (!wb->data) {
        g_object_unref(scaled);
        delete wb;
        return nullptr;
    }
    const unsigned char *p = gdk_pixbuf_get_pixels(scaled);
    int src_stride = gdk_pixbuf_get_rowstride(scaled);
    for (int y = 0; y < height; y++) {
        const unsigned char *row = p + y * src_stride;
        unsigned char *out = wb->data + y * dst_rowstride;
        for (int x = 0; x < width; x++) {
            const unsigned char *px = row + x * 4;
            unsigned char *opx = out + x * 4;
            opx[0] = px[2];
            opx[1] = px[1];
            opx[2] = px[0];
            opx[3] = px[3];
        }
    }
    g_object_unref(scaled);

    wallpaper_set_raster(server, wb->data, width, height);

    wlr_buffer_init(&wb->base, &wallpaper_buffer_impl, width, height);

    struct wlr_scene_buffer *scene_buffer =
        wlr_scene_buffer_create(server->scene_background, &wb->base);
    if (!scene_buffer) {
        wlr_log(WLR_ERROR, "wallpaper: failed to create scene buffer");
        wlr_buffer_drop(&wb->base);
        return nullptr;
    }
    wlr_buffer_drop(&wb->base);
    wlr_scene_buffer_set_dest_size(scene_buffer, width, height);
    wlr_log(WLR_INFO, "wallpaper: loaded '%s' (%dx%d -> %dx%d)", full.c_str(),
            iw, ih, width, height);
    return scene_buffer;
}
