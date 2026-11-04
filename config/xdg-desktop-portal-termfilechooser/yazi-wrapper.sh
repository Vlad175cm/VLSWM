#!/bin/bash
# xdg-desktop-portal-termfilechooser -> yazi
# args: multiple directory save path out

multiple="$1"
directory="$2"
save="$3"
path="$4"
out="$5"

export PATH="/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin"
: "${XDG_RUNTIME_DIR:=/run/user/$(id -u)}"
export XDG_RUNTIME_DIR

if [ -z "$WAYLAND_DISPLAY" ]; then
    WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* 2>/dev/null | grep -v '\.lock$' | head -n1)")
    export WAYLAND_DISPLAY
fi

: > "$out"

mkdir -p "$HOME/.local/state"
printf '%s multiple=%s directory=%s save=%s path=%s\n' \
    "$(date '+%F %T')" "$multiple" "$directory" "$save" "$path" \
    >> "$HOME/.local/state/termfilechooser.log"

start="$path"
if [ -z "$start" ] || [ ! -d "$start" ]; then
    start="$HOME"
fi

tmp=$(mktemp /tmp/yazi-chooser.XXXXXX)
export YAZI_CONFIG_HOME="$HOME/.config/yazi-chooser"

term() {
    if command -v foot >/dev/null 2>&1; then
        foot --app-id=filechooser --title=filechooser -e "$@"
    elif [ -x /usr/bin/ghostty ]; then
        /usr/bin/ghostty --gtk-single-instance=false -e "$@"
    elif command -v kitty >/dev/null 2>&1; then
        kitty --class filechooser --title filechooser -- "$@"
    else
        xterm -e "$@"
    fi
}

if [ "$save" = "1" ]; then
    term bash -c 'printf "Save to: "; read -r -e -i "$1" p; if [ -n "$p" ]; then printf "%s\n" "$p" > "$2"; fi' \
        _ "$path" "$out"
    rm -f "$tmp"
    exit 0
fi

if [ "$directory" = "1" ]; then
    term yazi --cwd-file="$tmp" "$start"
else
    term yazi --chooser-file="$tmp" "$start"
fi

if [ -s "$tmp" ]; then
    cat "$tmp" > "$out"
    rm -f "$tmp"
    exit 0
fi

rm -f "$tmp"
: > "$out"
exit 1
