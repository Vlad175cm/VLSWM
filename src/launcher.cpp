#include "vlswm/wlroots.h"
#include "vlswm/launcher.h"
#include "vlswm/server.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

static const int BOX_HEIGHT = 56;
static const int BORDER = 2;
static const int PADDING = 16;

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

static bool pixbuf_get_dmabuf(struct wlr_buffer *wlr_buffer, struct wlr_dmabuf_attributes *attribs) {
    (void)wlr_buffer;
    (void)attribs;
    return false;
}

static bool pixbuf_get_shm(struct wlr_buffer *wlr_buffer, struct wlr_shm_attributes *attribs) {
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

static struct wlr_scene_buffer *make_buffer(struct wlr_scene_tree *parent, cairo_surface_t *surf,
                                            int w, int h) {
    cairo_surface_flush(surf);
    auto *pb = new PixBuf;
    pb->data = (unsigned char *)malloc((size_t)w * h * 4);
    if (!pb->data) {
        delete pb;
        return nullptr;
    }
    const unsigned char *src = cairo_image_surface_get_data(surf);
    int src_stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; y++) {
        memcpy(pb->data + (size_t)y * w * 4, src + (size_t)y * src_stride, (size_t)w * 4);
    }
    wlr_buffer_init(&pb->base, &pixbuf_impl, w, h);
    struct wlr_scene_buffer *sb = wlr_scene_buffer_create(parent, &pb->base);
    wlr_buffer_drop(&pb->base);
    return sb;
}

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

static void utf8_pop(std::string &s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    s.erase(i);
}

void Launcher::init(Server *s) {
    server = s;
    render();
    if (buf) wlr_scene_node_set_enabled(&buf->node, false);
}

void Launcher::toggle() {
    if (open) close();
    else {
        open = true;
        input.clear();
        render();
        if (buf) wlr_scene_node_set_enabled(&buf->node, true);
        fade.begin(anim_now_ns(), server->settings.duration_ns, server->settings.easing, 0.0f, 1.0f);
        server->request_frame();
    }
}

void Launcher::close() {
    if (!open) return;
    open = false;
    fade.begin(anim_now_ns(), server->settings.duration_ns, server->settings.easing, 1.0f, 0.0f);
    server->request_frame();
}

void Launcher::key(xkb_keysym_t sym, const std::string &utf8) {
    if (!open) return;
    if (sym == XKB_KEY_Escape) {
        close();
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        std::string cmd = input;
        while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t')) cmd.pop_back();
        size_t start = cmd.find_first_not_of(" \t");
        cmd = start == std::string::npos ? "" : cmd.substr(start);
        close();
        if (!cmd.empty()) server->spawn_argv({"fish", "-c", cmd});
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        utf8_pop(input);
        render();
        return;
    }
    if (!utf8.empty() && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f) {
        input += utf8;
        render();
    }
}

void Launcher::render() {
    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);
    int w = std::clamp(ow / 3, 480, 900);
    int h = BOX_HEIGHT;

    bool white = server->theme_white;
    double bg = white ? 1.0 : 0.0;
    double fg = white ? 0.0 : 1.0;

    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(surf);
    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_set_line_width(cr, BORDER);
    cairo_rectangle(cr, BORDER / 2.0, BORDER / 2.0, w - BORDER, h - BORDER);
    cairo_stroke(cr);

    std::string text = "> " + input + "_";
    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(server->settings.font.c_str());
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_move_to(cr, PADDING, (h - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_destroy(cr);

    struct wlr_scene_buffer *fresh = make_buffer(server->scene_overlay, surf, w, h);
    cairo_surface_destroy(surf);
    if (!fresh) {
        wlr_log(WLR_ERROR, "launcher: cannot allocate buffer");
        return;
    }
    if (buf) wlr_scene_node_destroy(&buf->node);
    buf = fresh;
    wlr_scene_node_set_position(&buf->node, (ow - w) / 2, (oh - h) / 2);
    wlr_scene_node_set_enabled(&buf->node, open || fade.active);
    wlr_scene_buffer_set_opacity(buf, open ? 1.0f : 0.0f);
}

void Launcher::relayout() {
    render();
}

void Launcher::animate(uint64_t now) {
    if (!fade.active || !buf) return;
    float o = 0.0f;
    bool done = fade.step(now, &o);
    wlr_scene_buffer_set_opacity(buf, o);
    if (done && !open) wlr_scene_node_set_enabled(&buf->node, false);
}
