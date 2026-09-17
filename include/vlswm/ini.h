// vlswm — trivial INI config loader.
#pragma once

#include <map>
#include <optional>
#include <string>

struct IniConfig {
    std::map<std::string, std::string> values; // "section.key" -> value

    static std::optional<IniConfig> load(const std::string &path);

    std::string get(const std::string &section, const std::string &key,
                    const std::string &def = "") const;
    int get_int(const std::string &section, const std::string &key, int def) const;
    bool get_bool(const std::string &section, const std::string &key, bool def) const;

    const std::string &path() const { return path_; }

  private:
    std::string path_;
};
