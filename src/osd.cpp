#include "vlswm/osd.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"

#include <algorithm>

static const int OSD_W = 260;
static const int OSD_H = 12;
static const int OSD_MARGIN = 24;
static const int OSD_INSET = 2;

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

    double fill = std::clamp(value, 0.0, 1.0);
    if (fill > 0.0) {
        cairo_set_source_rgb(cr, fg, fg, fg);
        cairo_rectangle(cr, OSD_INSET, OSD_INSET,
                        (OSD_W - OSD_INSET * 2) * fill, OSD_H - OSD_INSET * 2);
        cairo_fill(cr);
    }

    cairo_destroy(cr);
    ov.commit(OSD_W, OSD_H);
    wlr_scene_node_set_position(&ov.node->node, (ow - OSD_W) / 2, OSD_MARGIN);
}

void Osd::show(double fraction) {
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
