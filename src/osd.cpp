#include "vlswm/osd.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>

static const int OSD_W = 320;
static const int OSD_H = 72;
static const int OSD_MARGIN = 32;

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

void Osd::render() {
    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);
    if (!ov.ensure(server->scene_overlay, OSD_W, OSD_H)) return;

    bool white_theme = server->theme_white;
    double bg = white_theme ? 0.0 : 1.0;
    double fg = 1.0 - bg;

    cairo_t *cr = cairo_create(ov.surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_set_line_width(cr, 2);
    cairo_rectangle(cr, 1, 1, OSD_W - 2, OSD_H - 2);
    cairo_stroke(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font =
        pango_font_description_from_string(server->settings.font.c_str());
    pango_layout_set_font_description(layout, font);
    pango_font_description_free(font);
    pango_layout_set_text(layout, label.c_str(), -1);
    cairo_move_to(cr, 16, 12);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    int bx = 16, by = OSD_H - 26, bw = OSD_W - 32, bh = 10;
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, bx + 0.5, by + 0.5, bw - 1, bh - 1);
    cairo_stroke(cr);
    double fill = std::clamp(value, 0.0, 1.0);
    if (fill > 0.0) {
        cairo_rectangle(cr, bx + 2, by + 2, (bw - 4) * fill, bh - 4);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    ov.commit(OSD_W, OSD_H);
    wlr_scene_node_set_position(&ov.node->node, (ow - OSD_W) / 2, OSD_MARGIN);
}

void Osd::show(const std::string &text, double fraction) {
    label = text;
    value = fraction;
    hide_at = anim_now_ns() + 1500ULL * 1000000ULL;
    visible = true;
    render();
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, true);
    server->request_frame();
}

void Osd::animate(uint64_t now) {
    if (!visible || now < hide_at) return;
    visible = false;
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, false);
    server->request_frame();
}
