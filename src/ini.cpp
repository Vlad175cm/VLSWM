
#include "vlswm/ini.h"

#include <cstdio>
#include <cstring>
#include <string>

static void trim(std::string &s) {
    const char *ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    s.erase(s.find_last_not_of(ws) + 1);
}

std::optional<IniConfig> IniConfig::load(const std::string &path) {
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return std::nullopt;

    IniConfig cfg;
    cfg.path_ = path;

    std::string section;
    char line[4096];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        trim(s);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;

        if (s[0] == '[') {
            size_t end = s.find(']');
            if (end != std::string::npos) {
                section = s.substr(1, end - 1);
                trim(section);
            }
            continue;
        }

        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;

        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        trim(key);
        trim(val);


        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\''))) {
            val = val.substr(1, val.size() - 2);
        }

        std::string full = section.empty() ? key : section + "." + key;
        cfg.values[full] = val;
    }

    std::fclose(f);
    return cfg;
}

std::string IniConfig::get(const std::string &section, const std::string &key,
                           const std::string &def) const {
    auto it = values.find(section + "." + key);
    if (it == values.end()) return def;
    return it->second;
}

int IniConfig::get_int(const std::string &section, const std::string &key, int def) const {
    std::string v = get(section, key);
    if (v.empty()) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}

bool IniConfig::get_bool(const std::string &section, const std::string &key, bool def) const {
    std::string v = get(section, key);
    if (v.empty()) return def;
    if (v == "true" || v == "yes" || v == "1" || v == "on") return true;
    if (v == "false" || v == "no" || v == "0" || v == "off") return false;
    return def;
}
