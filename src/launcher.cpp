#include "vlswm/launcher.h"
#include "vlswm/overlay.h"
#include "vlswm/server.h"

#include <cairo.h>
#include <pango/pangocairo.h>
#include <wlr/util/log.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

static const int BOX_HEIGHT = 56;
static const int ROW_HEIGHT = 34;
static const int BORDER = 2;
static const int PADDING = 16;

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
                i++;
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

    app_dirs = dirs;
    app_mtimes.clear();
    for (const auto &d : app_dirs) {
        struct stat st;
        app_mtimes.push_back(stat(d.c_str(), &st) == 0 ? st.st_mtime : 0);
    }
}

bool Launcher::apps_changed() {
    if (app_dirs.empty()) return true;
    for (size_t i = 0; i < app_dirs.size(); i++) {
        struct stat st;
        time_t mtime = stat(app_dirs[i].c_str(), &st) == 0 ? st.st_mtime : 0;
        if (mtime != app_mtimes[i]) return true;
    }
    return false;
}

Launcher::~Launcher() {
    if (font_desc) pango_font_description_free(font_desc);
}

void Launcher::init(Server *s) {
    server = s;
}

bool Launcher::busy() const {
    return fade.active;
}

const DesktopApp *Launcher::suggestion() const {
    std::string q = lower(trim(input));
    if (q.empty()) return nullptr;
    const DesktopApp *best = nullptr;
    int best_score = 4;
    size_t best_len = SIZE_MAX;
    for (const auto &app : apps) {
        size_t pos = app.match.find(q);
        if (pos == std::string::npos) continue;
        int score = 3;
        if (pos == 0) score = app.match.size() == q.size() ? 0 : 1;
        else if (app.match[pos - 1] == ' ') score = 2;
        if (score > best_score) continue;
        if (score == best_score && app.name.size() >= best_len) continue;
        best = &app;
        best_score = score;
        best_len = app.name.size();
    }
    return best;
}

void Launcher::toggle() {
    if (open) {
        close();
        return;
    }
    open = true;
    input.clear();
    if (!initialized) {
        initialized = true;
        font_desc = pango_font_description_from_string(server->settings.font.c_str());
        scan_apps();
    } else if (apps.empty() || apps_changed()) {
        scan_apps();
    }
    render();
    if (ov.node) wlr_scene_node_set_enabled(&ov.node->node, true);

    if (server->settings.launcher_duration_ns == 0) {
        fade.active = false;
        if (ov.node) wlr_scene_buffer_set_opacity(ov.node, 1.0f);
    } else {
        fade.begin(anim_now_ns(), server->settings.launcher_duration_ns,
                   server->settings.easing, 0.0f, 1.0f);
    }
    server->request_frame();
}

void Launcher::close() {
    if (!open) return;
    open = false;

    if (server->settings.launcher_duration_ns == 0) {
        fade.active = false;
        if (ov.node) {
            wlr_scene_buffer_set_opacity(ov.node, 0.0f);
            wlr_scene_node_set_enabled(&ov.node->node, false);
        }
        server->request_frame();
        return;
    }

    fade.begin(anim_now_ns(), server->settings.launcher_duration_ns,
               server->settings.easing, 1.0f, 0.0f);
    server->request_frame();
}

void Launcher::launch(const DesktopApp &app) {
    server->ensure_satellite();
    if (app.terminal) {
        server->spawn_terminal_command(app.exec);
    } else {
        server->spawn_argv({"/bin/sh", "-c", app.exec});
    }
}

void Launcher::key(xkb_keysym_t sym, const std::string &utf8) {
    if (!open) return;

    if (sym == XKB_KEY_Escape) {
        close();
        return;
    }
    if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
        const DesktopApp *app = suggestion();
        if (!app) return;
        launch(*app);
        close();
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
    const DesktopApp *app = suggestion();
    int h = BOX_HEIGHT + (app ? ROW_HEIGHT : 0);
    int max_w = std::max(320, ow - 80);
    bool white = server->theme_white;
    double bg = white ? 1.0 : 0.0;
    double fg = white ? 0.0 : 1.0;

    if (!ov.ensure(server->scene_overlay, max_w, h)) {
        wlr_log(WLR_ERROR, "launcher: cannot create buffer");
        return;
    }

    cairo_t *cr = cairo_create(ov.surf);
    PangoLayout *layout = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(layout, font_desc);
    std::string text = "> " + input + "_";
    pango_layout_set_text(layout, text.c_str(), -1);
    int tw = 0, th = 0;
    pango_layout_get_pixel_size(layout, &tw, &th);

    PangoLayout *sub = pango_cairo_create_layout(cr);
    pango_layout_set_font_description(sub, font_desc);
    int sw = 0, sh = 0;
    if (app) {
        pango_layout_set_text(sub, app->name.c_str(), -1);
        pango_layout_get_pixel_size(sub, &sw, &sh);
    }

    int w = std::clamp(std::max(tw, sw) + PADDING * 2 + BORDER * 2, 320, max_w);

    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgb(cr, bg, bg, bg);
    cairo_rectangle(cr, 0, 0, w, h);
    cairo_fill(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_set_source_rgb(cr, fg, fg, fg);
    cairo_set_line_width(cr, BORDER);
    cairo_rectangle(cr, BORDER / 2.0, BORDER / 2.0, w - BORDER, h - BORDER);
    cairo_stroke(cr);
    cairo_move_to(cr, PADDING, (BOX_HEIGHT - th) / 2.0);
    pango_cairo_show_layout(cr, layout);

    if (app) {
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, BORDER, BOX_HEIGHT);
        cairo_line_to(cr, w - BORDER, BOX_HEIGHT);
        cairo_stroke(cr);
        cairo_move_to(cr, PADDING, BOX_HEIGHT + (ROW_HEIGHT - sh) / 2.0);
        pango_cairo_show_layout(cr, sub);
    }

    g_object_unref(sub);
    g_object_unref(layout);
    cairo_destroy(cr);
    ov.commit(w, h);
    wlr_scene_node_set_position(&ov.node->node, (ow - w) / 2, (oh - BOX_HEIGHT) / 2);
    wlr_scene_node_set_enabled(&ov.node->node, open || fade.active);
    wlr_scene_buffer_set_opacity(ov.node, open ? 1.0f : 0.0f);
}

void Launcher::relayout() {
    if (!initialized) return;
    if (font_desc) pango_font_description_free(font_desc);
    font_desc = pango_font_description_from_string(server->settings.font.c_str());
    render();
}

void Launcher::reload() {
    if (!initialized) return;
    scan_apps();
    relayout();
}

void Launcher::animate(uint64_t now) {
    if (!fade.active || !ov.node) return;
    float o = 0.0f;
    bool done = fade.step(now, &o);
    wlr_scene_buffer_set_opacity(ov.node, o);
    if (done && !open) wlr_scene_node_set_enabled(&ov.node->node, false);
}
