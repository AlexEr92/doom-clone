# chore-003 — Указатели без `const`, хотя только читаются

**Откуда:** найдено `.claude/skills/lint/lint.sh strict`
(`constParameterPointer` / `constVariablePointer`)
**Зависит от:** —
**Блокирует:** —
**Оценка:** ~12 строк изменений: шесть мест из отчёта плюс цепочка
параметров, которые станут `const` вслед за ними (см. «Замечания»); оценка
по чтению кода, самой правки не пробовали

## Контекст

`.claude/skills/lint/lint.sh strict` сообщает шесть находок уровня
`const*Pointer`. Каждая проверена по коду: указатель нигде в функции не
пишет через себя, а всё, куда он передаётся, принимает `const`-указатель.

1. **`src/enemy.c:109`** — `can_see_player(..., DoorList *dl, ...)`. `dl`
   уходит только в `world_line_of_sight()`, чей параметр —
   `const struct DoorList *` (`src/raycast_world.h`).

2. **`src/enemy.c:132`** — в `pick_target()` локальная
   `PlayerState *p = &players[i];`. Дальше `p` идёт только в
   `is_targetable(const PlayerState *)` и `can_see_player(...,
   const PlayerState *p, ...)`, через неё ничего не пишется.

3. **`src/enemy.c:150`** — `move_towards(..., DoorList *dl, ...)`. `dl`
   используется двумя вызовами `map_is_wall_door()`, а тот принимает
   `const struct DoorList *` (`src/map.h`).

4. **`src/game.c:14`** — `game_handle_event(Game *g, InputState *in,
   SDL_Event *ev)`. Тело читает только `ev->type` и `ev->key.keysym.sym`,
   присваиваний через `ev` нет. Объявление — `src/game.h:30`; вызывающие
   (`src/main.c`, `tests/test_game.c`) передают обычный `SDL_Event *`, что
   в `const`-параметр идёт без приведений.

5. **`src/player.c:45`** — `blocked(const Map *m, DoorList *dl, float x,
   float y)`. Статическая функция из одного вызова `map_is_wall_door()`.

6. **`src/world.c:169`** — в цикле дверей `world_step()` локальная
   `PlayerState *p = &w->players[i];`. Дальше `p` читается
   (`p->active`) и передаётся в `door_try_use(DoorList *, const PlayerState
   *, ...)` — запись идёт в `w->doors`, не в игрока.

## Что сделать

- [ ] `src/enemy.c`: `const DoorList *dl` в `can_see_player()` и
      `move_towards()`; `const PlayerState *p` в цикле `pick_target()`
- [ ] `src/enemy.c`: `const DoorList *dl` и `const PlayerState *players` в
      `pick_target()` — они становятся `const` вслед за пунктом выше
- [ ] `src/enemy.c` и `src/enemy.h`: `enemy_update_all()` принимает
      `const struct DoorList *dl` — через неё идут только вызовы тех же
      `const`-функций. `players` остаётся неконстантным: через
      `target_of()` по нему пишет урон
- [ ] `src/game.c` и `src/game.h`: `game_handle_event(Game *g, InputState
      *in, const SDL_Event *ev)`
- [ ] `src/player.c`: `blocked(const Map *m, const DoorList *dl, float x,
      float y)`
- [ ] `src/world.c`: `const PlayerState *p` в цикле дверей `world_step()`
- [ ] Прогнать `lint.sh strict`: если после правки cppcheck предложит
      `const` ещё где-то по той же цепочке — дотянуть, не меняя поведения

## Затрагиваемые файлы

- `src/enemy.c`, `src/enemy.h`
- `src/game.c`, `src/game.h`
- `src/player.c`
- `src/world.c`

## Критерий готовности

- [ ] `.claude/skills/lint/lint.sh strict` не выдаёт ни
      `constParameterPointer`, ни `constVariablePointer` ни для одного
      места в `src/`
- [ ] `.claude/skills/lint/lint.sh sim` проходит
- [ ] Сборка (`cmake --build build`) проходит без новых предупреждений
- [ ] `ctest --test-dir build --output-on-failure` проходит, включая
      `replay_test`, без правок тестов — поведение не меняется
- [ ] `clang-format --dry-run -Werror src/enemy.c src/enemy.h src/game.c
      src/game.h src/player.c src/world.c` проходит
- [ ] `driver.sh smoke` проходит

## Замечания

- **Цепочка `const`.** `pick_target()` передаёт `dl` и `p` в функции, чьи
  параметры станут `const`, поэтому и его собственные `dl` и `players`, а
  вслед за ним `dl` в `enemy_update_all()`, cppcheck пометит на втором
  проходе. Они входят в задачу, хотя в исходном отчёте их нет.
