#!/bin/bash
# vlswm installation script
# Installs dependencies (build + runtime), then the vlswm binary, config and SDDM session.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/usr/bin"
SDDM_DIR="/usr/share/wayland-sessions"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Maps user input to a distro id. Returns empty string for invalid input.
map_distro_choice() {
  case "$1" in
  1 | arch | Arch) echo "arch" ;;
  2 | fedora | Fedora) echo "fedora" ;;
  3 | debian | Debian | ubuntu | Ubuntu) echo "debian" ;;
  4 | opensuse | openSUSE | suse | SUSE) echo "opensuse" ;;
  *) echo "" ;;
  esac
}

# Installs build and runtime dependencies for the given distro id.
# Runs as root. Returns non-zero if package installation fails.
install_dependencies() {
  local distro="$1"
  case "$distro" in
  arch)
    pacman -S --needed --noconfirm wayland wlroots0.20 wayland-protocols \
      libxkbcommon pixman libdrm libinput cairo pango gdk-pixbuf2 \
      libxcb xcb-util-wm \
      pkgconf cmake make gcc \
      kitty fuzzel brightnessctl wireplumber xorg-xwayland sddm
    ;;
  fedora)
    dnf install -y wayland-devel wlroots-devel xkbcommon-devel \
      pixman-devel libdrm-devel libinput-devel cairo-devel \
      pango-devel gdk-pixbuf2-devel libxcb-devel xcb-util-wm-devel \
      pkgconf-pkg-config cmake gcc-c++ make \
      kitty fuzzel brightnessctl pipewire wireplumber \
      xorg-x11-server-Xwayland sddm
    ;;
  debian)
    apt-get update
    apt-get install -y libwlroots-dev libwayland-dev wayland-protocols \
      libxkbcommon-dev libpixman-1-dev libdrm-dev libinput-dev \
      libcairo2-dev libpango1.0-dev libgdk-pixbuf-2.0-dev libxcb-dev \
      libxcb-ewmh-dev libxcb-icccm4-dev \
      pkg-config cmake g++ make \
      kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
    ;;
  opensuse)
    zypper install -y wlroots-devel wayland-devel libxkbcommon-devel \
      pixman-devel libdrm-devel libinput-devel cairo-devel \
      pango-devel gdk-pixbuf-devel libxcb-devel xcb-util-wm-devel \
      pkg-config cmake gcc-c++ make \
      kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
    ;;
  *)
    echo -e "${RED}Error: unknown distro '$distro'${NC}" >&2
    return 1
    ;;
  esac
}

# Asks the user which distro they use and returns its id on stdout.
# All interactive output goes to stderr so it is not captured by $( ... ).
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

