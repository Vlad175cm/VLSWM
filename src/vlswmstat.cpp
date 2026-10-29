#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/utsname.h>
#include <termios.h>
#include <unistd.h>
#include <vector>

#ifndef VLSWM_VERSION
#define VLSWM_VERSION "0.4.0"
#endif

static volatile sig_atomic_t g_resize = 0;
static volatile sig_atomic_t g_quit = 0;

static void on_winch(int) { g_resize = 1; }
static void on_quit(int) { g_quit = 1; }

static std::string trim(const std::string &s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string read_file(const std::string &path) {
    std::ifstream in(path);
    if (!in) return "";
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string read_line(const std::string &path) {
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);
    return trim(line);
}

static int utf8_len(const std::string &s) {
    int n = 0;
    for (unsigned char c : s) {
        if ((c & 0xC0) != 0x80) n++;
    }
    return n;
}

static std::string pad_to(const std::string &s, int width) {
    std::string out = s;
    int n = utf8_len(out);
    if (n > width) {
        int keep = width;
        std::string cut;
        for (unsigned char c : out) {
            if ((c & 0xC0) != 0x80) {
                if (keep == 0) break;
                keep--;
            }
            cut += (char)c;
        }
        out = cut;
        n = width;
    }
    if (n < width) out.append((size_t)(width - n), ' ');
    return out;
}

static std::string repeat(const std::string &s, int n) {
    std::string out;
    for (int i = 0; i < n; i++) out += s;
    return out;
}

static std::string human_bytes(double bytes) {
    static const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int u = 0;
    while (bytes >= 1024.0 && u < 4) {
        bytes /= 1024.0;
        u++;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", bytes, units[u]);
    return buf;
}

static std::string human_rate(double bytes_per_sec) {
    return human_bytes(bytes_per_sec) + "/s";
}

static std::string human_disk(double used, double total) {
    if (total >= 1024.0 * 1024 * 1024) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.0f / %.0f GB",
                      used / (1024.0 * 1024 * 1024), total / (1024.0 * 1024 * 1024));
        return buf;
    }
    return human_bytes(used) + " / " + human_bytes(total);
}

static std::string bar(double frac, int width) {
    frac = std::clamp(frac, 0.0, 1.0);
    int fill = (int)std::lround(frac * width);
    return "[" + repeat("█", fill) + repeat("░", width - fill) + "]";
}

static std::string percent(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d%%", (int)std::lround(v));
    return buf;
}

static std::string uptime_text(double seconds) {
    uint64_t s = (uint64_t)seconds;
    uint64_t d = s / 86400;
    uint64_t h = (s % 86400) / 3600;
    uint64_t m = (s % 3600) / 60;
    char buf[64];
    if (d) std::snprintf(buf, sizeof(buf), "%llud %02llu:%02llu",
                         (unsigned long long)d, (unsigned long long)h,
                         (unsigned long long)m);
    else std::snprintf(buf, sizeof(buf), "%02llu:%02llu",
                       (unsigned long long)h, (unsigned long long)m);
    return buf;
}

static double uptime_seconds() {
    std::ifstream in("/proc/uptime");
    double up = 0;
    in >> up;
    return up;
}

static std::string distro_name() {
    std::ifstream in("/etc/os-release");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("PRETTY_NAME=", 0) != 0) continue;
        std::string v = trim(line.substr(12));
        if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'')) {
            v = v.substr(1, v.size() - 2);
        }
        return v;
    }
    return "Linux";
}

struct CpuSample {
    uint64_t total = 0;
    uint64_t idle = 0;
};

static CpuSample read_cpu_total() {
    CpuSample s;
    std::ifstream in("/proc/stat");
    std::string line;
    if (!std::getline(in, line)) return s;
    std::istringstream ss(line);
    std::string cpu;
    ss >> cpu;
    uint64_t v = 0;
    for (int i = 0; i < 8 && (ss >> v); i++) {
        s.total += v;
        if (i == 3 || i == 4) s.idle += v;
    }
    return s;
}

