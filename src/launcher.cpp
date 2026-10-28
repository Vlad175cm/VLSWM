#include "vlswm/launcher.h"
#include "vlswm/server.h"

#include <cairo.h>
#include <drm_fourcc.h>
#include <pango/pangocairo.h>
#include <wlr/util/log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

static const int BOX_HEIGHT = 56;
static const int BORDER = 2;
static const int PADDING = 16;
static const uint64_t ERROR_MS = 1500;

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

static void copy_surface(cairo_surface_t *surf, PixBuf *pix, int w, int h) {
    cairo_surface_flush(surf);
    const unsigned char *src = cairo_image_surface_get_data(surf);
    int src_stride = cairo_image_surface_get_stride(surf);
    for (int y = 0; y < h; y++) {
        memcpy(pix->data + (size_t)y * w * 4, src + (size_t)y * src_stride, (size_t)w * 4);
    }
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

static std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string lower(std::string s) {
    for (auto &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::string strip_field_codes(const std::string &exec) {
    std::string out;
    out.reserve(exec.size());
    for (size_t i = 0; i < exec.size(); i++) {
        if (exec[i] != '%') {
            out += exec[i];
            continue;
        }
        if (i + 1 < exec.size()) {
            char c = exec[i + 1];
            if (c == '%') {
                out += '%';
            } else if (strchr("fFuUdDnNickvm", c)) {
                i++;
                continue;
            } else {
                out += c;
                i++;
            }
        }
    }
    return trim(out);
}

static std::string first_token(const std::string &exec) {
    size_t i = 0;
    while (i < exec.size() && isspace((unsigned char)exec[i])) i++;
    size_t start = i;
    bool quote = false;
    char q = 0;
    for (; i < exec.size(); i++) {
        char c = exec[i];
        if (quote) {
            if (c == q) quote = false;
            continue;
        }
        if (c == '\'' || c == '"') {
            quote = true;
            q = c;
            continue;
        }
        if (isspace((unsigned char)c)) break;
    }
    std::string token = exec.substr(start, i - start);
    if (!token.empty() && (token.front() == '\'' || token.front() == '"')) {
        token.erase(0, 1);
    }
    if (!token.empty() && (token.back() == '\'' || token.back() == '"')) {
        token.pop_back();
    }
    return token;
}

static bool command_exists(const std::string &cmd) {
    if (cmd.empty()) return false;
    if (cmd.find('/') != std::string::npos) return access(cmd.c_str(), X_OK) == 0;
    const char *path = std::getenv("PATH");
    if (!path) return false;
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        std::string full = dir + "/" + cmd;
        if (access(full.c_str(), X_OK) == 0) return true;
    }
    return false;
}

static void add_app(std::vector<DesktopApp> &apps, const std::string &name,
                    const std::string &exec, const std::string &stem, bool terminal,
                    const std::string &terminal_cmd) {
    if (name.empty() || exec.empty()) return;
    std::string token = first_token(exec);
    std::string base = token;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (!terminal_cmd.empty() && lower(base) == lower(terminal_cmd)) return;

    DesktopApp app;
    app.name = name;
    app.exec = exec;
    app.terminal = terminal;
    app.match = lower(name);
    apps.push_back(app);

    if (!stem.empty() && lower(stem) != app.match) {
        DesktopApp alias = app;
        alias.match = lower(stem);
        apps.push_back(alias);
    }
    if (!base.empty() && lower(base) != app.match && lower(base) != lower(stem)) {
        DesktopApp alias = app;
        alias.match = lower(base);
        apps.push_back(alias);
    }
}

static void parse_desktop_file(const std::filesystem::path &file,
                               std::vector<DesktopApp> &apps,
                               const std::string &terminal_cmd) {
    std::ifstream in(file);
    if (!in) return;

    std::string name;
    std::vector<std::string> localized;
    std::string exec;
    std::string try_exec;
    bool terminal = false;
    bool hidden = false;
    bool no_display = false;
    bool is_app = false;
    bool in_entry = false;
    std::string line;

    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        if (s[0] == '[') {
            in_entry = s == "[Desktop Entry]";
            continue;
        }
        if (!in_entry) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(s.substr(0, eq));
        std::string value = trim(s.substr(eq + 1));

        if (key == "Name") name = value;
        else if (key.rfind("Name[", 0) == 0) localized.push_back(value);
        else if (key == "Exec") exec = value;
        else if (key == "TryExec") try_exec = value;
        else if (key == "Terminal") terminal = lower(value) == "true";
        else if (key == "Hidden") hidden = lower(value) == "true";
        else if (key == "NoDisplay") no_display = lower(value) == "true";
        else if (key == "Type") is_app = value == "Application";
    }

    if (!is_app || hidden || no_display) return;
    if (!try_exec.empty() && !command_exists(try_exec)) return;

    std::string clean = strip_field_codes(exec);
    if (clean.empty()) return;

    std::string stem = file.stem().string();
    add_app(apps, name, clean, stem, terminal, terminal_cmd);
    for (const auto &alt : localized) {
        if (alt.empty()) continue;
        DesktopApp app;
        app.name = name;
        app.exec = clean;
        app.terminal = terminal;
        app.match = lower(alt);
        apps.push_back(app);
    }
}

void Launcher::scan_apps() {
    apps.clear();
    std::vector<std::string> dirs;

    const char *home = std::getenv("HOME");
    const char *data_home = std::getenv("XDG_DATA_HOME");
    if (data_home && *data_home) {
        dirs.push_back(std::string(data_home) + "/applications");
    } else if (home) {
        dirs.push_back(std::string(home) + "/.local/share/applications");
    }
    if (home) {
        dirs.push_back(std::string(home) + "/.local/share/flatpak/exports/share/applications");
    }
    dirs.push_back("/var/lib/flatpak/exports/share/applications");

    const char *data_dirs = std::getenv("XDG_DATA_DIRS");
    std::string system_dirs = data_dirs && *data_dirs
        ? data_dirs
        : "/usr/local/share:/usr/share";
    std::stringstream ss(system_dirs);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (!dir.empty()) dirs.push_back(dir + "/applications");
    }

    std::string terminal_cmd = server->settings.terminal;
    size_t tslash = terminal_cmd.find_last_of('/');
    if (tslash != std::string::npos) terminal_cmd = terminal_cmd.substr(tslash + 1);
    for (const auto &d : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(d, ec)) continue;
        for (auto it = std::filesystem::recursive_directory_iterator(
                 d, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".desktop") continue;
            parse_desktop_file(it->path(), apps, terminal_cmd);
        }
    }
}