main() {
  echo -e "${GREEN}╔════════════════════════════════════════╗${NC}"
  echo -e "${GREEN}║     vlswm Installation Script          ║${NC}"
  echo -e "${GREEN}╚════════════════════════════════════════╝${NC}"
  echo ""

  # Check if running with sudo/root
  if [ "$EUID" -ne 0 ]; then
    echo -e "${YELLOW}This script requires root privileges to install to /usr/bin and /usr/share${NC}"
    echo -e "${YELLOW}Re-running with sudo...${NC}"
    exec sudo bash "$0" "$@"
  fi

  # Save original user (in case script was called with sudo)
  REAL_USER="${SUDO_USER:-$USER}"
  REAL_HOME=$(getent passwd "$REAL_USER" | cut -d: -f6)
  CONFIG_DIR="$REAL_HOME/.config/vlswm"

  echo -e "${GREEN}[1/6]${NC} Checking dependencies..."

  # Install build + runtime dependencies for the chosen distro
  DISTRO="$(ask_distro)"
  echo -e "  Installing dependencies for ${GREEN}$DISTRO${NC}..."
  install_dependencies "$DISTRO"

  # Check if config exists
  if [ ! -f "$SCRIPT_DIR/config/config" ]; then
    echo -e "${RED}Error: Config not found in $SCRIPT_DIR/config/${NC}"
    exit 1
  fi

  # Check if fuzzel config exists
  if [ ! -f "$SCRIPT_DIR/config/fuzzel/fuzzel.ini" ]; then
    echo -e "${RED}Error: fuzzel.ini not found in $SCRIPT_DIR/config/fuzzel/${NC}"
    exit 1
  fi

  # Check if vlswm.desktop template exists
  if [ ! -f "$SCRIPT_DIR/vlswm.desktop" ]; then
    echo -e "${RED}Error: vlswm.desktop not found in $SCRIPT_DIR/${NC}"
    exit 1
  fi

  # Build the binary if it isn't already present (as the real user, not root).
  if [ ! -f "$SCRIPT_DIR/build/vlswm" ]; then
    echo -e "${YELLOW}Binary not found — building project (cmake + make)...${NC}"
    if ! command -v cmake >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
      echo -e "${RED}Error: cmake and make are required to build. Install them and run the script again.${NC}"
      exit 1
    fi
    if ! command -v pkg-config >/dev/null 2>&1; then
      echo -e "${RED}Error: pkg-config is required to build (package 'pkgconf' on Arch).${NC}"
      exit 1
    fi

    # Remove any stale/partial build directory (root-owned leftovers,
    # cache copied from another machine, half-finished configure).
    # cmake regenerates the Makefile here.
    rm -rf "$SCRIPT_DIR/build"

    BUILD_CMD="cd '$SCRIPT_DIR' && cmake -B build && cmake --build build"
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
  fi

  echo -e "${GREEN}[2/6]${NC} Installing vlswm binary to $INSTALL_DIR..."
  cp "$SCRIPT_DIR/build/vlswm" "$INSTALL_DIR/vlswm"
  chmod +x "$INSTALL_DIR/vlswm"
  echo -e "  ${GREEN}✓${NC} Installed: $INSTALL_DIR/vlswm"

  echo -e "${GREEN}[3/6]${NC} Installing configuration to $CONFIG_DIR..."
  sudo -u "$REAL_USER" mkdir -p "$CONFIG_DIR"

  # Copy config only if it doesn't exist (don't overwrite existing config)
  if [ -f "$CONFIG_DIR/config" ]; then
    echo -e "  ${YELLOW}⚠${NC}  Config already exists: $CONFIG_DIR/config"
    echo -e "  ${YELLOW}⚠${NC}  Saving backup: $CONFIG_DIR/config.backup"
    cp "$CONFIG_DIR/config" "$CONFIG_DIR/config.backup"
    chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config.backup"
  fi

  cp "$SCRIPT_DIR/config/config" "$CONFIG_DIR/config"
  chown "$REAL_USER:$REAL_USER" "$CONFIG_DIR/config"
  echo -e "  ${GREEN}✓${NC} Installed: $CONFIG_DIR/config"

  echo -e "${GREEN}[4/6]${NC} Creating SDDM session in $SDDM_DIR..."
  mkdir -p "$SDDM_DIR"
  cp "$SCRIPT_DIR/vlswm.desktop" "$SDDM_DIR/vlswm.desktop"
  echo -e "  ${GREEN}✓${NC} Created: $SDDM_DIR/vlswm.desktop"

  echo -e "${GREEN}[5/6]${NC} Verifying installation..."
  if command -v vlswm &>/dev/null; then
    VLSWM_VERSION=$(vlswm --version 2>&1 || echo "vlswm")
    echo -e "  ${GREEN}✓${NC} vlswm is available in PATH"
    echo -e "  ${GREEN}✓${NC} Version: $VLSWM_VERSION"
  else
    echo -e "  ${RED}✗${NC} Error: vlswm not found in PATH"
    exit 1
  fi

  echo -e "${GREEN}[6/6]${NC} Installing fuzzel launcher config..."
  sudo -u "$REAL_USER" mkdir -p "$REAL_HOME/.config/fuzzel"
  cp "$SCRIPT_DIR/config/fuzzel/fuzzel.ini" "$REAL_HOME/.config/fuzzel/fuzzel.ini"
  chown "$REAL_USER:$REAL_USER" "$REAL_HOME/.config/fuzzel/fuzzel.ini"
  echo -e "  ${GREEN}✓${NC} Installed: $REAL_HOME/.config/fuzzel/fuzzel.ini"

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
  echo -e "${YELLOW}Key bindings:${NC}"
  echo -e "  SUPER+ENTER     - Open terminal"
  echo -e "  SUPER+D         - Open launcher (fuzzel)"
  echo -e "  SUPER+Q         - Close window"
  echo -e "  SUPER+F         - Toggle fullscreen"
  echo -e "  SUPER+R         - Reload config (hot-reload)"
  echo -e "  SUPER+SPACE     - Toggle floating/tiling"
  echo -e "  SUPER+1..9      - Switch workspace"
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