static double cpu_usage(const CpuSample &prev, const CpuSample &cur) {
    if (cur.total <= prev.total) return 0.0;
    uint64_t dt = cur.total - prev.total;
    uint64_t di = cur.idle > prev.idle ? cur.idle - prev.idle : 0;
    return 100.0 * (double)(dt - di) / (double)dt;
}

static std::string cpu_model() {
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0) {
            size_t p = line.find(':');
            if (p != std::string::npos) return trim(line.substr(p + 1));
        }
    }
    return "unknown";
}

static double cpu_ghz() {
    std::ifstream in("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
    double khz = 0;
    if (in >> khz) return khz / 1000000.0;
    for (int i = 0; i < 4; i++) {
        std::string path = "/sys/devices/system/cpu/cpu" + std::to_string(i) +
                           "/cpufreq/scaling_cur_freq";
        std::ifstream f(path);
        if (f >> khz) return khz / 1000000.0;
    }
    return 0.0;
}

static void read_mem(uint64_t *total, uint64_t *avail, uint64_t *stotal, uint64_t *sfree) {
    *total = *avail = *stotal = *sfree = 0;
    std::ifstream in("/proc/meminfo");
    std::string key;
    uint64_t val = 0;
    std::string unit;
    while (in >> key >> val >> unit) {
        if (key == "MemTotal:") *total = val;
        else if (key == "MemAvailable:") *avail = val;
        else if (key == "SwapTotal:") *stotal = val;
        else if (key == "SwapFree:") *sfree = val;
    }
    *total *= 1024;
    *avail *= 1024;
    *stotal *= 1024;
    *sfree *= 1024;
}

struct DiskInfo {
    std::string mount;
    uint64_t total = 0;
    uint64_t free = 0;
};

static bool disk_info(const std::string &path, DiskInfo *out) {
    struct statvfs st;
    if (statvfs(path.c_str(), &st) != 0) return false;
    out->mount = path;
    out->total = (uint64_t)st.f_blocks * st.f_frsize;
    out->free = (uint64_t)st.f_bavail * st.f_frsize;
    return out->total > 0;
}

static std::string net_iface(std::string *ip) {
    ip->clear();
    struct ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0) return "";
    std::string best;
    for (struct ifaddrs *a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (a->ifa_flags & IFF_LOOPBACK) continue;
        std::string name = a->ifa_name;
        char buf[INET_ADDRSTRLEN] = {0};
        auto *sin = (struct sockaddr_in *)a->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf));
        if (!best.empty() && (a->ifa_flags & IFF_UP) == 0) continue;
        best = name;
        *ip = buf;
        if (a->ifa_flags & IFF_UP) break;
    }
    freeifaddrs(list);
    return best;
}

static uint64_t net_bytes(const std::string &iface, const char *file) {
    std::ifstream in("/sys/class/net/" + iface + "/statistics/" + file);
    uint64_t v = 0;
    in >> v;
    return v;
}

static std::string temperature() {
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return "";
    double best = -1;
    std::string label;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.') continue;
        std::string base = "/sys/class/hwmon/" + std::string(de->d_name);
        std::string name = read_line(base + "/name");
        for (int i = 1; i <= 8; i++) {
            std::string f = base + "/temp" + std::to_string(i) + "_input";
            std::ifstream in(f);
            double milli = 0;
            if (!(in >> milli)) continue;
            double c = milli / 1000.0;
            if (c > best) {
                best = c;
                label = name;
            }
        }
    }
    closedir(dir);
    if (best < 0) return "";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f °C", best);
    return buf;
}

