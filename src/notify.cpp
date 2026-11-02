#include "vlswm/notify.h"
#include "vlswm/wlroots.h"
#include "vlswm/server.h"

#include <pango/pangocairo.h>
#include <systemd/sd-bus.h>

#include <algorithm>
#include <cstdio>

static const char *BUS_NAME = "org.freedesktop.Notifications";
static const char *OBJ_PATH = "/org/freedesktop/Notifications";

static const int CARD_W = 340;
static const int CARD_H = 112;
static const int CARD_GAP = 8;
static const int MAX_CARDS = 3;
static const int MAX_HISTORY = 50;
static const int MARGIN = 32;

static const int PANEL_W = 440;
static const int PANEL_HEADER = 52;
static const int PANEL_ENTRY = 112;
static const int PANEL_PAD = 8;

static void output_size(Server *server, int *w, int *h) {
    *w = 0;
    *h = 0;
    if (!server->outputs.empty()) {
        wlr_output_effective_resolution(server->outputs.front()->wlr_output, w, h);
    }
}

static uint64_t now_ms() {
    return anim_now_ns() / 1000000ULL;
}

static int draw_text(cairo_t *cr, const std::string &font, const std::string &text,
                     double x, double y, double width, bool bold, double shade,
                     int size_pt, int max_lines) {
    PangoFontDescription *fd = pango_font_description_from_string(font.c_str());
    pango_font_description_set_size(fd, size_pt * PANGO_SCALE);
    if (bold) pango_font_description_set_weight(fd, PANGO_WEIGHT_BOLD);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, fd);
    pango_layout_set_width(layout, (int)(width * PANGO_SCALE));
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
    if (max_lines > 0) pango_layout_set_height(layout, -max_lines);
    pango_layout_set_text(layout, text.c_str(), -1);
    cairo_set_source_rgb(cr, shade, shade, shade);
    cairo_move_to(cr, x, y);
    pango_cairo_show_layout(cr, layout);
    int w = 0, h = 0;
    pango_layout_get_pixel_size(layout, &w, &h);
    g_object_unref(layout);
    pango_font_description_free(fd);
    return h;
}

static void emit_closed(sd_bus *bus, uint32_t id, uint32_t reason) {
    if (!bus) return;
    sd_bus_emit_signal(bus, OBJ_PATH, BUS_NAME, "NotificationClosed", "uu", id, reason);
}

uint32_t Notifications::push(const std::string &app, const std::string &summary,
                             const std::string &body, int timeout_ms, uint32_t replaces) {
    uint32_t id = replaces ? replaces : next_id++;
    if (replaces) {
        cards.erase(std::remove_if(cards.begin(), cards.end(),
                                   [id](const Note &n) { return n.id == id; }),
                    cards.end());
        history.erase(std::remove_if(history.begin(), history.end(),
                                     [id](const Note &n) { return n.id == id; }),
                      history.end());
    }

    int t = timeout_ms < 0 ? 4000 : timeout_ms;
    if (t == 0) t = 4000;

    Note note;
    note.id = id;
    note.app = app;
    note.summary = summary;
    note.body = body;
    note.expires = now_ms() + (uint64_t)t;

    cards.insert(cards.begin(), note);
    while (cards.size() > MAX_CARDS) {
        emit_closed(bus, cards.back().id, 1);
        cards.pop_back();
    }
    history.insert(history.begin(), note);
    if (history.size() > MAX_HISTORY) history.pop_back();

    render();
    if (server) server->request_frame();
    return id;
}

void Notifications::close_note(uint32_t id) {
    auto it = std::find_if(cards.begin(), cards.end(), [id](const Note &n) { return n.id == id; });
    if (it != cards.end()) {
        emit_closed(bus, id, 3);
        cards.erase(it);
        render();
        if (server) server->request_frame();
    }
}

