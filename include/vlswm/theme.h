#pragma once

#include <map>
#include <string>

inline constexpr const char *VLSWM_FOOT_THEME_FILE = "vlswm-theme.ini";

std::map<std::string, std::string> foot_theme_read_base(const std::string &foot_dir);
bool foot_theme_write(const std::string &foot_dir,
                      const std::map<std::string, std::string> &base);
bool foot_theme_ensure_include(const std::string &foot_dir);
