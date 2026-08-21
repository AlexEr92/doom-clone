# bug-004 — `audio_init()` теряет 8 WAV-буферов (~92 КБ) при каждом запуске

**Откуда:** обнаружено при обкатке сборки с ASan/UBSan для
`chore-005-sanitizer-build.md`
**Зависит от:** —
**Блокирует:** —
**Оценка:** ~10 строк изменений, только `audio.c`

## Что происходит

`audio_init()` (`audio.c:146-205`) синтезирует 8 звуков через `make_chunk()`
(`audio.c:127-144`): та строит WAV-буфер в памяти (`build_wav()`,
`audio.c:29-55`, `malloc`), отдаёт его `Mix_QuickLoad_WAV()`
(`audio.c:138`), которая **не копирует** буфер — сохраняет указатель прямо
в `chunk->abuf` (публичное поле, `SDL_mixer.h:230-235`: `struct Mix_Chunk {
int allocated; Uint8 *abuf; Uint32 alen; Uint8 volume; }`). Комментарий над
вызовом (`audio.c:139-142`) утверждает:

```c
/* Mix_QuickLoad_WAV copies? No — it does NOT copy the buffer; it keeps the
 * pointer. We must keep the WAV buffer alive for the chunk's lifetime.
 * Store the pointer in chunk->abuf (which IS the pointer) — that's fine,
 * but Mix_FreeChunk will free(abuf). So we must hand ownership over. */
```

Последнее утверждение («`Mix_FreeChunk` will free(abuf)») не подтверждается
ни документацией SDL_mixer, ни фактическим поведением. Заголовок
(`SDL_mixer.h:785-798`, `Mix_QuickLoad_WAV`) пишет только: «the provided
memory buffer must remain available until `Mix_FreeChunk()` is called» — то
есть буфер обязан пережить вызов `Mix_FreeChunk`, а не то, что она его
освободит. Освобождает ли она `abuf`, зависит от поля `allocated`, которое
`Mix_QuickLoad_WAV` для «чужого» буфера выставляет в `0` — то есть mixer
сознательно не берёт владение. `audio_shutdown()` (`audio.c:207-224`)
честно вызывает `Mix_FreeChunk()` для каждого чанка, но нигде не
освобождает `abuf` — ни через `free()`, ни как-то ещё.

## Как воспроизвести

```
cmake -S . -B build-san -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-san
BUILD_DIR=build-san .claude/skills/run-doom-clone/driver.sh start --build
BUILD_DIR=build-san .claude/skills/run-doom-clone/driver.sh key Return   # меню -> игра
# ... любые действия ...
BUILD_DIR=build-san .claude/skills/run-doom-clone/driver.sh key Escape   # PLAYING -> PAUSED
BUILD_DIR=build-san .claude/skills/run-doom-clone/driver.sh key Escape   # PAUSED -> MENU
BUILD_DIR=build-san .claude/skills/run-doom-clone/driver.sh key Escape   # MENU -> выход (game.c:164-169)
```

Процесс завершается сам (`return 0` из `main.c:243`), и в `.run/game.log`
появляется:

```
==<pid>==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 24298 byte(s) in 1 object(s) allocated from:
    #0 ... in malloc
    #1 ... in build_wav /home/.../src/audio.c:33
    #2 ... in make_chunk /home/.../src/audio.c:133
    #3 ... in audio_init /home/.../src/audio.c:181
    #4 ... in main /home/.../src/main.c:99
...
SUMMARY: AddressSanitizer: 94282 byte(s) leaked in 8 allocation(s).
```

Все 8 утечек — тот же стек через `build_wav → make_chunk → audio_init`, с
местом вызова на строках `audio.c:170` (`SND_PISTOL`), `174` (`SND_SHOTGUN`),
`178` (`SND_ENEMY_HURT`), `181` (`SND_ENEMY_DEATH`), `184`
(`SND_PLAYER_HURT`), `187` (`SND_PICKUP`), `190` (`SND_DOOR`), `201`
(`SND_NO_AMMO`) — то есть все восемь звуков без исключения.

Важно: остановка через `driver.sh stop` (`pkill -x doom-clone`) **не
воспроизводит находку** — `SIGTERM` без обработчика обрывает процесс до
`atexit`-хука `LeakSanitizer`; нужен именно штатный выход через `Escape`
из меню (см. «Как воспроизвести» выше и `chore-005-sanitizer-build.md`,
«Контекст», где это разобрано подробнее вместе с обоснованием, зачем
драйверу нужна отдельная команда `quit`).

## Что сделать

- [ ] В `audio_shutdown()` (`audio.c:207-224`), перед освобождением каждого
      чанка, сохранить `a->chunks[i]->abuf` и освободить его через `free()`
      **после** `Mix_FreeChunk()` (структура `Mix_Chunk` к этому моменту уже
      освобождена, но локально сохранённый указатель на буфер остаётся
      валидным), под защитой `allocated == 0` — освобождать буфер вручную
      только тогда, когда сам mixer не взял на себя владение; это защищает
      от двойного `free`, если когда-нибудь часть чанков станет грузиться
      через `Mix_LoadWAV`/`Mix_LoadWAV_RW` (`allocated == 1`) вместо
      `Mix_QuickLoad_WAV`
- [ ] Поправить или убрать неверный комментарий на `audio.c:139-142` —
      он утверждает обратное тому, что на самом деле делает `Mix_FreeChunk`
- [ ] Пересобрать с ASan/UBSan (рецепт выше или `-DENABLE_SANITIZERS=ON`
      из `chore-005-sanitizer-build.md`, если та уже закрыта) и повторить
      воспроизведение — `LeakSanitizer` не должен появиться в
      `.run/game.log`

## Затрагиваемые файлы

- `src/audio.c`

## Критерий готовности

- [ ] `grep -n "abuf" src/audio.c` показывает `free()` в `audio_shutdown()`,
      под условием на `allocated`
- [ ] Пересборка с ASan/UBSan + штатный выход через `Escape` из меню (см.
      «Как воспроизвести») не выводит `LeakSanitizer` в `.run/game.log`
- [ ] `.claude/skills/run-doom-clone/driver.sh smoke` на обычной сборке
      (без санитайзеров) проходит без изменений в выводе
- [ ] `clang-format --dry-run -Werror src/audio.c` проходит чисто

## Замечания

Утечка одноразовая при старте процесса: `audio_init()` вызывается ровно
один раз (`main.c:99`), рестарт матча (`game.restart`, `main.c:159-168`)
переиспользует ту же `Audio` без повторной инициализации — то есть это не
утечка, растущая со временем, а фиксированные ~92 КБ на весь процесс.
Заметности с земли (глазами, по `driver.sh smoke`) это не имеет — играбельность
не страдает. Ценность именно в том, что это ровно тот класс дефекта, который
`chore-005-sanitizer-build.md` вводит санитайзеры чтобы ловить: невидим ни
компилятору, ни cppcheck, и без него остался бы незамеченным до появления
раздела 9/10 `requirements.md` (замер утечек на длинном матче).
