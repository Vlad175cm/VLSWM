#!/bin/bash
# vlswm installation script
# Arch Linux / Arch-based only.
# Installs dependencies (build + runtime), builds vlswm, then installs the binary, config and SDDM session.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/usr/bin"
SDDM_DIR="/usr/share/wayland-sessions"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

require_arch() {
  if [ ! -r /etc/os-release ]; then
    echo -e "${RED}Error: vlswm supports Arch Linux and Arch-based distributions only.${NC}" >&2
    exit 1
  fi
  # shellcheck disable=SC1091
  . /etc/os-release
  case "${ID:-} ${ID_LIKE:-}" in
  *arch*) ;;
  *)
    echo -e "${RED}Error: vlswm supports Arch Linux and Arch-based distributions only.${NC}" >&2
    echo -e "${RED}Detected: ${PRETTY_NAME:-unknown}${NC}" >&2
    exit 1
    ;;
  esac
}

install_dependencies() {
  pacman -S --needed --noconfirm wayland wlroots0.20 wayland-protocols \
    libxkbcommon pixman libinput cairo pango wlr-protocols \
    pkgconf cmake make gcc base-devel git \
    foot fish xwayland-satellite \
    grim slurp wl-clipboard wf-recorder cliphist \
    brightnessctl wireplumber sddm ttf-jetbrains-mono-nerd mesa \
    qt5-declarative xdg-desktop-portal xdg-desktop-portal-gtk \
    yazi neovim btop cava tree-sitter-cli

  install_aur_helper
}