Launcher::~Launcher() {
    if (surf) cairo_surface_destroy(surf);
    if (pix) wlr_buffer_drop(&pix->base);
}

void Launcher::init(Server *s) {
    server = s;
    scan_apps();
    render();
    if (buf) wlr_scene_node_set_enabled(&buf->node, false);
}

bool Launcher::busy() const {
    return fade.active || error_until != 0;
}

void Launcher::toggle() {
    if (open) {
        close();
        return;
    }
    open = true;
    input.clear();
    error.clear();
    error_until = 0;
    scan_apps();
    render();
    if (buf) wlr_scene_node_set_enabled(&buf->node, true);
    fade.begin(anim_now_ns(), server->settings.duration_ns, server->settings.easing, 0.0f, 1.0f);
    server->request_frame();
}

void Launcher::close() {
    if (!open) return;
    open = false;
    error.clear();
    error_until = 0;
    fade.begin(anim_now_ns(), server->settings.duration_ns, server->settings.easing, 1.0f, 0.0f);
    server->request_frame();
}

bool Launcher::launch(const std::string &text) {
    std::string norm = lower(trim(text));
    if (norm.empty()) return false;

    for (const auto &app : apps) {
        if (app.match != norm) continue;
        std::string base = first_token(app.exec);
        size_t slash = base.find_last_of('/');
        if (slash != std::string::npos) base = base.substr(slash + 1);
        if (lower(base) == lower(server->settings.terminal)) return false;

        if (app.terminal) {
            server->spawn_terminal_command(app.exec);
        } else {
            server->spawn_argv({"/bin/sh", "-c", app.exec});
        }
        return true;
    }

    std::string cmd = first_token(text);
    std::string base = cmd;
    size_t slash = base.find_last_of('/');
    if (slash != std::string::npos) base = base.substr(slash + 1);
    if (lower(base) == lower(server->settings.terminal)) return false;
    if (!command_exists(cmd)) return false;
    server->spawn_argv({"fish", "-c", text});
    return true;
}

