#!/bin/bash
# Build vlswm from this checkout and install it system-wide.
# Usage: sudo ./update.sh

set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REAL_USER="${SUDO_USER:-$USER}"
REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"

if [ "$EUID" -ne 0 ]; then
    echo "Run with sudo: sudo $0" >&2
    exit 1
fi
if [ -z "$REAL_HOME" ] || [ ! -d "$REAL_HOME" ]; then
    echo "Cannot resolve home directory for $REAL_USER" >&2
    exit 1
fi

echo "==> Building in $DIR/build"
mkdir -p "$DIR/build"
chown -R "$REAL_USER:$REAL_USER" "$DIR/build"
sudo -u "$REAL_USER" cmake -B "$DIR/build" -DCMAKE_BUILD_TYPE=Release
sudo -u "$REAL_USER" cmake --build "$DIR/build" -j"$(nproc)"

echo "==> Installing binaries to /usr/bin"
install -m 755 -s "$DIR/build/vlswm" /usr/bin/vlswm
install -m 755 -s "$DIR/build/vlswmstat" /usr/bin/vlswmstat

echo "==> Installing scripts to $REAL_HOME/.local/bin"
install -Dm755 "$DIR/scripts/vlswm-record" "$REAL_HOME/.local/bin/vlswm-record"
install -Dm755 "$DIR/scripts/vlswm-screenshot" "$REAL_HOME/.local/bin/vlswm-screenshot"
chown "$REAL_USER:$REAL_USER" "$REAL_HOME/.local/bin/vlswm-record" \
    "$REAL_HOME/.local/bin/vlswm-screenshot"

echo "==> Installing SDDM session"
install -m 644 "$DIR/vlswm.desktop" /usr/share/wayland-sessions/vlswm.desktop

echo "==> Done: $(/usr/bin/vlswm --version), $(/usr/bin/vlswmstat --version)"
echo "    Log out and back in (SUPER+ESC) to use the new binary."