static std::string fan_rpm() {
    DIR *dir = opendir("/sys/class/hwmon");
    if (!dir) return "";
    std::string result;
    struct dirent *de;
    while ((de = readdir(dir)) && result.empty()) {
        if (de->d_name[0] == '.') continue;
        std::string base = "/sys/class/hwmon/" + std::string(de->d_name);
        for (int i = 1; i <= 4; i++) {
            std::ifstream in(base + "/fan" + std::to_string(i) + "_input");
            int rpm = 0;
            if (in >> rpm && rpm > 0) {
                result = std::to_string(rpm) + " RPM";
                break;
            }
        }
    }
    closedir(dir);
    return result;
}

static std::string battery() {
    DIR *dir = opendir("/sys/class/power_supply");
    if (!dir) return "";
    std::string result;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (strncmp(de->d_name, "BAT", 3) != 0) continue;
        std::string base = "/sys/class/power_supply/" + std::string(de->d_name);
        std::string cap = read_line(base + "/capacity");
        std::string status = read_line(base + "/status");
        if (!cap.empty()) {
            result = cap + "%";
            if (!status.empty()) result += " (" + status + ")";
            break;
        }
    }
    closedir(dir);
    return result;
}

static std::string theme_name() {
    const char *state = getenv("XDG_STATE_HOME");
    std::string state_dir;
    if (state && *state) {
        state_dir = std::string(state) + "/vlswm";
    } else {
        const char *home = getenv("HOME");
        if (home) state_dir = std::string(home) + "/.local/state/vlswm";
    }
    if (!state_dir.empty()) {
        std::string name = trim(read_line(state_dir + "/theme"));
        if (name == "white" || name == "black") return name;
    }

    const char *home = getenv("HOME");
    if (!home) return "unknown";
    std::ifstream in(std::string(home) + "/.config/ghostty/vlswm-theme.ghostty");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("background", 0) != 0) continue;
        size_t p = line.find('#');
        if (p == std::string::npos) continue;
        std::string hex = line.substr(p);
        return hex == "#ffffff" ? "white" : "black";
    }
    return "unknown";
}

static double process_uptime(const char *name) {
    DIR *dir = opendir("/proc");
    if (!dir) return -1;
    double result = -1;
    struct dirent *de;
    while ((de = readdir(dir))) {
        if (!isdigit((unsigned char)de->d_name[0])) continue;
        std::string base = std::string("/proc/") + de->d_name;
        if (read_line(base + "/comm") != name) continue;
        std::string stat = read_file(base + "/stat");
        size_t rp = stat.rfind(')');
        if (rp == std::string::npos) continue;
        std::istringstream ss(stat.substr(rp + 2));
        std::string tok;
        for (int i = 0; i < 19; i++) ss >> tok;
        uint64_t start = 0;
        if (!(ss >> start)) continue;
        double ticks = (double)sysconf(_SC_CLK_TCK);
        result = uptime_seconds() - (double)start / ticks;
        break;
    }
    closedir(dir);
    return result;
}

struct Snapshot {
    std::string host;
    std::string distro;
    std::string kernel;
    std::string arch;
    double uptime = 0;
    std::string shell;
    std::string term;

    std::string cpu;
    long cores = 1;
    double load[3] = {0, 0, 0};
    double cpu_pct = 0;
    double ghz = 0;

    uint64_t mem_total = 0, mem_avail = 0;
    uint64_t swap_total = 0, swap_free = 0;
    std::vector<DiskInfo> disks;

    std::string iface;
    std::string ip;
    double rx = 0, tx = 0;

    std::string temp;
    std::string fan;
    std::string batt;

    std::string theme;
    double session_uptime = -1;
};

