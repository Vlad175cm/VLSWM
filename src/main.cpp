
#include "vlswm/server.h"
#include "vlswm/ini.h"
#include "vlswm/wlroots.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <execinfo.h>
#include <getopt.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static Server *g_server = nullptr;

static FILE *g_logfile = nullptr;

static void log_callback(enum wlr_log_importance importance, const char *fmt, va_list args) {
    if (g_logfile) {
        vfprintf(g_logfile, fmt, args);
        fflush(g_logfile);
    }
    (void)importance;
}

static void open_logfile() {
    const char *state = std::getenv("XDG_STATE_HOME");
    std::string dir;
    if (state) {
        dir = std::string(state) + "/vlswm";
    } else {
        const char *home = std::getenv("HOME");
        dir = home ? std::string(home) + "/.local/state/vlswm" : "/tmp/vlswm";
    }
    std::string cmd = "mkdir -p \"" + dir + "\"";
    (void)system(cmd.c_str());
    std::string path = dir + "/vlswm.log";
    g_logfile = std::fopen(path.c_str(), "a");
    if (g_logfile) {
        std::fprintf(g_logfile, "\n=== vlswm session start (pid %d) ===\n", (int)getpid());
    }
}

static void handle_signal(int sig) {
    (void)sig;
    if (g_server && g_server->wl_display) {
        wl_display_terminate(g_server->wl_display);
    }
}



static void crash_handler(int sig) {
    void *frames[48];
    int n = backtrace(frames, 48);
    std::fprintf(stderr, "\n=== vlswm CRASH signal=%d ===\n", sig);
    backtrace_symbols_fd(frames, n, 2);
    if (g_logfile) {
        std::fprintf(g_logfile, "\n=== vlswm CRASH signal=%d ===\n", sig);
        backtrace_symbols_fd(frames, n, fileno(g_logfile));
        std::fflush(g_logfile);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void print_usage(const char *argv0) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -c, --config <path>   Use this config file (default: ~/.config/vlswm/config)\n"
        "  -v, --version         Print version\n"
        "  -h, --help            Show this help\n",
        argv0);
}

int main(int argc, char **argv) {
    open_logfile();
    wlr_log_init(WLR_DEBUG, log_callback);

    std::string config_path;
    const char *home = std::getenv("HOME");
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg) {
        config_path = std::string(xdg) + "/vlswm/config";
    } else if (home) {
        config_path = std::string(home) + "/.config/vlswm/config";
    }

    static struct option long_opts[] = {
        {"config", required_argument, nullptr, 'c'},
        {"version", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "c:vh", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'c': config_path = optarg; break;
            case 'v': std::fprintf(stdout, "vlswm 0.1.0\n"); return 0;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    auto cfg = IniConfig::load(config_path);
    if (!cfg && !config_path.empty()) {
        wlr_log(WLR_DEBUG, "No config at %s, using defaults", config_path.c_str());
    }

    Server server;
    g_server = &server;
    if (!server.init(cfg ? &*cfg : nullptr)) {
        wlr_log(WLR_ERROR, "Failed to initialize server");
        return 1;
    }

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);

    wlr_log(WLR_INFO, "vlswm started, running event loop");
    server.run();

    server.finish();
    g_server = nullptr;
    wlr_log(WLR_INFO, "vlswm shutting down");
    return 0;
}
