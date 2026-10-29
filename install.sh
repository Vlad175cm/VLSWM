#!/bin/bash
# vlswm installation script
# Installs dependencies (build + runtime), builds vlswm, then installs the binary, config and SDDM session.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/usr/bin"
SDDM_DIR="/usr/share/wayland-sessions"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

map_distro_choice() {
  case "$1" in
  1 | arch | Arch) echo "arch" ;;
  2 | fedora | Fedora) echo "fedora" ;;
  3 | debian | Debian | ubuntu | Ubuntu) echo "debian" ;;
  4 | opensuse | openSUSE | suse | SUSE) echo "opensuse" ;;
  *) echo "" ;;
  esac
}

install_dependencies() {
  local distro="$1"
  case "$distro" in
  arch)
    pacman -S --needed --noconfirm wayland wlroots0.20 wayland-protocols \
      libxkbcommon pixman libinput cairo pango wlr-protocols \
      pkgconf cmake make gcc \
      foot fish xwayland-satellite \
      grim slurp wl-clipboard wf-recorder \
      brightnessctl wireplumber sddm ttf-jetbrains-mono-nerd mesa
    ;;
  fedora)
    dnf install -y wayland-devel wlroots-devel xkbcommon-devel \
      pixman-devel libinput-devel cairo-devel pango-devel wlr-protocols-devel \
      pkgconf-pkg-config cmake gcc-c++ make \
      fish foot brightnessctl pipewire wireplumber sddm jetbrains-mono-fonts \
      mesa-dri-drivers mesa-libGL grim slurp wl-clipboard wf-recorder
    warn_manual_install
    ;;
  debian)
    apt-get update
    apt-get install -y libwlroots-dev libwayland-dev wayland-protocols \
      libxkbcommon-dev libpixman-1-dev libinput-dev \
      libcairo2-dev libpango1.0-dev \
      pkg-config cmake g++ make \
      fish foot brightnessctl pipewire wireplumber sddm fonts-jetbrains-mono \
      libgl1-mesa-dri grim slurp wl-clipboard wf-recorder
    warn_manual_install
    ;;
  opensuse)
    zypper install -y wlroots-devel wayland-devel libxkbcommon-devel \
      pixman-devel libinput-devel cairo-devel pango-devel \
      wlr-protocols-devel pkg-config cmake gcc-c++ make \
      fish foot brightnessctl pipewire wireplumber sddm \
      jetbrains-mono-fonts Mesa Mesa-libGL1 grim slurp wl-clipboard wf-recorder
    warn_manual_install
    ;;
  *)
    echo -e "${RED}Error: unknown distro '$distro'${NC}" >&2
    return 1
    ;;
  esac
}

warn_manual_install() {
  echo -e "${YELLOW}xwayland-satellite is not in the official repositories of this distro.${NC}"
  echo -e "${YELLOW}Install it manually; vlswm uses it at runtime for X11 apps.${NC}"
}

ask_distro() {
  echo -e "${YELLOW}Which distro are you using?${NC}" >&2
  echo "  1) Arch" >&2
  echo "  2) Fedora" >&2
  echo "  3) Debian / Ubuntu" >&2
  echo "  4) openSUSE" >&2
  local choice="" distro=""
  while [ -z "$distro" ]; do
    read -rp "Enter the number or name: " choice
    distro="$(map_distro_choice "$choice")"
    if [ -z "$distro" ]; then
      echo -e "${RED}Invalid selection, try again.${NC}" >&2
    fi
  done
  echo "$distro"
}

