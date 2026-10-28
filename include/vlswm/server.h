#pragma once

#include "vlswm/wlroots.h"
#include "vlswm/animation.h"
#include "vlswm/ini.h"

#include <xkbcommon/xkbcommon.h>

#include <sys/types.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

struct View;
struct Launcher;

struct Output {
    struct wlr_output *wlr_output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct Settings {
    uint64_t duration_ns = 300ULL * 1000000ULL;
    Easing easing = Easing::EaseOut;
    int repeat_delay = 200;
    int repeat_rate = 40;
    double scroll_speed = 0.5;
    std::string layout = "us,ru";
    std::string layout_options = "grp:win_space_toggle";
    std::string terminal = "ghostty";
    std::string file_manager = "yazi";
    std::string font = "JetBrainsMono Nerd Font 16";
    std::string satellite = "xwayland-satellite";
    std::string satellite_display = ":1";
    std::string ghostty_dir;
};

enum class BindAction {
    Close,
    Exit,
    Reload,
    FocusNext,
    FocusPrev,
    Theme,
    Launcher,
    FileManager,
    NewTerminal,
    RestartTerminal,
    Spawn,
};

struct Binding {
    uint32_t mods = 0;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;
    BindAction action = BindAction::Spawn;
    std::string cmd;
    bool repeat = false;
};

enum class RepeatKind {
    None,
    Binding,
    LauncherKey,
};

struct Repeat {
    RepeatKind kind = RepeatKind::None;
    int binding = -1;
    xkb_keysym_t sym = XKB_KEY_NoSymbol;
    std::string utf8;
    uint32_t keycode = 0;
};

struct Server {
    std::unique_ptr<IniConfig> config;
    Settings settings;
    std::vector<Binding> bindings;
    std::map<std::string, std::string> theme_base;
    bool theme_white = false;

    struct wl_display *wl_display = nullptr;
    struct wlr_backend *backend = nullptr;
    struct wlr_renderer *renderer = nullptr;
    struct wlr_allocator *allocator = nullptr;

    struct wlr_compositor *compositor = nullptr;
    struct wlr_xdg_shell *xdg_shell = nullptr;
    struct wlr_layer_shell_v1 *layer_shell = nullptr;
    struct wlr_linux_dmabuf_v1 *linux_dmabuf = nullptr;
    struct wlr_xdg_activation_v1 *xdg_activation = nullptr;
    struct wlr_xdg_decoration_manager_v1 *decoration_mgr = nullptr;

    struct wlr_scene *scene = nullptr;
    struct wlr_scene_tree *scene_background = nullptr;
    struct wlr_scene_tree *scene_apps = nullptr;
    struct wlr_scene_tree *scene_overlay = nullptr;
    struct wlr_scene_tree *scene_layers = nullptr;
    struct wlr_scene_rect *background = nullptr;

    struct wlr_seat *seat = nullptr;
    struct wlr_cursor *cursor = nullptr;
    struct wlr_xcursor_manager *cursor_mgr = nullptr;
    struct wlr_output_layout *output_layout = nullptr;
    struct wlr_scene_output_layout *scene_output_layout = nullptr;

    struct wlr_keyboard *keyboard = nullptr;
    struct wlr_input_device *pointer_device = nullptr;

    std::vector<View *> stack;
    std::vector<Output *> outputs;
    Launcher *launcher = nullptr;

    pid_t base_pid = 0;
    pid_t satellite_pid = 0;
    struct wl_event_source *respawn_timer = nullptr;

    Repeat repeat;
    struct wl_event_source *repeat_timer = nullptr;
    std::vector<pid_t> children;

    struct wl_listener new_output = {};
    struct wl_listener new_input = {};
    struct wl_listener new_xdg_surface = {};
    struct wl_listener new_layer_surface = {};
    struct wl_listener new_decoration = {};
    struct wl_listener cursor_motion = {};
    struct wl_listener cursor_motion_absolute = {};
    struct wl_listener cursor_button = {};
    struct wl_listener cursor_axis = {};
    struct wl_listener cursor_frame = {};
    struct wl_listener request_set_selection = {};
    struct wl_listener request_set_primary_selection = {};
    struct wl_listener request_start_drag = {};
    struct wl_listener request_activate = {};

    bool init(std::unique_ptr<IniConfig> cfg);
    void run();
    void finish();
    void reload_config();

    void load_settings();
    void load_bindings();
    void check_config();
    int find_binding(xkb_keysym_t sym, uint32_t mods) const;
    void run_action(int idx);
    void start_repeat(const Repeat &r);
    void stop_repeat(uint32_t keycode);
    void spawn(const std::string &cmd);
    pid_t spawn_argv(const std::vector<std::string> &argv);
    void spawn_file_manager();
    pid_t spawn_terminal_command(const std::string &cmd);
    void start_terminal();
    void spawn_new_terminal();
    void terminal_gone();
    void start_satellite();
    void ghostty_setup_theme();

    View *top_view();
    View *focused_view();
    void focus_view(View *view, struct wlr_surface *surface);
    void update_visibility();
    void focus_top();
    void cycle_next();
    void cycle_prev();
    void close_top();
    void close_view(View *view);
    void restart_terminal();
    void set_theme(bool white);
    void configure_all();
    void request_frame();
    void animate(uint64_t now);
    bool animations_running() const;
};

Output *create_output(Server *server, struct wlr_output *wlr_output);
View *desktop_view_at(Server *server, double lx, double ly,
                      struct wlr_surface **surface, double *sx, double *sy);