void Notifications::animate(uint64_t now) {
    uint64_t ms = now / 1000000ULL;
    bool changed = false;
    for (auto it = cards.begin(); it != cards.end();) {
        if (it->expires && ms >= it->expires) {
            emit_closed(bus, it->id, 1);
            it = cards.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed) {
        render();
        if (server) server->request_frame();
    }
}

void Notifications::toggle_panel() {
    panel = !panel;
    render();
    if (server) server->request_frame();
}

void Notifications::close_panel() {
    if (!panel) return;
    panel = false;
    render();
    if (server) server->request_frame();
}

void Notifications::key(xkb_keysym_t sym, const std::string &utf8) {
    if (sym == XKB_KEY_Escape) {
        close_panel();
        return;
    }
    if (utf8 == "c" || utf8 == "C") {
        history.clear();
        cards.clear();
        render();
        if (server) server->request_frame();
    }
}

void Notifications::render() {
    int ow = 0, oh = 0;
    output_size(server, &ow, &oh);

    bool white_theme = server->theme_white;
    double bg = white_theme ? 0.0 : 1.0;
    double fg = 1.0 - bg;
    double dim = white_theme ? 0.72 : 0.42;
    const std::string &font = server->settings.font;

    if (!panel && cards.empty()) {
        if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, false);
        return;
    }

    int w = 0, h = 0;
    int max_entries = 0;
    if (panel) {
        w = PANEL_W;
        max_entries = std::clamp((oh - 2 * MARGIN - PANEL_HEADER) / PANEL_ENTRY, 1, 8);
        int n = std::min((int)history.size(), max_entries);
        h = PANEL_HEADER + std::max(n, 1) * PANEL_ENTRY + PANEL_PAD;
    } else {
        w = CARD_W;
        h = (int)cards.size() * CARD_H + ((int)cards.size() - 1) * CARD_GAP;
    }
    if (!ov.ensure(server->scene_overlay, w, h)) return;

    cairo_t *cr = cairo_create(ov.surf);
    // Opaque plate in the theme background colour: transparent gaps would
    // show the dashboard text behind the cards.
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    double plate = 1.0 - bg;
    cairo_set_source_rgb(cr, plate, plate, plate);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    if (panel) {
        cairo_set_source_rgb(cr, bg, bg, bg);
        cairo_rectangle(cr, 0, 0, w, h);
        cairo_fill(cr);

        draw_text(cr, font, "Notifications", 16, 14, w - 32, true, fg, 14, 1);
        cairo_set_source_rgb(cr, fg, fg, fg);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, 0, PANEL_HEADER - 0.5);
        cairo_line_to(cr, w, PANEL_HEADER - 0.5);
        cairo_stroke(cr);

        if (history.empty()) {
            draw_text(cr, font, "No notifications", 16, PANEL_HEADER + 24, w - 32, false, dim, 12, 1);
        }
        int n = std::min((int)history.size(), max_entries);
        for (int i = 0; i < n; i++) {
            const Note &note = history[i];
            double y = PANEL_HEADER + i * PANEL_ENTRY;
            cairo_save(cr);
            cairo_rectangle(cr, 0, y, w, PANEL_ENTRY);
            cairo_clip(cr);
            double ty = y + 10;
            ty += draw_text(cr, font, note.app, 16, ty, w - 32, false, dim, 11, 1);
            ty += draw_text(cr, font, note.summary, 16, ty + 2, w - 32, true, fg, 13, 1);
            draw_text(cr, font, note.body, 16, ty + 2, w - 32, false, fg, 12, 2);
            cairo_restore(cr);
            if (i + 1 < n) {
                cairo_set_source_rgb(cr, dim, dim, dim);
                cairo_set_line_width(cr, 1);
                cairo_move_to(cr, 16, y + PANEL_ENTRY - 0.5);
                cairo_line_to(cr, w - 16, y + PANEL_ENTRY - 0.5);
                cairo_stroke(cr);
            }
        }
    } else {
        for (size_t i = 0; i < cards.size(); i++) {
            const Note &note = cards[i];
            double y = (double)i * (CARD_H + CARD_GAP);
            cairo_set_source_rgb(cr, bg, bg, bg);
            cairo_rectangle(cr, 0, y, w, CARD_H);
            cairo_fill(cr);

            cairo_save(cr);
            cairo_rectangle(cr, 0, y, w, CARD_H);
            cairo_clip(cr);
            double ty = y + 10;
            ty += draw_text(cr, font, note.app, 12, ty, w - 24, false, dim, 11, 1);
            ty += draw_text(cr, font, note.summary, 12, ty + 2, w - 24, true, fg, 13, 1);
            draw_text(cr, font, note.body, 12, ty + 2, w - 24, false, fg, 12, 2);
            cairo_restore(cr);
        }
    }

    cairo_destroy(cr);
    ov.commit(w, h);
    wlr_scene_node_set_position(&ov.node->node, MARGIN, (oh - h) / 2);
    wlr_scene_node_set_enabled(&ov.node->node, true);
}

