# vlswm

Минимальная Wayland-оболочка на **wlroots 0.20** для Arch Linux. Рабочий стол — это терминал (Ghostty), приложения открываются поверх него на весь экран. Только чёрный и белый, только прямые углы, без обоев.

> Экспериментальный проект. Писался с помощью ИИ.

## Как это работает

- При запуске сразу открывается **Ghostty** на весь экран. Это базовый слой: закрыть его нельзя, второй экземпляр открыть нельзя. Если он завершился (например, `exit` в fish), vlswm запускает его снова.
- Любое приложение открывается **поверх** на весь экран. Окна лежат стопкой.
- `SUPER+←` / `SUPER+→` переключает окна по кругу.
- `SUPER+Q` закрывает верхнее окно. Базовый Ghostty не закрывается.
- `SUPER+D` открывает лаунчер: белый (или чёрный) прямоугольник по центру с полем ввода. Введите команду и нажмите `Enter`, она выполнится через `fish -c`. `Esc` отменяет.
- `SUPER+T` переключает тему: чёрный ↔ белый. Меняются фон экрана, рамка и поле лаунчера. Тема Ghostty тоже меняется: vlswm записывает инвертированную палитру и посылает Ghostty сигнал перечитать конфиг, без закрытия.
- Анимация одна: fade 300 мс при открытии, закрытии и появлении лаунчера.
- X11-приложения работают через `xwayland-satellite`.

## Зависимости

- wlroots 0.20
- wayland, xkbcommon, pixman, libinput, cairo, pango, pangocairo
- Рантайм: `ghostty`, `fish`, `xwayland-satellite`, `brightnessctl` (яркость), `wireplumber` (`wpctl`, громкость)
- Шрифт `JetBrainsMono Nerd Font` (или любой другой, указанный в `[launcher] font`)

## Сборка и установка

```bash
cmake -B build
cmake --build build
```

Бинарник: `build/vlswm`. Установка: `sudo ./install.sh`.

Запуск из TTY или из SDDM-сессии `vlswm`. Другой конфиг: `vlswm -c /путь/к/config`.

## Конфигурация

Файл: `~/.config/vlswm/config`. Перечитать без перезапуска: `SUPER+R`.

Комбинации записываются как `МОДИФИКАТОРЫ+КЛАВИША`. Модификаторы: `SUPER`, `SHIFT`, `CTRL`, `ALT`. Одно действие может иметь несколько клавиш через запятую.

```ini
[keybinds]
launcher = "SUPER+D"
close = "SUPER+Q"
reload = "SUPER+R"
exit = "SUPER+ESC"
theme = "SUPER+T"
focus_prev = "SUPER+LEFT"
focus_next = "SUPER+RIGHT"
zen = "SUPER+Z"                 # приложение: ключ из [commands]
volume_up = "XF86AudioRaiseVolume, SUPER+F4"

[commands]
zen = "zen-browser"            # любое приложение: имя = команда
volume_up = "wpctl set-volume @DEFAULT_AUDIO_SINK@ 5%+"

[launcher]
terminal = "ghostty"
font = "JetBrainsMono Nerd Font 16"

[theme]
ghostty = "~/.config/ghostty"  # каталог конфига Ghostty

[animations]
duration = 300                 # мс

[input]
repeat_delay = 200             # мс до первого повтора
repeat_rate = 40               # повторов в секунду
repeat = "focus_next, focus_prev, volume_up, volume_down, brightness_up, brightness_down"

[xwayland]
satellite = "xwayland-satellite"
display = ":1"
```

### Как добавить приложение на клавишу

1. В `[commands]` задайте имя и команду: `имя = "команда"`.
2. В `[keybinds]` задайте то же имя и клавишу: `имя = "SUPER+X"`.

Приложения при удержании клавиши не повторяются. Повторяются только действия из списка `[input] repeat`.

### Тема Ghostty

vlswm читает цвета из вашего `config.ghostty` (и из подключённых через `config-file` файлов), создаёт файл `vlswm-theme.ghostty` в каталоге Ghostty и добавляет в `config.ghostty` строку `config-file = vlswm-theme.ghostty`. Цвета Ghostty управляются vlswm: редактировать их в `config.ghostty` бессмысленно, файл `vlswm-theme.ghostty` перезаписывается при каждом переключении темы.

## Клавиши по умолчанию

| Клавиши | Действие |
|---|---|
| `SUPER+D` | лаунчер |
| `SUPER+Q` | закрыть верхнее окно |
| `SUPER+←` / `SUPER+→` | переключить окно |
| `SUPER+T` | чёрный / белый |
| `SUPER+R` | перечитать конфиг |
| `SUPER+ESC` | выход |
| `SUPER+F1` / `SUPER+F2` | яркость −/+ |
| `SUPER+F3` / `SUPER+F4` | громкость −/+ |

## Структура

- `src/main.cpp` — запуск, лог, аргументы.
- `src/server.cpp` — ввод, привязки, стек окон, процессы, xwayland-satellite.
- `src/view.cpp` — окна (xdg), открытие и закрытие.
- `src/launcher.cpp` — лаунчер (cairo + pango).
- `src/theme.cpp` — тема Ghostty.
- `src/output.cpp`, `src/ini.cpp`, `src/animation.cpp` — выход, конфиг, анимации.

## Лицензия

См. файл `LICENSE`.