install_aur_package() {
  local pkg="$1" dir
  if ! command -v git >/dev/null 2>&1 || ! command -v makepkg >/dev/null 2>&1; then
    echo -e "  ${YELLOW}⚠${NC}  git and makepkg are required to build $pkg"
    return 1
  fi
  dir="$(mktemp -d)"
  chown "$REAL_USER" "$dir" 2>/dev/null || true
  if ! sudo -u "$REAL_USER" git clone --depth 1 \
      "https://aur.archlinux.org/$pkg.git" "$dir/$pkg"; then
    rm -rf "$dir"
    return 1
  fi
  if ! sudo -u "$REAL_USER" bash -c "cd '$dir/$pkg' && makepkg -s --noconfirm"; then
    rm -rf "$dir"
    return 1
  fi
  if ! pacman -U --noconfirm "$dir/$pkg"/*.pkg.tar.*; then
    rm -rf "$dir"
    return 1
  fi
  rm -rf "$dir"
  return 0
}

install_aur_helper() {
  if command -v zen-browser >/dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} zen-browser is already installed"
    return 0
  fi

  local have_paru="" have_yay="" choice="" helper="" pkg="" def=1
  command -v paru >/dev/null 2>&1 && have_paru=1
  command -v yay >/dev/null 2>&1 && have_yay=1
  [ -n "$have_yay" ] && [ -z "$have_paru" ] && def=2

  echo -e "${YELLOW}My setup installs zen-browser-bin from the AUR: 1) paru-bin  2) yay-bin  3) skip${NC}"
  read -r -p "  Choice [$def]: " choice || choice="$def"
  choice="${choice:-$def}"
  case "$choice" in
  1) helper=paru; pkg=paru-bin ;;
  2) helper=yay; pkg=yay-bin ;;
  3) echo -e "  skipped"; return 0 ;;
  *)
    echo -e "  ${YELLOW}⚠${NC}  unknown choice, skipped"
    return 0
    ;;
  esac

  if ! command -v "$helper" >/dev/null 2>&1; then
    echo -e "  Installing $pkg..."
    if [ -n "$have_paru" ] && [ "$helper" = "yay" ]; then
      sudo -u "$REAL_USER" paru -S --needed --noconfirm "$pkg" || true
    elif [ -n "$have_yay" ] && [ "$helper" = "paru" ]; then
      sudo -u "$REAL_USER" yay -S --needed --noconfirm "$pkg" || true
    else
      install_aur_package "$pkg" || true
    fi
  fi

  if ! command -v "$helper" >/dev/null 2>&1; then
    if [ -n "$have_paru" ]; then
      helper=paru
    elif [ -n "$have_yay" ]; then
      helper=yay
    fi
  fi

  if ! command -v "$helper" >/dev/null 2>&1; then
    echo -e "  ${YELLOW}⚠${NC}  $helper is not available; skipping zen-browser-bin"
    return 0
  fi

  echo -e "  Installing zen-browser-bin via $helper..."
  if sudo -u "$REAL_USER" "$helper" -S --needed --noconfirm zen-browser-bin; then
    echo -e "  ${GREEN}✓${NC} Installed: zen-browser-bin"
  else
    echo -e "  ${YELLOW}⚠${NC}  zen-browser-bin was not installed; run later: $helper -S zen-browser-bin"
  fi
}

check_runtime_tools() {
  local missing=""
  for tool in foot fish xwayland-satellite grim slurp wl-copy wf-recorder cliphist yazi nvim btop cava; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing="$missing $tool"
    fi
  done
  if [ -n "$missing" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Not found in PATH:$missing"
    echo -e "  ${YELLOW}⚠${NC}  vlswm still installs, but these programs must be present to work fully."
  fi
}

install_dots() {
  local src="$SCRIPT_DIR/dots"
  [ -d "$src" ] || return 0
  echo -e "${YELLOW}Install my Neovim, btop and cava configs from dots/? [Y/n]${NC}"
  local ans=""
  read -r ans
  case "$ans" in
  [nN]*) echo -e "  skipped"; return 0 ;;
  esac

  local app dst backup
  for app in nvim btop cava; do
    [ -d "$src/$app" ] || continue
    dst="$REAL_HOME/.config/$app"
    if [ -e "$dst" ]; then
      backup="$dst.backup.$(date +%Y-%m-%d-%H%M%S)"
      mv "$dst" "$backup"
      echo -e "  ${YELLOW}⚠${NC}  Existing $dst moved to $backup"
    fi
    cp -a "$src/$app" "$dst"
    chown -R "$REAL_USER:$REAL_USER" "$dst"
    echo -e "  ${GREEN}✓${NC} Installed: $dst"
  done

  install -Dm755 "$src/vlswm-theme-watch" "$REAL_HOME/.local/bin/vlswm-theme-watch"
  install -Dm644 "$src/vlswm-theme-watch.service" \
    "$REAL_HOME/.config/systemd/user/vlswm-theme-watch.service"
  chown "$REAL_USER:$REAL_USER" "$REAL_HOME/.local/bin/vlswm-theme-watch" \
    "$REAL_HOME/.config/systemd/user/vlswm-theme-watch.service"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-theme-watch"

  local uid
  uid="$(id -u "$REAL_USER")"
  if sudo -u "$REAL_USER" env XDG_RUNTIME_DIR="/run/user/$uid" \
      systemctl --user enable --now vlswm-theme-watch.service >/dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} Enabled: vlswm-theme-watch.service"
  else
    echo -e "  ${YELLOW}⚠${NC}  Enable it after login: systemctl --user enable --now vlswm-theme-watch.service"
  fi
}

install_sddm_theme() {
  local repo="https://github.com/Vlad175cm/vlswm-sddm-theme.git"
  local dir="$SCRIPT_DIR/vlswm-sddm-theme"
  local ans=""

  echo -e "${YELLOW}Install my VLSWM SDDM theme? [Y/n]${NC}"
  read -r ans || ans=""
  case "$ans" in
  [nN]*) echo -e "  skipped"; return 0 ;;
  esac

  if [ -d "$dir/.git" ]; then
    echo -e "  Updating $dir..."
    if ! git -c safe.directory="$dir" -C "$dir" pull --ff-only; then
      echo -e "  ${YELLOW}⚠${NC}  could not update the theme, using the existing clone"
    fi
  elif [ -d "$dir" ]; then
    if [ ! -f "$dir/install.sh" ]; then
      echo -e "  ${YELLOW}⚠${NC}  $dir exists but is not a theme clone; skipped"
      return 0
    fi
    echo -e "  ${YELLOW}⚠${NC}  $dir is not a git clone, using it as is"
  else
    echo -e "  Cloning $repo..."
    if ! git clone --depth 1 "$repo" "$dir"; then
      echo -e "  ${YELLOW}⚠${NC}  could not clone the theme; skipped"
      return 0
    fi
    chown -R "$REAL_USER" "$dir" 2>/dev/null || true
  fi

  if [ ! -f "$dir/install.sh" ]; then
    echo -e "  ${YELLOW}⚠${NC}  $dir/install.sh not found; skipped"
    return 0
  fi
  if ! bash "$dir/install.sh"; then
    echo -e "  ${YELLOW}⚠${NC}  SDDM theme installation failed; VLSWM is still installed"
  fi
}

main() {
  echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
  echo -e "${GREEN}║     vlswm Installation Script          ║${NC}"
  echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
  echo ""

  if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}This script requires root privileges to install to /usr/bin and /usr/share${NC}"
    echo -e "${YELLOW}Re-running with sudo...${NC}"
    exec sudo bash "$0" "$@"
  fi

  REAL_USER="${SUDO_USER:-$USER}"
  REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)
  CONFIG_DIR="$REAL_HOME/.config/vlswm"
  FOOT_DIR="$REAL_HOME/.config/foot"

  echo -e "${GREEN}[1/8]${NC} Installing dependencies..."
  require_arch
  install_dependencies

  if [ ! -f "$SCRIPT_DIR/config/config" ]; then
    echo -e "${RED}Error: Config not found in $SCRIPT_DIR/config/${NC}"
    exit 1
  fi
  if [ ! -f "$SCRIPT_DIR/vlswm.desktop" ]; then
    echo -e "${RED}Error: vlswm.desktop not found in $SCRIPT_DIR/${NC}"
    exit 1
  fi

  echo -e "${GREEN}[2/8]${NC} Building vlswm..."
  if ! command -v cmake >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo -e "${RED}Error: cmake and make are required to build.${NC}"
    exit 1
  fi
  if ! command -v pkg-config >/dev/null 2>&1; then
    echo -e "${RED}Error: pkg-config is required to build (package 'pkgconf' on Arch).${NC}"
    exit 1
  fi

  rm -rf "$SCRIPT_DIR/build"
  BUILD_CMD="cd '$SCRIPT_DIR' && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
  BUILD_LOG="$SCRIPT_DIR/build.log"
  if ! sudo -u "$REAL_USER" bash -c "$BUILD_CMD" >"$BUILD_LOG" 2>&1; then
    tail -n 25 "$BUILD_LOG" >&2
    echo -e "${RED}Error: build failed. Full log: $BUILD_LOG${NC}"
    exit 1
  fi

  echo -e "${GREEN}[3/8]${NC} Installing binaries to $INSTALL_DIR..."
  install -m 755 -s "$SCRIPT_DIR/build/vlswm" "$INSTALL_DIR/vlswm"
  install -m 755 -s "$SCRIPT_DIR/build/vlswmstat" "$INSTALL_DIR/vlswmstat"
  install -m 755 "$SCRIPT_DIR/vlswm-update" "$INSTALL_DIR/vlswm-update"
  install -m 755 "$SCRIPT_DIR/scripts/vlswm-config-merge" "$INSTALL_DIR/vlswm-config-merge"
  sudo -u "$REAL_USER" install -Dm755 "$SCRIPT_DIR/scripts/vlswm-record" \
    "$REAL_HOME/.local/bin/vlswm-record"
  sudo -u "$REAL_USER" install -Dm755 "$SCRIPT_DIR/scripts/vlswm-screenshot" \
    "$REAL_HOME/.local/bin/vlswm-screenshot"
  sudo -u "$REAL_USER" install -Dm755 "$SCRIPT_DIR/scripts/vlswm-mic" \
    "$REAL_HOME/.local/bin/vlswm-mic"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswm"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswmstat"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswm-update"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswm-config-merge"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-record"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-screenshot"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-mic"

  echo -e "${GREEN}[4/8]${NC} Installing configuration to $CONFIG_DIR..."
  sudo -u "$REAL_USER" mkdir -p "$CONFIG_DIR"
  if [ -f "$CONFIG_DIR/config" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Config already exists, saving backup: $CONFIG_DIR/config.backup"
    cp "$CONFIG_DIR/config" "$CONFIG_DIR/config.backup"
    chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config.backup"
  fi
  cp "$SCRIPT_DIR/config/config" "$CONFIG_DIR/config"
  chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config"
  echo -e "  ${GREEN}✓${NC} Installed: $CONFIG_DIR/config"

  echo -e "${GREEN}[5/8]${NC} Installing foot and file chooser configuration..."
  if [ ! -d "$SCRIPT_DIR/config/foot" ]; then
    echo -e "${RED}Error: foot config not found in $SCRIPT_DIR/config/foot${NC}"
    exit 1
  fi
  sudo -u "$REAL_USER" mkdir -p "$FOOT_DIR"
  if [ -f "$FOOT_DIR/foot.ini" ]; then
    cp "$FOOT_DIR/foot.ini" "$FOOT_DIR/foot.ini.backup"
    chown "$REAL_USER:$REAL_USER" "$FOOT_DIR/foot.ini.backup"
  fi
  cp "$SCRIPT_DIR/config/foot/foot.ini" "$FOOT_DIR/foot.ini"
  cp "$SCRIPT_DIR/config/foot/vlswm-theme.ini" "$FOOT_DIR/vlswm-theme.ini"
  chown "$REAL_USER:$REAL_USER" "$FOOT_DIR/foot.ini" "$FOOT_DIR/vlswm-theme.ini"
  echo -e "  ${GREEN}✓${NC} Installed: $FOOT_DIR/foot.ini"
  echo -e "  ${GREEN}✓${NC} The theme file vlswm-theme.ini is generated by vlswm on start."

  sudo -u "$REAL_USER" "$SCRIPT_DIR/scripts/vlswm-env-install" "$SCRIPT_DIR" "$REAL_HOME"

  if [ ! -x /usr/lib/xdg-desktop-portal-termfilechooser ]; then
    echo -e "  ${YELLOW}⚠${NC}  Install xdg-desktop-portal-termfilechooser (AUR) to use yazi in file pickers; without it the GTK dialog is used."
  fi

  local uid
  uid="$(id -u "$REAL_USER")"
  if sudo -u "$REAL_USER" env XDG_RUNTIME_DIR="/run/user/$uid" \
      systemctl --user restart xdg-desktop-portal-termfilechooser.service xdg-desktop-portal.service >/dev/null 2>&1; then
    echo -e "  ${GREEN}✓${NC} Restarted the portal services"
  else
    echo -e "  ${YELLOW}⚠${NC}  Restart the portal after login: systemctl --user restart xdg-desktop-portal-termfilechooser xdg-desktop-portal"
  fi

  echo -e "${GREEN}[6/8]${NC} Installing dotfiles (nvim, btop, cava)..."
  install_dots

  echo -e "${GREEN}[7/8]${NC} Creating SDDM session in $SDDM_DIR..."
  mkdir -p "$SDDM_DIR"
  cp "$SCRIPT_DIR/vlswm.desktop" "$SDDM_DIR/vlswm.desktop"
  echo -e "  ${GREEN}✓${NC} Created: $SDDM_DIR/vlswm.desktop"

  install_sddm_theme

  echo -e "${GREEN}[8/8]${NC} Verifying installation..."
  if command -v vlswm &>/dev/null; then
    echo -e "  ${GREEN}✓${NC} vlswm is available in PATH: $(vlswm --version 2>&1 || echo vlswm)"
  else
    echo -e "  ${RED}✗${NC} Error: vlswm not found in PATH"
    exit 1
  fi
  check_runtime_tools
  if ! fc-list 2>/dev/null | grep -qi "JetBrainsMono Nerd"; then
    echo -e "  ${YELLOW}⚠${NC}  Font 'JetBrainsMono Nerd Font' not found; install it or change [launcher] font."
  fi

  echo ""
  echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
  echo -e "${GREEN}║   Installation completed successfully! ║${NC}"
  echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
  echo ""
  echo -e "${YELLOW}What's next:${NC}"
  echo -e "  1. Log out of your current session"
  echo -e "  2. Select 'vlswm' in the SDDM session menu"
  echo -e "  3. Log in with your password"
  echo ""
  echo -e "${YELLOW}Key bindings (defaults):${NC}"
  echo -e "  SUPER+D         - Launcher (type a command, Enter runs it in fish)"
  echo -e "  SUPER+Q         - Close the focused window (the terminal cannot be closed)"
  echo -e "  SUPER+LEFT/RIGHT- Switch windows"
  echo -e "  SUPER+T         - Toggle black / white theme (also recolors foot)"
  echo -e "  SUPER+R         - Reload config"
  echo -e "  SUPER+ESC       - Exit WM"
  echo ""
  echo -e "${YELLOW}Configuration:${NC}"
  echo -e "  Config file: $CONFIG_DIR/config"
  echo -e "  After editing, press SUPER+R to apply"
  echo ""
  echo -e "${YELLOW}Updates:${NC}"
  echo -e "  sudo vlswm-update   # installs the latest GitHub release"
  echo ""
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