static Snapshot snapshot(const CpuSample &prev_cpu, double elapsed,
                         const std::string &prev_iface,
                         uint64_t prev_rx, uint64_t prev_tx, double prev_time,
                         double now, bool rates) {
    Snapshot s;
    char host[256] = {0};
    if (gethostname(host, sizeof(host) - 1) == 0) s.host = host;
    s.distro = distro_name();
    struct utsname un;
    if (uname(&un) == 0) {
        s.kernel = un.release;
        s.arch = un.machine;
    }
    s.uptime = uptime_seconds();
    const char *shell = getenv("SHELL");
    if (shell) {
        std::string p = shell;
        size_t slash = p.find_last_of('/');
        s.shell = slash == std::string::npos ? p : p.substr(slash + 1);
    }
    const char *term = getenv("TERM_PROGRAM");
    if (!term || !*term) term = getenv("TERM");
    s.term = term ? term : "terminal";

    s.cpu = cpu_model();
    s.cores = sysconf(_SC_NPROCESSORS_ONLN);
    std::ifstream load("/proc/loadavg");
    load >> s.load[0] >> s.load[1] >> s.load[2];
    s.cpu_pct = cpu_usage(prev_cpu, read_cpu_total());
    s.ghz = cpu_ghz();
    (void)elapsed;

    read_mem(&s.mem_total, &s.mem_avail, &s.swap_total, &s.swap_free);

    DiskInfo root;
    if (disk_info("/", &root)) s.disks.push_back(root);
    struct stat st_root, st_home;
    if (stat("/home", &st_home) == 0 && stat("/", &st_root) == 0 &&
        st_home.st_dev != st_root.st_dev) {
        DiskInfo home;
        if (disk_info("/home", &home)) s.disks.push_back(home);
    }

    s.iface = net_iface(&s.ip);
    if (rates && !s.iface.empty()) {
        uint64_t rx = net_bytes(s.iface, "rx_bytes");
        uint64_t tx = net_bytes(s.iface, "tx_bytes");
        if (s.iface == prev_iface && now > prev_time) {
            double dt = now - prev_time;
            s.rx = (double)(rx - prev_rx) / dt;
            s.tx = (double)(tx - prev_tx) / dt;
        }
    }

    s.temp = temperature();
    s.fan = fan_rpm();
    s.batt = battery();
    s.theme = theme_name();
    s.session_uptime = process_uptime("vlswm");
    return s;
}

static const char *DIGITS[10][5] = {
    {"█████", "█   █", "█   █", "█   █", "█████"},
    {"   ██", "    █", "    █", "    █", "    █"},
    {"█████", "    █", "█████", "█    ", "█████"},
    {"█████", "    █", "█████", "    █", "█████"},
    {"█   █", "█   █", "█████", "    █", "    █"},
    {"█████", "█    ", "█████", "    █", "█████"},
    {"█████", "█    ", "█████", "█   █", "█████"},
    {"█████", "    █", "   █ ", "  █  ", "  █  "},
    {"█████", "█   █", "█████", "█   █", "█████"},
    {"█████", "█   █", "█████", "    █", "█████"},
};

static void add_clock(std::vector<std::string> &out, int width) {
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[16];
    char datebuf[32];
    std::strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tm);
    std::strftime(datebuf, sizeof(datebuf), "%d.%m.%Y", &tm);

    if (width < 44) {
        out.push_back(pad_to(std::string(timebuf) + "   " + datebuf, width));
        out.push_back("");
        return;
    }

    std::string glyph[5];
    for (int row = 0; row < 5; row++) {
        std::string line;
        for (int i = 0; timebuf[i]; i++) {
            if (timebuf[i] == ':') {
                line += (row == 1 || row == 3) ? "██" : "  ";
            } else {
                line += DIGITS[timebuf[i] - '0'][row];
            }
            line += " ";
        }
        glyph[row] = line;
    }
    for (int row = 0; row < 5; row++) {
        out.push_back(pad_to("  " + glyph[row], width));
    }
    out.push_back(pad_to("  " + std::string(datebuf), width));
    out.push_back("");
}

