#include "vlswm/switcher.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"
#include "vlswm/view.h"
#include "vlswm/launcher.h"
#include "vlswm/clipboard.h"
#include "vlswm/notify.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cstdio>

static const int PANEL_W = 560;
static const int HEADER_H = 48;
static const int ROW_H = 32;
static const int PADDING = 10;
static const int MAX_ROWS = 10;

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

static std::string view_title(View *view) {
    if (!view || !view->xdg_surface || !view->xdg_surface->toplevel) return "window";
    const char *title = view->xdg_surface->toplevel->title;
    if (title && *title) return title;
    const char *app_id = view->xdg_surface->toplevel->app_id;
    if (app_id && *app_id) return app_id;
    return "window";
}

void Switcher::reload() {
    View *selected_view =
        (selected >= 0 && selected < (int)entries.size()) ? entries[selected] : nullptr;
    View *current = server->focused_view();
    if (current && (current->closing || current->dialog || current->lock_screen)) {
        current = nullptr;
    }
    entries.clear();
    if (current) entries.push_back(current);
    for (auto it = server->stack.rbegin(); it != server->stack.rend(); ++it) {
        View *view = *it;
        if (!view || view == current || view->closing || view->dialog ||
            view->lock_screen) {
            continue;
        }
        entries.push_back(view);
    }
    selected = 0;
    if (selected_view) {
        for (int i = 0; i < (int)entries.size(); i++) {
            if (entries[i] == selected_view) {
                selected = i;
                break;
            }
        }
    }
    if (open && entries.empty()) {
        close();
        return;
    }
    if (open) render();
}

void Switcher::toggle() {
    if (open) {
        close();
        return;
    }
    if (server->launcher && server->launcher->open) server->launcher->close();
    if (server->clipboard && server->clipboard->open) server->clipboard->close();
    if (server->notifications && server->notifications->panel) {
        server->notifications->close_panel();
    }
    reload();
    if (entries.empty()) return;
    View *current = server->focused_view();
    selected = 0;
    for (int i = 0; i < (int)entries.size(); i++) {
        if (entries[i] == current) {
            selected = i;
            break;
        }
    }
    open = true;
    render();
}

void Switcher::close() {
    if (!open) return;
    open = false;
    entries.clear();
    selected = 0;
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, false);
    server->request_frame();
}

void Switcher::choose() {
    if (!open || selected < 0 || selected >= (int)entries.size()) {
        close();
        return;
    }
    View *view = entries[selected];
    auto it = std::find(server->stack.begin(), server->stack.end(), view);
    if (it == server->stack.end()) {
        close();
        return;
    }
    server->stack.erase(it);
    server->stack.push_back(view);
    server->focus_view(view, view->wlr_surface());
    server->update_visibility();
    close();
}

void Switcher::key(xkb_keysym_t sym) {
    if (!open) return;
    if (sym == XKB_KEY_Escape) {
        close();
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        choose();
        return;
    }
    int n = (int)entries.size();
    if (n == 0) {
        close();
        return;
    }
    if (sym == XKB_KEY_Up) {
        selected = (selected + n - 1) % n;
        render();
        return;
    }
    if (sym == XKB_KEY_Down || sym == XKB_KEY_Tab) {
        selected = (selected + 1) % n;
        render();
    }
}

void Switcher::render() {
    if (!open) return;
    if (entries.empty()) {
        close();
        return;
    }
    if (selected < 0) selected = 0;
    if (selected >= (int)entries.size()) selected = (int)entries.size() - 1;

    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);
    int rows = std::min((int)entries.size(), MAX_ROWS);
    int h = HEADER_H + std::max(rows, 1) * ROW_H + PADDING;
    if (!ov.ensure(server->scene_overlay, PANEL_W, h)) return;

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
    cairo_rectangle(cr, 1, 1, PANEL_W - 2, h - 2);
    cairo_stroke(cr);

    PangoFontDescription *font =
        pango_font_description_from_string(server->settings.font.c_str());
    PangoFontDescription *header_font = pango_font_description_copy(font);
    pango_font_description_set_weight(header_font, PANGO_WEIGHT_BOLD);
    PangoLayout *header = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(header, header_font);
    pango_layout_set_text(header, "WINDOWS", -1);
    int th = 0;
    pango_layout_get_pixel_size(header, nullptr, &th);
    cairo_move_to(cr, PADDING, (HEADER_H - th) / 2.0);
    pango_cairo_show_layout(cr, header);
    g_object_unref(header);
    pango_font_description_free(header_font);

    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 1, HEADER_H);
    cairo_line_to(cr, PANEL_W - 1, HEADER_H);
    cairo_stroke(cr);

    int first = selected - rows / 2;
    first = std::clamp(first, 0, std::max(0, (int)entries.size() - rows));

    for (int r = 0; r < rows; r++) {
        int idx = first + r;
        if (idx >= (int)entries.size()) break;
        double y = HEADER_H + r * ROW_H;
        bool active = idx == selected;
        if (active) {
            cairo_set_source_rgb(cr, fg, fg, fg);
            cairo_rectangle(cr, 1, y, PANEL_W - 2, ROW_H);
            cairo_fill(cr);
            cairo_set_source_rgb(cr, bg, bg, bg);
        } else {
            cairo_set_source_rgb(cr, fg, fg, fg);
        }

        PangoLayout *row = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(row, font);
        pango_layout_set_width(row, (PANEL_W - PADDING * 2) * PANGO_SCALE);
        pango_layout_set_ellipsize(row, PANGO_ELLIPSIZE_END);
        std::string title = view_title(entries[idx]);
        pango_layout_set_text(row, title.c_str(), -1);
        int rh = 0;
        pango_layout_get_pixel_size(row, nullptr, &rh);
        cairo_move_to(cr, PADDING, y + (ROW_H - rh) / 2.0);
        pango_cairo_show_layout(cr, row);
        g_object_unref(row);
    }

    pango_font_description_free(font);
    cairo_destroy(cr);

    ov.commit(PANEL_W, h);
    wlr_scene_node_set_position(&ov.node->node, (ow - PANEL_W) / 2, (oh - h) / 2);
    wlr_scene_node_set_enabled(&ov.node->node, true);
    server->request_frame();
}
