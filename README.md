# doom-clone

Демо-клон Doom на Си — raycasting-движок (DDA) поверх SDL2. Две карты,
несколько врагов, 5 стволов, HUD, звук, FSM состояний игры.

Технологии: **C11**, **SDL2**, **SDL2_mixer**, **ENet** и **stb_image** (vendor/).
Сборка — **CMake** (кросс-платформенная: macOS / Linux / Windows).

## Возможности

- Raycasting (DDA) с коррекцией fish-eye и Z-буфером
- Текстурированные стены + спрайты врагов и предметов
- 5 стволов: пистолет, дробовик, автомат, винтовка, гранатомёт; патроны у
  каждого свои, в начале только пистолет, хитскан-стрельба (гранатомёт пока
  не стреляет)
- ИИ врагов: `IDLE → ALERT → CHASE → ATTACK → DEAD`, 2 типа (имп / сержант)
- Предметы: аптечка, патроны, броня; двери с таймером
- HUD: HP, патроны, броня, лицо игрока, индикатор оружия
- Звук через SDL_mixer (процедурно сгенерированные WAV, без внешних файлов)
- FSM игры: `MENU → PLAYING → PAUSED → DEAD → WIN`
- Игровой цикл с фиксированным таймстеппом 60 Гц

## Сборка

### Зависимости

| ОС | Команда установки |
|---|---|
| macOS (Homebrew) | `brew install sdl2 sdl2_mixer cmake` |
| Linux (Debian/Ubuntu) | `sudo apt install libsdl2-dev libsdl2-mixer-dev cmake build-essential` |
| Linux (Fedora) | `sudo dnf install SDL2-devel SDL2-mixer-devel cmake gcc gcc-c++` |
| Windows (MSYS2/MinGW) | `pacman -S mingw-w64-x86_64-SDL2 mingw-w64-x86_64-SDL2_mixer mingw-w64-x86_64-cmake mingw-w64-x86_64-gcc` |
| Windows (vcpkg/MSVC) | `vcpkg install sdl2 sdl2-mixer` |

ENet ставить не нужно: он лежит submodule'ом в `vendor/enet/` и собирается
вместе с игрой. Клонировать репозиторий с `--recurse-submodules` или после
клона выполнить `git submodule update --init`. Сборка ENet требует ещё и
компилятора C++ — в наборах из таблицы он есть.

### Команды

```bash
cmake -S . -B build            # конфигурация (один раз)
cmake --build build           # сборка
cmake --build build --target run    # запуск из корня проекта (пересоберёт проект, если нужно)
cmake --build build --target clean  # очистка
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # отладочная сборка
ctest --test-dir build --output-on-failure      # юнит-тесты
cmake -S . -B build -DBUILD_TESTS=OFF           # собрать только игру, без тестов
cmake -S . -B build -DENABLE_MULTIPLAYER=OFF    # одиночная игра без ENet
```

Запускать нужно из корня проекта — ассеты грузятся по относительным путям.
Цель `run` делает это автоматически. Исполняемый файл: `build/doom-clone`
(или `build/doom-clone.exe` на Windows).

### Git-хуки

Хуки лежат в `.githooks/` и включаются один раз на клон — каталог
`.git/hooks/` не версионируется:

```bash
git config core.hooksPath .githooks
```

Они проверяют сообщение коммита, форматирование проиндексированных
исходников и — перед push — сборку, юнит-тесты и smoke-прогон. Подробности в
[CLAUDE.md](CLAUDE.md). Обойти любой: `--no-verify`.

## Управление

| Клавиша | Действие |
|---|---|
| W / S | Движение вперёд / назад |
| A / D | Поворот влево / вправо |
| , / . | Стрейф влево / вправо |
| Мышь | Обзор |
| Пробел / ЛКМ | Огонь |
| 1–5 | Переключение оружия |
| E | Использовать дверь |
| M | Выключить звук |
| P | Пауза |
| Enter | Подтвердить в меню / экранах |
| ESC | Выход |

## Структура проекта

```
doom-clone/
├── CMakeLists.txt        # кросс-платформенная сборка
├── README.md
├── CLAUDE.md             # инструкции для Claude Code
├── .claude/              # агенты, скиллы и хуки Claude Code
├── docs/
│   ├── architecture.md   # как устроено сейчас и почему
│   ├── roadmap.md        # куда движется проект
│   ├── requirements.md   # нефункциональные требования
│   ├── harness.md        # харнесс Claude Code: что есть и как вызывать
│   ├── tasks/            # открытые задачи с критериями приёмки
│   └── closed_tasks/     # закрытые задачи
├── vendor/               # stb_image.h, unity/ (юнит-тесты), enet/ (submodule)
├── src/                  # исходники (.c/.h)
├── tests/                # юнит-тесты на Unity, по файлу на модуль; manual/ — ручные
│                         # инструменты; что где — tests/README.md
└── assets/
    ├── maps/             # ASCII-карты: level1.txt — одиночная, dm1.txt — deathmatch
    ├── sounds/           # звуки
    ├── textures/         # текстуры стен
    └── sprites/          # враги, предметы
```

Текстуры стен/спрайтов и звуки генерируются процедурно (без внешних PNG/WAV),
но `assets_load_png()` через stb_image доступен для подмены файловыми ассетами.

## Формат карты (ASCII)

```
#  - стена (тип 1)
D  - дверь (тип 2)
P  - точка старта игрока
s  - точка спавна в сетевой игре
.  - пусто (проходимо)
E  - враг
M  - аптечка
A  - патроны
```

Карта — до 48×48, внешние границы — стены. Одиночная игра идёт на
`level1.txt` (24×24), сервер по умолчанию поднимает `dm1.txt` (48×48);
другую карту из `assets/maps/` выбирает `--map <файл>`.

## Статус

Демо готово: пройдены все четыре этапа разработки (движок, текстуры/спрайты,
стрельба/ИИ, HUD/звук/FSM) и кросс-платформенная сборка на CMake. Как всё
устроено — [docs/architecture.md](docs/architecture.md).

Ведётся перевод игры в сетевой deathmatch на 10 игроков: направление и
обоснование решений в [docs/roadmap.md](docs/roadmap.md), ограничения в
[docs/requirements.md](docs/requirements.md), конкретные работы в
[docs/tasks/](docs/tasks/).
