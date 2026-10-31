#include "vlswm/server.h"
#include "vlswm/wlroots.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <execinfo.h>
#include <filesystem>
#include <getopt.h>
#include <signal.h>
#include <string>
#include <unistd.h>

#ifndef VLSWM_VERSION
#define VLSWM_VERSION "1.1.1"
#endif

static Server *g_server = nullptr;
static FILE *g_logfile = nullptr;

static void log_callback(enum wlr_log_importance importance, const char *fmt, va_list args) {
    if (importance > WLR_ERROR) return;
    va_list copy;
    va_copy(copy, args);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    if (g_logfile) {
        std::vfprintf(g_logfile, fmt, copy);
        std::fputc('\n', g_logfile);
        std::fflush(g_logfile);
    }
    va_end(copy);
}

static void open_logfile() {
    const char *state = std::getenv("XDG_STATE_HOME");
    std::string dir;
    if (state && *state) {
        dir = std::string(state) + "/vlswm";
    } else {
        const char *home = std::getenv("HOME");
        dir = home ? std::string(home) + "/.local/state/vlswm" : "/tmp/vlswm";
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    g_logfile = std::fopen((dir + "/vlswm.log").c_str(), "a");
    if (g_logfile) {
        std::fprintf(g_logfile, "=== vlswm started (pid %d) ===\n", (int)getpid());
        std::fflush(g_logfile);
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
    std::fprintf(stderr, "vlswm crash signal=%d\n", sig);
    backtrace_symbols_fd(frames, n, 2);
    if (g_logfile) {
        std::fprintf(g_logfile, "vlswm crash signal=%d\n", sig);
        backtrace_symbols_fd(frames, n, fileno(g_logfile));
        std::fflush(g_logfile);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void print_usage(const char *argv0) {
    std::fprintf(stderr,
        "Usage: %s [OPTIONS]\n"
        "  -c, --config <path>   config file (default: ~/.config/vlswm/config)\n"
        "  -v, --version         print version\n"
        "  -h, --help            show this help\n",
        argv0);
}

static std::string default_config_path() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/vlswm/config";
    const char *home = std::getenv("HOME");
    return home ? std::string(home) + "/.config/vlswm/config" : std::string();
}

int main(int argc, char **argv) {
    open_logfile();
    wlr_log_init(WLR_ERROR, log_callback);

    std::string config_path = default_config_path();

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
            case 'v': std::fprintf(stdout, "vlswm %s\n", VLSWM_VERSION); return 0;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    auto cfg = std::make_unique<IniConfig>(IniConfig::load_or_empty(config_path));

    Server server;
    g_server = &server;
    if (!server.init(std::move(cfg))) {
        wlr_log(WLR_ERROR, "failed to initialize server");
        return 1;
    }

    signal(SIGTERM, handle_signal);
    signal(SIGINT, handle_signal);
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGBUS, crash_handler);

    server.run();
    server.finish();
    g_server = nullptr;
    if (g_logfile) std::fclose(g_logfile);
    return 0;
}
