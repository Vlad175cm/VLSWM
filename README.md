# vlswm

Минималистичный тайловый оконный менеджер (compositor) для **Wayland** на базе **wlroots 0.20**, написанный на **C++20**.

> ⚠️ **ПРОЕКТ ЭКСПЕРИМЕНТАЛЬНЫЙ.** Написан с помощью **искусственного интеллекта (AI)**. В коде могут быть баги, неоптимальные места и неожиданное поведение. Используйте на свой страх и риск.

---

## Содержание

- [Внимание](#внимание)
- [Возможности](#возможности)
- [Рабочие столы](#рабочие-столы)
- [Оперативная память](#оперативная-память)
- [Поддержка оборудования](#поддержка-оборудования)
- [Зависимости](#зависимости)
- [Сборка](#сборка)
- [Установка](#установка)
- [Запуск](#запуск)
- [Горячие клавиши](#горячие-клавиши)
- [Конфигурация](#конфигурация)
- [Структура проекта](#структура-проекта)
- [Изменения](#изменения)
- [Лицензия](#лицензия)
- [English version](#english-version)

---

## Внимание

- **Экспериментальный проект, созданный при помощи ИИ.**
- **В проекте возможны проблемы** (issues), баги и артефакты — проект находится в активной разработке.
- **Некоторые настройки конфига могут не работать** — если что-то не работает, значит функция ещё не реализована.
- Я **рекомендую не использовать полностью прозрачные приложения** — при низкой непрозрачности окон и скруглённых углах возможны артефакты отрисовки (наложение, фантомные следы, проблемы с сортировкой слоёв).
- **В WM нет панели (bar).** Поддержки **waybar** и **quickshell** **нет** — не пытайтесь их подключать/ожидать.
- Использует **~75–76 МБ ОЗУ** в режиме бездействия (idle).

## Возможности

- Tайловый layout **master + stack** (главное окно слева, остальные — справа стопкой)
- **Рабочие столы 1–9** (`SUPER+1..9`), перемещение окна между столами (`SUPER+SHIFT+1..9`)
- **Анимации** открытия/закрытия/изменения размера окон (fade + slide, easing: ease-in / ease-out / ease-in-out)
- **Скруглённые углы** окон (радиус настраивается) — **в разработке, имеют баг**
- **Прозрачность** окон (настраивается)
- **Обои** (путь в конфиге; PNG/JPEG)
- **Floating-режим** — **в разработке**
- **Горячие клавиши** и **мультимедиа-клавиши** (яркость экрана/клавиатуры, громкость, mute)
- **fuzzel** (лаунчер) — в комплекте конфиг `config/fuzzel/fuzzel.ini` (взят из [driftwm](https://github.com/malbiruk/driftwm))
- **Горячая перезагрузка конфига** (`SUPER+R`) без перезапуска WM
- **XWayland** (запуск X11-приложений)
- **SDDM-сессия** (setup через `install.sh`)
- Логирование в `~/.local/state/vlswm/vlswm.log`

## Рабочие столы

Рабочие столы переключаются мгновенно (без анимации).

> ⭐ **Фича, а не баг:** рабочие столы **открываются (спавнятся) друг поверх друга** — окна разных рабочих столов могут перекрывать друг друга. Это сделано намеренно.

## Оперативная память

**~75–76 МБ** при idle (без открытых окон).

## Поддержка оборудования

| Компонент | Статус |
|-----------|--------|
| Backend | DRM/KMS (wlroots session: logind/seatd) |
| Renderer | **GLES2** (основной); **Pixman** — для headless-тестов |
| Allocator | GBM |
| Ввод | libinput (клавиатура, мышь, тачпад) |
| XWayland | Да |
| Сессия | SDDM / любой wayland-сессионный менеджер |
| Нескольких мониторов | Код поддерживает, **но тестировалось только на одном** |

- **Тестировалось:** Intel HD Graphics 6000 (Broadwell GT3), Mesa 26.x, Arch Linux
- **Не тестировалось:** NVIDIA (возможны проблемы с GLES/GBM), ARM-GPU, AMD (скорее всего работает, но не проверено)
- Не рекомендуется использовать с очень старым железом или очень медленными GPU — применяются полноэкранный композитинг, прозрачность и скругления

## Зависимости

> 💡 **`install.sh` может установить все зависимости автоматически.** Скрипт запросит права root и спросит, какой у вас дистрибутив (Arch / Fedora / Debian / Ubuntu / openSUSE), затем сам установит нужные пакеты через ваш менеджер пакетов. Ниже — списки для ручной установки.

Обязательная версия: **wlroots 0.20** (в репозиториях некоторых дистрибутивов версия может отличаться — см. предупреждение ниже).

### Arch Linux

```bash
sudo pacman -S --needed wayland wlroots0.20 wayland-protocols libxkbcommon \
  pixman libdrm libinput cairo pango gdk-pixbuf2 libxcb xcb-util-wm \
  cmake make gcc

# рекомендуемые рантайм-инструменты
sudo pacman -S --needed kitty fuzzel brightnessctl wireplumber xorg-xwayland sddm
```

### Fedora

```bash
sudo dnf install wayland-devel wlroots-devel xkbcommon-devel pixman-devel \
  libdrm-devel libinput-devel cairo-devel pango-devel gdk-pixbuf2-devel \
  libxcb-devel xcb-util-wm-devel cmake gcc-c++ make

# рекомендуемые рантайм-инструменты
sudo dnf install kitty fuzzel brightnessctl pipewire wireplumber \
  xorg-x11-server-Xwayland sddm
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install libwlroots-dev libwayland-dev wayland-protocols \
  libxkbcommon-dev libpixman-1-dev libdrm-dev libinput-dev libcairo2-dev \
  libpango1.0-dev libgdk-pixbuf-2.0-dev libxcb-dev libxcb-ewmh-dev \
  libxcb-icccm4-dev cmake g++ make

# рекомендуемые рантайм-инструменты
sudo apt install kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
```

### openSUSE

```bash
sudo zypper install wlroots-devel wayland-devel libxkbcommon-devel \
  pixman-devel libdrm-devel libinput-devel cairo-devel pango-devel \
  gdk-pixbuf-devel libxcb-devel xcb-util-wm-devel cmake gcc-c++ make

# рекомендуемые рантайм-инструменты
sudo zypper install kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
```

> ⚠️ **Wlroots 0.20 обязателен.** В Debian/Ubuntu и старых релизах Fedora/openSUSE в официальных репозиториях может быть более старая версия wlroots. В этом случае либо соберите **wlroots 0.20.2** из исходников, либо используйте vendored sysroot (см. [Сборка](#сборка)).

## Сборка

```bash
# если wlroots 0.20 установлен системно:
cmake -B build
cmake --build build

# если wlroots 0.20 НЕ установлен системно (vendored sysroot):
export PKG_CONFIG_PATH="$HOME/.local/vlswm-sysroot/usr/lib/pkgconfig"
cmake -B build
cmake --build build
```

Бинарник: `build/vlswm`.

## Установка

```bash
sudo ./install.sh
```

Скрипт:
1. требует root (перезапускает себя через `sudo`);
2. **спрашивает ваш дистрибутив и автоматически устанавливает все зависимости** (build + runtime) через ваш менеджер пакетов;
3. при отсутствии бинарника собирает проект;
4. копирует бинарник в `/usr/bin/vlswm`;
5. копирует конфиг в `~/.config/vlswm/config` (существующий не перезаписывает, а сохраняет как `config.backup`) и конфиг fuzzel в `~/.config/fuzzel/`;
6. создаёт SDDM-сессию `/usr/share/wayland-sessions/vlswm.desktop`.

После установки: выйдите из сессии → в SDDM выберите **vlswm** → войдите.

## Запуск

Из TTY или вместо текущего композитора:

```bash
build/vlswm
```

Headless-режим для быстрой проверки:

```bash
WLR_BACKENDS=headless build/vlswm &
WAYLAND_DISPLAY=wayland-1 kitty  # любой wayland-клиент
```

## Горячие клавиши

| Клавиши | Действие |
|---------|----------|
| `SUPER+ENTER` | Открыть терминал (из конфига `[launcher] terminal` или `$TERMINAL`; по умолчанию `kitty`) |
| `SUPER+D` | Лаунчер приложений (`fuzzel`) |
| `SUPER+Q` | Закрыть сфокусированное окно (анимированно) |
| `SUPER+F` | Полноэкранный режим |
| `SUPER+SPACE` | Переключить floating / tiling (**floating в разработке**) |
| `SUPER+R` | Hot-reload конфига (без перезапуска) |
| `SUPER+←` / `SUPER+→` | Циклическое переключение фокуса влево / вправо |
| `SUPER+1..9` | Перейти на рабочий стол 1–9 |
| `SUPER+SHIFT+1..9` | Переместить окно на рабочий стол 1–9 |
| `SUPER+ESC` / `CTRL+ALT+ESC` | Выйти из vlswm |
| `XF86MonBrightnessUp/Down` | Яркость экрана (+/-5%) |
| `XF86AudioRaiseVolume/LowerVolume/Mute` | Громкость (wpctl) |
| `XF86KbdBrightnessUp/Down` | Яркость подсветки клавиатуры |

## Конфигурация

Путь: `~/.config/vlswm/config` (пример: `/home/user/vlswm/config`). После изменения нажмите `SUPER+R`.

> ⚠️ Некоторые настройки конфига **могут не работать** — это нормально: проект в разработке, функции добавляются постепенно. Ниже отмечено, что реализовано, а что нет.

```ini
[wm]
gaps = true                 # отступы между окнами        [работает]
gap_size = 8                # размер отступа              [работает]
border_enabled = false      # рамка окон                  [НЕ РЕАЛИЗОВАНО]
border_width = 2            # толщина рамки               [НЕ РЕАЛИЗОВАНО]
corner_radius = 12          # радиус скругления углов     [работает]

[windows]
opacity_enabled = true      # включать прозрачность окон  [работает]
opacity = 0.75              # непрозрачность (0.0-1.0)    [работает]
                            #   не рекомендуется полностью прозрачные
                            #   приложения — возможны артефакты

[animations]
enabled = true              # анимации                    [работает]
duration = 300              # длительность, мс            [работает]
curve = "ease-out"          # ease-in | ease-out | ease-in-out [работает]

[wallpaper]
path = "/path/to/wallpapers"   # путь к обоям    [работает]

[launcher]
terminal = "kitty"          # терминал для SUPER+ENTER     [работает]

[keybinds]
terminal = "SUPER+ENTER"
launcher = "SUPER+D"
close = "SUPER+Q"
fullscreen = "SUPER+F"
floating = "SUPER+SPACE"
reload = "SUPER+R"
exit = "SUPER+ESC"

[keybinds.workspaces]
"1" = "SUPER+1"
"2" = "SUPER+2"
# ... до 9
```

Рекомендация: **не используйте полностью прозрачные приложения** — при низкой непрозрачности + скруглённых углах + композитинге возможны артефакты отрисовки.

## Структура проекта

```
vlswm/
├── CMakeLists.txt          # сборка (CMake)
├── install.sh              # установка (бинарник, конфиг, SDDM-сессия)
├── vlswm.desktop           # шаблон wayland-сессии для SDDM
├── include/vlswm/
│   ├── server.h            # ядро, цикл событий
│   ├── view.h              # окна (toplevel)
│   ├── animation.h         # анимации
│   ├── corners.h           # скруглённые углы
│   ├── ini.h               # парсер конфига
│   ├── wallpaper.h         # обои
│   ├── xwayland.h          # XWayland
│   └── wlroots.h           # обёртки wlroots для C++ (extern "C")
├── src/
│   ├── main.cpp            # входная точка, лог, сигналы
│   ├── server.cpp          # ядро: seat, hotkeys, workspace, spawn
│   ├── output.cpp          # мониторы/вывод
│   ├── view.cpp            # окна, фокус, floating/fullscreen
│   ├── layout.cpp          # master + stack
│   ├── ini.cpp             # парсер конфига
│   ├── animation.cpp       # easing-кривые
│   ├── wallpaper.cpp       # загрузка обоев
│   ├── corners.cpp         # рендер скруглённых углов
│   └── xwayland.c          # интеграция XWayland
└── config/
    ├── config               # пример конфига
    └── fuzzel/fuzzel.ini    # конфиг лаунчера fuzzel (взят из driftwm)
```

## Изменения

Текущие возможности (v0.1.0):

- Wayland-композитор на wlroots 0.20 (scene-graph)
- GLES2-рендерер, GBM-аллокатор, DRM-бэкенд
- Клавиатура/мышь/тачпад (libinput, seat)
- xdg-toplevel окна, управление фокусом
- Тайлинг master + stack, отступы (gaps)
- Рабочие столы 1–9 + перемещение окон между столами
- Анимации открытия/закрытия/ресайза
- Скруглённые углы
- Прозрачность окон
- Обои (PNG/JPEG)
- Конфиг (INI) с hot-reload без перезапуска
- Горячие и мультимедиа-клавиши
- XWayland
- SDDM-сессия и установщик
- Логирование в `~/.local/state/vlswm/vlswm.log`

## Лицензия

[MIT](LICENSE). Проект распространяется «как есть», без гарантий.

---

# English version

# vlswm

Minimalistic tiling **Wayland** window manager (compositor) built on **wlroots 0.20**, written in **C++20**.

> ⚠️ **EXPERIMENTAL PROJECT.** Written with the help of **artificial intelligence (AI)**. The code may contain bugs, non-optimal parts and unexpected behaviour. Use at your own risk.

---

## Table of Contents

- [Warning](#warning)
- [Features](#features)
- [Workspaces](#workspaces)
- [Memory](#memory)
- [Hardware support](#hardware-support)
- [Dependencies](#dependencies)
- [Build](#build)
- [Install](#install)
- [Run](#run)
- [Key bindings](#key-bindings)
- [Configuration](#configuration)
- [Project structure](#project-structure)
- [Changelog](#changelog)
- [License](#license)

---

## Warning

- **Experimental project created with AI.**
- **Issues and bugs are possible** in this project — it is in active development.
- **Some configuration settings may not work** — if something does not work, it means the feature is not implemented yet.
- I **recommend not to use fully transparent applications** — with low window opacity and rounded corners there may be rendering artifacts (overlapping, ghost trails, layer sorting issues).
- **This WM has NO bar (panel).** Support for **waybar** and **quickshell** is **NOT present** — do not expect them to work.
- Uses **~75–76 MB of RAM** when idle.

## Features

- Tiling layout **master + stack** (main window on the left, the rest stacked on the right)
- **Workspaces 1–9** (`SUPER+1..9`), move window between workspaces (`SUPER+SHIFT+1..9`)
- **Animations** for window open/close/resize (fade + slide, easing: ease-in / ease-out / ease-in-out)
- **Rounded corners** (configurable radius) — **under development, has a bug**
- **Window opacity** (configurable)
- **Wallpaper** (path in config; PNG/JPEG)
- **Floating mode** — **under development**
- **Hotkeys** and **multimedia keys** (screen/keyboard brightness, volume, mute)
- **fuzzel** (launcher) — bundled config `config/fuzzel/fuzzel.ini` (taken from [driftwm](https://github.com/malbiruk/driftwm))
- **Hot config reload** (`SUPER+R`) without restarting the WM
- **XWayland** (run X11 applications)
- **SDDM session** (set up via `install.sh`)
- Logs to `~/.local/state/vlswm/vlswm.log`

## Workspaces

Workspaces switch instantly (no animation).

> ✅ **Feature, not a bug:** workspaces **spawn on top of each other** — windows from different workspaces may overlap. This is intentional.

## Memory

**~75–76 MB** when idle (no open windows).

## Hardware support

| Component | Status |
|-----------|--------|
| Backend | DRM/KMS (wlroots session: logind/seatd) |
| Renderer | **GLES2** (primary); **Pixman** — for headless testing |
| Allocator | GBM |
| Input | libinput (keyboard, mouse, touchpad) |
| XWayland | Yes |
| Session | SDDM / any wayland session manager |
| Multiple monitors | Supported by code, **but only a single monitor was tested** |

- **Tested on:** Intel HD Graphics 6000 (Broadwell GT3), Mesa 26.x, Arch Linux
- **Not tested:** NVIDIA (possible GLES/GBM issues), ARM GPUs, AMD (should work but unverified)
- Not recommended on very old or slow hardware — full-screen compositing, opacity and rounded corners are used

## Dependencies

> 💡 **`install.sh` can install all dependencies automatically.** The script will ask for root, ask which distro you use (Arch / Fedora / Debian / Ubuntu / openSUSE), and then install everything through your package manager. Below are the lists for manual installation.

Required version: **wlroots 0.20** (some distro repos may ship a different version — see the note below).

### Arch Linux

```bash
sudo pacman -S --needed wayland wlroots0.20 wayland-protocols libxkbcommon \
  pixman libdrm libinput cairo pango gdk-pixbuf2 libxcb xcb-util-wm \
  cmake make gcc

# recommended runtime tools
sudo pacman -S --needed kitty fuzzel brightnessctl wireplumber xorg-xwayland sddm
```

### Fedora

```bash
sudo dnf install wayland-devel wlroots-devel xkbcommon-devel pixman-devel \
  libdrm-devel libinput-devel cairo-devel pango-devel gdk-pixbuf2-devel \
  libxcb-devel xcb-util-wm-devel cmake gcc-c++ make

# recommended runtime tools
sudo dnf install kitty fuzzel brightnessctl pipewire wireplumber \
  xorg-x11-server-Xwayland sddm
```

### Debian / Ubuntu

```bash
sudo apt update
sudo apt install libwlroots-dev libwayland-dev wayland-protocols \
  libxkbcommon-dev libpixman-1-dev libdrm-dev libinput-dev libcairo2-dev \
  libpango1.0-dev libgdk-pixbuf-2.0-dev libxcb-dev libxcb-ewmh-dev \
  libxcb-icccm4-dev cmake g++ make

# recommended runtime tools
sudo apt install kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
```

### openSUSE

```bash
sudo zypper install wlroots-devel wayland-devel libxkbcommon-devel \
  pixman-devel libdrm-devel libinput-devel cairo-devel pango-devel \
  gdk-pixbuf-devel libxcb-devel xcb-util-wm-devel cmake gcc-c++ make

# recommended runtime tools
sudo zypper install kitty fuzzel brightnessctl pipewire wireplumber xwayland sddm
```

> ⚠️ **Wlroots 0.20 is required.** Debian/Ubuntu and old Fedora/openSUSE releases may ship an older wlroots in their official repos. In that case either build **wlroots 0.20.2** from source, or use the vendored sysroot (see [Build](#build)).

## Build

```bash
# if wlroots 0.20 is installed system-wide:
cmake -B build
cmake --build build

# if wlroots 0.20 is NOT installed system-wide (vendored sysroot):
export PKG_CONFIG_PATH="$HOME/.local/vlswm-sysroot/usr/lib/pkgconfig"
cmake -B build
cmake --build build
```

Binary: `build/vlswm`.

## Install

```bash
sudo ./install.sh
```

The script:
1. requires root (re-runs itself with `sudo`);
2. **asks which distro you use and installs all dependencies automatically** (build + runtime) via your package manager;
3. builds the project if the binary is absent;
4. copies the binary to `/usr/bin/vlswm`;
5. copies the config to `~/.config/vlswm/config` (does not overwrite an existing one, saves it as `config.backup`) and the fuzzel config to `~/.config/fuzzel/`;
6. creates the SDDM session `/usr/share/wayland-sessions/vlswm.desktop`.

After install: log out → select **vlswm** in SDDM → log in.

## Run

From a TTY or instead of your current compositor:

```bash
build/vlswm
```

Headless quick test:

```bash
WLR_BACKENDS=headless build/vlswm &
WAYLAND_DISPLAY=wayland-1 kitty  # any wayland client
```

## Key bindings

| Keys | Action |
|------|--------|
| `SUPER+ENTER` | Open terminal (from config `[launcher] terminal` or `$TERMINAL`; defaults to `kitty`) |
| `SUPER+D` | App launcher (`fuzzel`) |
| `SUPER+Q` | Close focused window (animated) |
| `SUPER+F` | Toggle fullscreen |
| `SUPER+SPACE` | Toggle floating / tiling (**floating under development**) |
| `SUPER+R` | Hot-reload config (no restart) |
| `SUPER+←` / `SUPER+→` | Cycle focus left / right |
| `SUPER+1..9` | Switch to workspace 1–9 |
| `SUPER+SHIFT+1..9` | Move window to workspace 1–9 |
| `SUPER+ESC` / `CTRL+ALT+ESC` | Exit vlswm |
| `XF86MonBrightnessUp/Down` | Screen brightness (+/-5%) |
| `XF86AudioRaiseVolume/LowerVolume/Mute` | Volume (wpctl) |
| `XF86KbdBrightnessUp/Down` | Keyboard backlight brightness |

## Configuration

Path: `~/.config/vlswm/config` (example: `/home/user/vlswm/config`). After changing it, press `SUPER+R`.

> ⚠️ Some config settings **may not work** — that is normal: the project is under development and features are added gradually. Below, what works and what does not is marked.

```ini
[wm]
gaps = true                 # gaps between windows           [WORKS]
gap_size = 8                # gap size                       [WORKS]
border_enabled = false      # window border                  [NOT IMPLEMENTED]
border_width = 2            # border width                   [NOT IMPLEMENTED]
corner_radius = 12          # corner radius                  [WORKS]

[windows]
opacity_enabled = true      # enable window opacity          [WORKS]
opacity = 0.75              # opacity (0.0-1.0)              [WORKS]
                            #   fully transparent apps are NOT recommended
                            #   — rendering artifacts possible

[animations]
enabled = true              # animations                     [WORKS]
duration = 300              # duration, ms                   [WORKS]
curve = "ease-out"          # ease-in | ease-out | ease-in-out [WORKS]

[wallpaper]
path = "/path/to/wallpapers"   # wallpaper path     [WORKS]

[launcher]
terminal = "kitty"          # terminal for SUPER+ENTER        [WORKS]

[keybinds]
terminal = "SUPER+ENTER"
launcher = "SUPER+D"
close = "SUPER+Q"
fullscreen = "SUPER+F"
floating = "SUPER+SPACE"
reload = "SUPER+R"
exit = "SUPER+ESC"

[keybinds.workspaces]
"1" = "SUPER+1"
"2" = "SUPER+2"
# ... up to 9
```

Recommendation: **do not use fully transparent applications** — with low opacity, rounded corners and compositing you may get rendering artifacts.

## Project structure

```
vlswm/
├── CMakeLists.txt          # build (CMake)
├── install.sh              # installer (binary, config, SDDM session)
├── vlswm.desktop           # wayland session template for SDDM
├── include/vlswm/
│   ├── server.h            # core, event loop
│   ├── view.h              # windows (toplevel)
│   ├── animation.h         # animations
│   ├── corners.h           # rounded corners
│   ├── ini.h               # config parser
│   ├── wallpaper.h         # wallpaper
│   ├── xwayland.h          # XWayland
│   └── wlroots.h           # wlroots C++ wrappers (extern "C")
├── src/
│   ├── main.cpp            # entry point, logging, signals
│   ├── server.cpp          # core: seat, hotkeys, workspaces, spawn
│   ├── output.cpp          # monitors / output
│   ├── view.cpp            # windows, focus, floating/fullscreen
│   ├── layout.cpp          # master + stack
│   ├── ini.cpp             # config parser
│   ├── animation.cpp       # easing curves
│   ├── wallpaper.cpp       # wallpaper loading
│   ├── corners.cpp         # rounded-corner rendering
│   └── xwayland.c          # XWayland integration
└── config/
    ├── config               # example config
    └── fuzzel/fuzzel.ini    # fuzzel launcher config (taken from driftwm)
```

## Changelog

Current features (v0.1.0):

- Wayland compositor on wlroots 0.20 (scene-graph)
- GLES2 renderer, GBM allocator, DRM backend
- Keyboard / mouse / touchpad (libinput, seat)
- xdg-toplevel windows, focus management
- Master + stack tiling, gaps
- Workspaces 1–9 + moving windows between workspaces
- Open/close/resize animations
- Rounded corners
- Window opacity
- Wallpaper (PNG/JPEG)
- INI config with hot reload without restart
- Hotkeys and multimedia keys
- XWayland
- SDDM session and installer
- Logging to `~/.local/state/vlswm/vlswm.log`

## License

[MIT](LICENSE). The project is provided "as is", without warranties.