static void add_section(std::vector<std::string> &out, const std::string &title,
                        const std::vector<std::pair<std::string, std::string>> &rows,
                        int width) {
    std::string head = "─ " + title + " ";
    int fill = width - utf8_len(head);
    if (fill < 1) fill = 1;
    out.push_back(pad_to(head + repeat("─", fill), width));
    for (const auto &r : rows) {
        std::string line = "  " + pad_to(r.first, 9) + r.second;
        out.push_back(pad_to(line, width));
    }
}

static std::vector<std::string> build_frame(const Snapshot &s, int cols, int rows) {
    int width = std::max(40, cols - 1);
    bool two = cols >= 80 && rows >= 22;
    int lw = two ? (width - 3) / 2 : width;
    int rw = two ? (width - 3 - lw) : width;

    std::vector<std::string> left;
    add_clock(left, lw);

    add_section(left, "SYSTEM", {
        {"host", s.host},
        {"distro", s.distro},
        {"kernel", s.kernel},
        {"uptime", uptime_text(s.uptime)},
        {"shell", s.shell},
        {"term", s.term},
    }, lw);

    std::string freq;
    if (s.ghz > 0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f GHz", s.ghz);
        freq = buf;
    }
    add_section(left, "CPU", {
        {"model", s.cpu},
        {"cores", std::to_string(s.cores)},
        {"load", [&] {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", s.load[0], s.load[1], s.load[2]);
            return std::string(buf);
        }()},
        {"usage", bar(s.cpu_pct / 100.0, 14) + " " + percent(s.cpu_pct)},
        {"freq", freq},
    }, lw);

    int rbar = rw >= 48 ? 14 : 8;
    std::vector<std::string> right;
    double mem_used = (double)(s.mem_total - s.mem_avail);
    double swap_used = (double)(s.swap_total - s.swap_free);
    add_section(right, "MEMORY", {
        {"RAM", bar(s.mem_total ? mem_used / s.mem_total : 0, rbar) + " " +
                 human_bytes(mem_used) + " / " + human_bytes((double)s.mem_total)},
        {"swap", bar(s.swap_total ? swap_used / s.swap_total : 0, rbar) + " " +
                 human_bytes(swap_used) + " / " + human_bytes((double)s.swap_total)},
    }, rw);

    std::vector<std::pair<std::string, std::string>> disk_rows;
    for (const auto &d : s.disks) {
        double used = (double)(d.total - d.free);
        disk_rows.push_back({d.mount, bar(d.total ? used / d.total : 0, rbar) + " " +
                                       human_disk(used, (double)d.total)});
    }
    add_section(right, "DISKS", disk_rows, rw);

    std::vector<std::pair<std::string, std::string>> net_rows;
    net_rows.push_back({"iface", s.iface.empty() ? "offline" : s.iface});
    net_rows.push_back({"ip", s.ip.empty() ? "-" : s.ip});
    net_rows.push_back({"rate", "↓ " + human_rate(s.rx) + "  ↑ " + human_rate(s.tx)});
    add_section(right, "NETWORK", net_rows, rw);

    std::vector<std::pair<std::string, std::string>> sensor_rows;
    if (!s.temp.empty()) sensor_rows.push_back({"cpu", s.temp});
    if (!s.fan.empty()) sensor_rows.push_back({"fan", s.fan});
    if (!s.batt.empty()) sensor_rows.push_back({"battery", s.batt});
    if (!sensor_rows.empty()) add_section(right, "SENSORS", sensor_rows, rw);

    std::vector<std::pair<std::string, std::string>> vlswm_rows;
    vlswm_rows.push_back({"version", VLSWM_VERSION});
    vlswm_rows.push_back({"theme", s.theme});
    if (s.session_uptime >= 0) {
        vlswm_rows.push_back({"session", uptime_text(s.session_uptime)});
    }
    add_section(right, "VLSWM", vlswm_rows, rw);

    std::vector<std::string> frame;
    std::string header = "VLSWM // SYSTEM";
    std::string head_line = pad_to(header, width - utf8_len(s.host) - 1) + " " + s.host;
    frame.push_back("\x1b[7m" + pad_to(head_line, width) + "\x1b[0m");
    frame.push_back("");

    if (two) {
        size_t n = std::max(left.size(), right.size());
        for (size_t i = 0; i < n; i++) {
            std::string l = i < left.size() ? left[i] : "";
            std::string r = i < right.size() ? right[i] : "";
            frame.push_back(pad_to(l, lw) + " │ " + pad_to(r, rw));
        }
    } else {
        for (auto &l : left) frame.push_back(pad_to(l, width));
        for (auto &r : right) frame.push_back(pad_to(r, width));
    }

    if ((int)frame.size() > rows) frame.resize((size_t)rows);
    return frame;
}

