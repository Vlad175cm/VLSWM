#include "vlswm/rec.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"

static const int REC_SIZE = 44;
static const int REC_ICON = 16;
static const int REC_MARGIN = 32;
static const int REC_OFFSET = 180;

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

void Rec::render() {
    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);
    if (!ov.ensure(server->scene_overlay, REC_SIZE, REC_SIZE)) return;

    bool white_theme = server->theme_white;
    double bg = white_theme ? 0.0 : 1.0;
    double fg = 1.0 - bg;

    cairo_t *cr = cairo_create(ov.surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_paint(cr);

    double x = (REC_SIZE - REC_ICON) / 2.0;
    cairo_set_source_rgb(cr, fg, fg, fg);
    if (filled) {
        cairo_rectangle(cr, x, x, REC_ICON, REC_ICON);
        cairo_fill(cr);
    } else {
        cairo_set_line_width(cr, 2);
        cairo_rectangle(cr, x + 1, x + 1, REC_ICON - 2, REC_ICON - 2);
        cairo_stroke(cr);
    }

    cairo_destroy(cr);
    ov.commit(REC_SIZE, REC_SIZE);
    wlr_scene_node_set_position(&ov.node->node, ow - REC_SIZE - REC_MARGIN,
                                (oh - REC_SIZE) / 2 - REC_OFFSET);
    wlr_scene_node_set_enabled(&ov.node->node, visible);
}

void Rec::flash(bool start) {
    filled = start;
    visible = true;
    hide_at = anim_now_ns() + 1500ULL * 1000000ULL;
    render();
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, true);
    server->request_frame();
}

void Rec::animate(uint64_t now) {
    if (!visible || now < hide_at) return;
    visible = false;
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, false);
    server->request_frame();
}