check_runtime_tools() {
  local missing=""
  for tool in foot fish xwayland-satellite grim slurp wl-copy wf-recorder; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing="$missing $tool"
    fi
  done
  if [ -n "$missing" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Not found in PATH:$missing"
    echo -e "  ${YELLOW}⚠${NC}  vlswm still installs, but these programs must be present to work fully."
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
  ENV_DIR="$REAL_HOME/.config/environment.d"

  echo -e "${GREEN}[1/7]${NC} Installing dependencies..."
  DISTRO="$(ask_distro)"
  echo -e "  Installing dependencies for ${GREEN}$DISTRO${NC}..."
  install_dependencies "$DISTRO"

  if [ ! -f "$SCRIPT_DIR/config/config" ]; then
    echo -e "${RED}Error: Config not found in $SCRIPT_DIR/config/${NC}"
    exit 1
  fi
  if [ ! -f "$SCRIPT_DIR/vlswm.desktop" ]; then
    echo -e "${RED}Error: vlswm.desktop not found in $SCRIPT_DIR/${NC}"
    exit 1
  fi

  echo -e "${GREEN}[2/7]${NC} Building vlswm..."
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
    if [ -d "$REAL_HOME/.local/vlswm-sysroot/usr/lib/pkgconfig" ]; then
      echo -e "${YELLOW}First pass failed — trying with vendored sysroot...${NC}"
      sudo -u "$REAL_USER" env \
        PKG_CONFIG_PATH="$REAL_HOME/.local/vlswm-sysroot/usr/lib/pkgconfig" \
        bash -c "$BUILD_CMD" >"$BUILD_LOG" 2>&1 || {
        tail -n 25 "$BUILD_LOG" >&2
        echo -e "${RED}Error: build failed. Full log: $BUILD_LOG${NC}"
        exit 1
      }
    else
      tail -n 25 "$BUILD_LOG" >&2
      echo -e "${RED}Error: build failed. Full log: $BUILD_LOG${NC}"
      exit 1
    fi
  fi

  echo -e "${GREEN}[3/7]${NC} Installing binaries to $INSTALL_DIR..."
  install -m 755 -s "$SCRIPT_DIR/build/vlswm" "$INSTALL_DIR/vlswm"
  install -m 755 -s "$SCRIPT_DIR/build/vlswmstat" "$INSTALL_DIR/vlswmstat"
  sudo -u "$REAL_USER" install -Dm755 "$SCRIPT_DIR/scripts/vlswm-record" \
    "$REAL_HOME/.local/bin/vlswm-record"
  sudo -u "$REAL_USER" install -Dm755 "$SCRIPT_DIR/scripts/vlswm-screenshot" \
    "$REAL_HOME/.local/bin/vlswm-screenshot"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswm"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswmstat"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-record"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.local/bin/vlswm-screenshot"

  echo -e "${GREEN}[4/7]${NC} Installing configuration to $CONFIG_DIR..."
  sudo -u "$REAL_USER" mkdir -p "$CONFIG_DIR"
  if [ -f "$CONFIG_DIR/config" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Config already exists, saving backup: $CONFIG_DIR/config.backup"
    cp "$CONFIG_DIR/config" "$CONFIG_DIR/config.backup"
    chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config.backup"
  fi
  cp "$SCRIPT_DIR/config/config" "$CONFIG_DIR/config"
  chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config"
  echo -e "  ${GREEN}✓${NC} Installed: $CONFIG_DIR/config"

  echo -e "${GREEN}[5/7]${NC} Installing foot configuration..."
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

  sudo -u "$REAL_USER" mkdir -p "$ENV_DIR"
  if [ -f "$ENV_DIR/10-vlswm.conf" ]; then
    cp "$ENV_DIR/10-vlswm.conf" "$ENV_DIR/10-vlswm.conf.backup"
    chown "$REAL_USER:$REAL_USER" "$ENV_DIR/10-vlswm.conf.backup"
  fi
  cp "$SCRIPT_DIR/config/environment.d/10-vlswm.conf" "$ENV_DIR/10-vlswm.conf"
  chown "$REAL_USER:$REAL_USER" "$ENV_DIR/10-vlswm.conf"
  echo -e "  ${GREEN}✓${NC} Installed: $ENV_DIR/10-vlswm.conf"

  FISH_CONF_DIR="$REAL_HOME/.config/fish/conf.d"
  sudo -u "$REAL_USER" mkdir -p "$FISH_CONF_DIR"
  if [ -f "$FISH_CONF_DIR/vlswmstat.fish" ]; then
    cp "$FISH_CONF_DIR/vlswmstat.fish" "$FISH_CONF_DIR/vlswmstat.fish.backup"
    chown "$REAL_USER:$REAL_USER" "$FISH_CONF_DIR/vlswmstat.fish.backup"
  fi
  cat > "$FISH_CONF_DIR/vlswmstat.fish" <<'FISHEOF'
if status is-interactive; and test -t 1; and set -q VLSWM_BASE_TERM
    set -e VLSWM_BASE_TERM
    if command -q vlswmstat
        vlswmstat
    else if test -x $HOME/.local/bin/vlswmstat
        $HOME/.local/bin/vlswmstat
    end
end
FISHEOF
  chown "$REAL_USER:$REAL_USER" "$FISH_CONF_DIR/vlswmstat.fish"
  echo -e "  ${GREEN}✓${NC} Installed: $FISH_CONF_DIR/vlswmstat.fish"

  echo -e "${GREEN}[6/7]${NC} Creating SDDM session in $SDDM_DIR..."
  mkdir -p "$SDDM_DIR"
  cp "$SCRIPT_DIR/vlswm.desktop" "$SDDM_DIR/vlswm.desktop"
  echo -e "  ${GREEN}✓${NC} Created: $SDDM_DIR/vlswm.desktop"

  echo -e "${GREEN}[7/7]${NC} Verifying installation..."
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
}

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  main "$@"
fi
