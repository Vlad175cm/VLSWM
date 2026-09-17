

#include "vlswm/server.h"
#include "vlswm/view.h"
#include "vlswm/ini.h"
#include "vlswm/corners.h"

#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/box.h>

#include <iterator>

static const int MASTER_W_DEN = 2;

static int wm_gap(Server *server) {
    if (!server->config) return 8;
    bool on = server->config->get_bool("wm", "gaps", true);
    if (!on) return 0;
    return server->config->get_int("wm", "gap_size", 8);
}

static void apply_geometry(Server *server, View *view, struct wlr_box box) {

    if (box.x == view->gx && box.y == view->gy &&
        box.width == view->gw && box.height == view->gh) {
        return;
    }
    bool jump = !server->animations_enabled() || server->suppress_anims ||
                view->closing || view->opening ||
                (view->gw == 0 && view->gh == 0);
    if (jump) {
        view->geo.active = false;
        view_apply_box(view, box.x, box.y, box.width, box.height);
        view->gx = box.x; view->gy = box.y;
        view->gw = box.width; view->gh = box.height;
        corners_update(server, view);
        return;
    }

    view->geo.begin(anim_now_ns(), server->animation_duration_ns(), server->easing(),
                    view->gx, view->gy, view->gw, view->gh,
                    box.x, box.y, box.width, box.height);
}

void Server::arrange() {
    if (outputs.empty()) return;

    Output *output = outputs.front();
    struct wlr_output *wlr_output = output->wlr_output;

    int ow, oh;
    wlr_output_effective_resolution(wlr_output, &ow, &oh);

    double ox = 0.0, oy = 0.0;
    wlr_output_layout_output_coords(output_layout, wlr_output, &ox, &oy);

    int area_x = (int)ox;
    int area_y = (int)oy;
    int area_w = ow;
    int area_h = oh;


    for (auto *v : views) {
        if (v->fullscreen && v->mapped && !v->closing &&
            v->workspace == current_workspace) {
            struct wlr_box box = {area_x, area_y, area_w, area_h};
            apply_geometry(this, v, box);
            wlr_scene_node_raise_to_top(&v->scene_tree->node);
            return;
        }
    }




    std::list<View *> tiled;
    for (auto *v : views) {
        if (v->mapped && !v->closing && !v->floating && !v->fullscreen &&
            v->workspace == current_workspace) {
            tiled.push_back(v);
        }
    }

    if (tiled.empty()) return;

    int gap = wm_gap(this);

    int inner_x = area_x + gap;
    int inner_y = area_y + gap;
    int inner_w = area_w - 2 * gap;
    int inner_h = area_h - 2 * gap;

    auto it = tiled.begin();
    View *master = *it;
    ++it;

    int master_w = inner_w / MASTER_W_DEN;
    if (tiled.size() == 1) {
        master_w = inner_w;
    }

    struct wlr_box mbox = {inner_x, inner_y, master_w, inner_h};
    apply_geometry(this, master, mbox);

    int count = (int)std::distance(it, tiled.end());
    if (count == 0) return;

    int stack_x = inner_x + master_w + gap;
    int stack_w = inner_x + inner_w - stack_x;
    int each_h = (inner_h - (count - 1) * gap) / count;

    int y = inner_y;
    for (; it != tiled.end(); ++it) {
        int h = each_h;

        if (std::next(it) == tiled.end()) {
            h = inner_y + inner_h - y;
        }
        struct wlr_box sbox = {stack_x, y, stack_w, h};
        apply_geometry(this, *it, sbox);
        y += h + gap;
    }


    View *focused = focused_view();
    if (focused && focused->mapped) {
        wlr_scene_node_raise_to_top(&focused->scene_tree->node);
    }
}