static int method_notify(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *n = (Notifications *)userdata;
    const char *app = "";
    const char *icon = "";
    const char *summary = "";
    const char *body = "";
    uint32_t replaces = 0;
    int32_t timeout = -1;

    if (sd_bus_message_read(m, "susss", &app, &replaces, &icon, &summary, &body) < 0) {
        return sd_bus_reply_method_errorf(m, SD_BUS_ERROR_INVALID_ARGS, "bad Notify call");
    }
    sd_bus_message_skip(m, "as");
    sd_bus_message_skip(m, "a{sv}");
    if (sd_bus_message_read(m, "i", &timeout) < 0) timeout = -1;

    uint32_t id = n->push(app, summary, body, timeout, replaces);
    return sd_bus_reply_method_return(m, "u", id);
}

static int method_close(sd_bus_message *m, void *userdata, sd_bus_error *) {
    auto *n = (Notifications *)userdata;
    uint32_t id = 0;
    sd_bus_message_read(m, "u", &id);
    n->close_note(id);
    return sd_bus_reply_method_return(m, "");
}

static int method_caps(sd_bus_message *m, void *, sd_bus_error *) {
    sd_bus_message *reply = nullptr;
    if (sd_bus_message_new_method_return(m, &reply) < 0) return -ENOMEM;
    sd_bus_message_open_container(reply, 'a', "s");
    sd_bus_message_append(reply, "s", "body");
    sd_bus_message_close_container(reply);
    int r = sd_bus_send(nullptr, reply, nullptr);
    sd_bus_message_unref(reply);
    return r < 0 ? r : 1;
}

static int method_info(sd_bus_message *m, void *, sd_bus_error *) {
    return sd_bus_reply_method_return(m, "ssss", "vlswm", "vlswm", VLSWM_VERSION, "1.2");
}

static const sd_bus_vtable notify_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Notify", "susssasa{sv}i", "u", method_notify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("CloseNotification", "u", "", method_close, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetCapabilities", "", "as", method_caps, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("GetServerInformation", "", "ssss", method_info, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END,
};

static int on_bus_event(int, uint32_t, void *data) {
    auto *n = (Notifications *)data;
    n->dbus_process();
    return 0;
}

void Notifications::dbus_process() {
    if (!bus) return;
    while (sd_bus_process(bus, nullptr) > 0) {
    }
    sd_bus_flush(bus);
}

void Notifications::dbus_init() {
    if (sd_bus_default_user(&bus) < 0) {
        wlr_log(WLR_ERROR, "notifications: cannot connect to the session bus");
        bus = nullptr;
        return;
    }
    if (sd_bus_add_object_vtable(bus, nullptr, OBJ_PATH, BUS_NAME, notify_vtable, this) < 0) {
        wlr_log(WLR_ERROR, "notifications: cannot register the object");
        sd_bus_unref(bus);
        bus = nullptr;
        return;
    }
    int r = sd_bus_request_name(bus, BUS_NAME, 0);
    if (r < 0) {
        wlr_log(WLR_ERROR, "notifications: cannot own %s (is another daemon running?)", BUS_NAME);
        sd_bus_unref(bus);
        bus = nullptr;
        return;
    }
    int fd = sd_bus_get_fd(bus);
    if (fd >= 0) {
        bus_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), fd,
                                          WL_EVENT_READABLE, on_bus_event, this);
    }
}