void Launcher::key(xkb_keysym_t sym, const std::string &utf8) {
    if (!open) return;
    error.clear();
    error_until = 0;

    if (sym == XKB_KEY_Escape) {
        close();
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        std::string text = trim(input);
        if (text.empty()) return;
        if (launch(text)) {
            close();
        } else {
            error = "not found: " + text;
            error_until = anim_now_ns() + ERROR_MS * 1000000ULL;
            render();
            server->request_frame();
        }
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
    int h = BOX_HEIGHT;
    bool white = server->theme_white;
    double bg = white ? 1.0 : 0.0;
    double fg = white ? 0.0 : 1.0;

    cairo_surface_t *probe = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *pcr = cairo_create(probe);
    PangoLayout *pl = pango_cairo_create_layout(pcr);
    PangoFontDescription *pf = pango_font_description_from_string(server->settings.font.c_str());
    pango_layout_set_font_description(pl, pf);
    std::string text = error.empty() ? "> " + input + "_" : error;
    pango_layout_set_text(pl, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(pl, &tw, &th);
    pango_font_description_free(pf);
    g_object_unref(pl);
    cairo_destroy(pcr);
    cairo_surface_destroy(probe);

    int w = std::clamp(tw + PADDING * 2 + BORDER * 2, 320, std::max(320, ow - 80));

    if (w != width || h != height || !pix) {
        if (buf) {
            wlr_scene_node_destroy(&buf->node);
            buf = nullptr;
        }
        if (surf) {
            cairo_surface_destroy(surf);
            surf = nullptr;
        }
        if (pix) {
            wlr_buffer_drop(&pix->base);
            pix = nullptr;
        }
        pix = new PixBuf;
        pix->data = (unsigned char *)malloc((size_t)w * h * 4);
        if (!pix->data) {
            delete pix;
            pix = nullptr;
            wlr_log(WLR_ERROR, "launcher: cannot allocate pixels");
            return;
        }
        wlr_buffer_init(&pix->base, &pixbuf_impl, w, h);
        surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
        buf = wlr_scene_buffer_create(server->scene_overlay, &pix->base);
        if (!buf || !surf) {
            wlr_log(WLR_ERROR, "launcher: cannot create buffer");
            return;
        }
        width = w;
        height = h;
    } else {
        cairo_surface_flush(surf);
    }

    cairo_t *cr = cairo_create(surf);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_set_line_width(cr, BORDER);
    cairo_rectangle(cr, BORDER / 2.0, BORDER / 2.0, w - BORDER, h - BORDER);
    cairo_stroke(cr);

    PangoLayout *layout = pango_cairo_create_layout(cr);
    PangoFontDescription *font = pango_font_description_from_string(server->settings.font.c_str());
    pango_layout_set_font_description(layout, font);
    pango_layout_set_text(layout, text.c_str(), -1);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_move_to(cr, PADDING, (h - th) / 2.0);
    pango_cairo_show_layout(cr, layout);
    pango_font_description_free(font);
    g_object_unref(layout);
    cairo_destroy(cr);
    copy_surface(surf, pix, w, h);

    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 0, 0, w, h);
    wlr_scene_buffer_set_buffer_with_damage(buf, &pix->base, &damage);
    pixman_region32_fini(&damage);
    wlr_scene_node_set_position(&buf->node, (ow - w) / 2, (oh - h) / 2);
    wlr_scene_node_set_enabled(&buf->node, open || fade.active);
    wlr_scene_buffer_set_opacity(buf, open ? 1.0f : 0.0f);
}

void Launcher::relayout() {
    render();
}

void Launcher::animate(uint64_t now) {
    if (error_until && now >= error_until) {
        error_until = 0;
        error.clear();
        render();
    }
    if (!fade.active || !buf) return;
    float o = 0.0f;
    bool done = fade.step(now, &o);
    wlr_scene_buffer_set_opacity(buf, o);
    if (done && !open) wlr_scene_node_set_enabled(&buf->node, false);
}