static void terminal_size(int *cols, int *rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
    } else {
        *cols = 80;
        *rows = 24;
    }
}

int main(int argc, char **argv) {
    bool once = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--once") once = true;
        else if (a == "--version") {
            std::printf("vlswmstat %s\n", VLSWM_VERSION);
            return 0;
        } else if (a == "--help") {
            std::printf("usage: vlswmstat [--once] [--version] [--help]\n");
            return 0;
        }
    }

    CpuSample prev_cpu = read_cpu_total();
    std::string prev_iface;
    std::string ip;
    prev_iface = net_iface(&ip);
    uint64_t prev_rx = prev_iface.empty() ? 0 : net_bytes(prev_iface, "rx_bytes");
    uint64_t prev_tx = prev_iface.empty() ? 0 : net_bytes(prev_iface, "tx_bytes");
    double prev_time = (double)time(nullptr);

    if (once || !isatty(STDOUT_FILENO)) {
        struct timespec ts = {0, 120 * 1000000};
        nanosleep(&ts, nullptr);
        int cols, rows;
        terminal_size(&cols, &rows);
        double now = (double)time(nullptr);
        Snapshot s = snapshot(prev_cpu, now - prev_time, prev_iface,
                              prev_rx, prev_tx, prev_time, now, false);
        for (auto &line : build_frame(s, cols, rows)) {
            std::printf("%s\n", line.c_str());
        }
        return 0;
    }

    struct termios orig;
    if (tcgetattr(STDIN_FILENO, &orig) != 0) return 1;
    struct termios raw = orig;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    signal(SIGWINCH, on_winch);
    signal(SIGINT, on_quit);
    signal(SIGTERM, on_quit);
    std::printf("\x1b[?1049h\x1b[?25l");

    double last_time = (double)time(nullptr);
    std::string last_iface = prev_iface;
    uint64_t last_rx = prev_rx;
    uint64_t last_tx = prev_tx;
    bool running = true;

    while (running && !g_quit) {
        int cols, rows;
        terminal_size(&cols, &rows);
        double now = (double)time(nullptr);
        CpuSample cur_cpu = read_cpu_total();
        Snapshot s = snapshot(prev_cpu, now - last_time, last_iface,
                              last_rx, last_tx, last_time, now, true);

        std::printf("\x1b[H");
        for (auto &line : build_frame(s, cols, rows)) {
            std::printf("%s\x1b[K\n", line.c_str());
        }
        std::printf("\x1b[J");
        std::fflush(stdout);

        prev_cpu = cur_cpu;
        last_time = now;
        last_iface = s.iface;
        if (!s.iface.empty()) {
            last_rx = net_bytes(s.iface, "rx_bytes");
            last_tx = net_bytes(s.iface, "tx_bytes");
        }

        struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
        int r = poll(&pfd, 1, 1000);
        if (r > 0) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                if (c == 'q' || c == 'Q' || c == 3) running = false;
                else if (c == 27) {
                    struct pollfd esc = {STDIN_FILENO, POLLIN, 0};
                    if (poll(&esc, 1, 20) == 0) running = false;
                }
            }
        }
        if (g_resize) {
            g_resize = 0;
            std::printf("\x1b[2J");
        }
    }

    std::printf("\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    return 0;
}
