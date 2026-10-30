#include "vlswm/clipboard.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"

#include <pango/pangocairo.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

static const int PANEL_W = 640;
static const int HEADER_H = 48;
static const int ROW_H = 28;
static const int PADDING = 10;
static const int MAX_ROWS = 10;

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

static std::string run_cmd(const std::string &cmd) {
    std::string out;
    FILE *p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), p)) out += buf;
    pclose(p);
    return out;
}

static std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string preview_of(const std::string &item) {
    size_t tab = item.find('\t');
    return tab == std::string::npos ? item : item.substr(tab + 1);
}

static void utf8_pop(std::string &s) {
    if (s.empty()) return;
    size_t i = s.size() - 1;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
    s.erase(i);
}

void Clipboard::reload() {
    items.clear();
    std::string out = run_cmd("cliphist list 2>/dev/null");
    std::stringstream ss(out);
    std::string line;
    while (std::getline(ss, line) && items.size() < 500) {
        if (!line.empty()) items.push_back(line);
    }
    refilter();
}

void Clipboard::refilter() {
    std::string q = lower(input);
    filtered.clear();
    for (size_t i = 0; i < items.size(); i++) {
        if (q.empty() || lower(preview_of(items[i])).find(q) != std::string::npos) {
            filtered.push_back((int)i);
        }
    }
    if (selected >= (int)filtered.size()) selected = (int)filtered.size() - 1;
    if (selected < 0) selected = 0;
}

void Clipboard::toggle() {
    if (open) {
        close();
        return;
    }
    open = true;
    input.clear();
    selected = 0;
    reload();
    render();
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, true);
}

void Clipboard::close() {
    if (!open) return;
    open = false;
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, false);
    server->focus_top();
    server->request_frame();
}

void Clipboard::choose() {
    if (filtered.empty() || selected < 0 || selected >= (int)filtered.size()) return;
    const std::string &item = items[filtered[selected]];
    size_t tab = item.find('\t');
    std::string id = item.substr(0, tab);
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) return;
    server->spawn("cliphist decode " + id + " | wl-copy");
    close();
}

void Clipboard::key(xkb_keysym_t sym, const std::string &utf8) {
    if (!open) return;
    if (sym == XKB_KEY_Escape) {
        close();
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        choose();
        return;
    }
    if (sym == XKB_KEY_Up) {
        if (selected > 0) selected--;
        render();
        return;
    }
    if (sym == XKB_KEY_Down) {
        if (selected + 1 < (int)filtered.size()) selected++;
        render();
        return;
    }
    if (sym == XKB_KEY_BackSpace) {
        utf8_pop(input);
        refilter();
        render();
        return;
    }
    if (!utf8.empty() && (unsigned char)utf8[0] >= 0x20 && utf8[0] != 0x7f) {
        input += utf8;
        refilter();
        render();
    }
}

void Clipboard::render() {
    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);
    int rows = std::min((int)filtered.size(), MAX_ROWS);
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

    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font);
    std::string header = "> " + input + "_";
    pango_layout_set_text(layout, header.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_move_to(cr, PADDING, (HEADER_H - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    g_object_unref(layout);

    cairo_set_line_width(cr, 1);
    cairo_move_to(cr, 1, HEADER_H);
    cairo_line_to(cr, PANEL_W - 1, HEADER_H);
    cairo_stroke(cr);

    if (filtered.empty()) {
        PangoLayout *empty = pango_cairo_create_layout(cr);
        pango_layout_set_font_description(empty, font);
        pango_layout_set_text(empty, "empty", -1);
        cairo_move_to(cr, PADDING, HEADER_H + (ROW_H - th) / 2.0);
        pango_cairo_show_layout(cr, empty);
        g_object_unref(empty);
    }

    int first = std::max(0, selected - MAX_ROWS + 1);
    for (int r = 0; r < rows; r++) {
        int idx = first + r;
        if (idx >= (int)filtered.size()) break;
        std::string text = preview_of(items[filtered[idx]]);
        std::string one;
        for (char c : text) {
            if (c == '\n' || c == '\r' || c == '\t') one += ' ';
            else one += c;
        }
        if (one.size() > 200) one = one.substr(0, 200);

        double y = HEADER_H + r * ROW_H;
        if (idx == selected) {
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
        pango_layout_set_text(row, one.c_str(), -1);
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
}
