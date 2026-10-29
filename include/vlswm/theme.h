#pragma once

#include <map>
#include <string>

inline constexpr const char *VLSWM_THEME_FILE = "vlswm-theme.ghostty";
inline constexpr const char *VLSWM_FOOT_THEME_FILE = "vlswm-theme.ini";

std::map<std::string, std::string> theme_read_base(const std::string &ghostty_dir);
bool theme_write(const std::string &ghostty_dir,
                 const std::map<std::string, std::string> &base, bool white);
bool theme_ensure_include(const std::string &ghostty_dir);
std::string theme_invert(const std::string &hex);

std::map<std::string, std::string> foot_theme_read_base(const std::string &foot_dir);
bool foot_theme_write(const std::string &foot_dir,
                      const std::map<std::string, std::string> &base);
bool foot_theme_ensure_include(const std::string &foot_dir);
