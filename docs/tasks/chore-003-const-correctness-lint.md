# chore-003 — Три параметра/переменных без `const`, хотя только читаются

**Откуда:** найдено `.claude/skills/lint/lint.sh strict`
(`constParameterPointer` / `constVariablePointer`)
**Зависит от:** —
**Блокирует:** —
**Оценка:** ~5 строк изменений (три сигнатуры/объявления)

## Контекст

`.claude/skills/lint/lint.sh strict` находит семь находок уровня
`const*Pointer`. Три из них — реальная и безопасная правка, проверено по
коду: параметр или локальный указатель нигде в функции не пишет через
себя.

1. **`src/game.c:155`** — `game_handle_event(Game *g, InputState *in,
   SDL_Event *ev)`. Тело функции (`src/game.c:155-220`) читает только
   `ev->type` и `ev->key.keysym.sym`, ни разу не присваивает через `ev`.

2. **`src/item.c:79`** — в `item_update()`:

   ```c
   Sprite *sp = &sl->items[it->sprite_id];
   ```

   Дальше `sp` только читается (`sp->active`, `sp->x`, `sp->y`,
   `src/item.c:80-86`); запись в спрайт происходит отдельно, в
   `apply_pickup()` (`src/item.c:60`), через `sl`, а не через `sp`.

3. **`src/map.c:83`** — `map_is_wall_door(const Map *m, struct DoorList
   *dl, float x, float y)`. `dl` используется только как булев признак
   (`if (c == 2 && dl)`) и передаётся дальше в `door_is_blocking()`,
   которая уже принимает `const DoorList *` (`src/door.c:47`,
   `src/door.h:45`). Ставить `const` в `map_is_wall_door()` безопасно и
   для объявления в `src/map.h:30` — вызывающие (`src/enemy.c:95,119,122`,
   `src/player.c:21`) передают обычный `DoorList *`, что в `const`-параметр
   идёт без приведений.

## Что сделать

- [ ] `src/game.c:155` и объявление в `src/game.h` —
      `game_handle_event(Game *g, InputState *in, const SDL_Event *ev)`
- [ ] `src/item.c:79` — `const Sprite *sp = &sl->items[it->sprite_id];`
- [ ] `src/map.c:83` и объявление в `src/map.h:30` —
      `map_is_wall_door(const Map *m, const struct DoorList *dl, float x,
      float y)`

## Затрагиваемые файлы

- `src/game.c`, `src/game.h`
- `src/item.c`
- `src/map.c`, `src/map.h`

## Критерий готовности

- [ ] `.claude/skills/lint/lint.sh strict` больше не находит
      `constParameterPointer`/`constVariablePointer` для `ev` в
      `game_handle_event`, `sp` в `item_update`, `dl` в `map_is_wall_door`
- [ ] Сборка (`cmake --build build`) проходит без новых предупреждений
- [ ] `clang-format --dry-run -Werror src/game.c src/game.h src/item.c
      src/map.c src/map.h` проходит
- [ ] `driver.sh smoke` проходит — правка не меняет поведение, только
      сигнатуры

## Замечания

В объём этой задачи **намеренно не входят** ещё две находки того же
прогона, которые cppcheck предлагает так же — не переносить их сюда:

- **`perpDist` и `Enemy *e` в `weapon.c` (`src/weapon.c:130,153`)** — это
  код экранного хитскана (`weapon_try_fire()`), который задача
  `05-04-hitscan-rewrite.md` выкидывает целиком при переходе на мировой
  рейкаст. `perpDist` там и так мёртв (`unassignedVariable`, гасится
  через `(void)perpDist`) — чинить или даже просто расставлять `const` в
  коде, который скоро удалят — работа на выброс.
- **`const Framebuffer *fb` в `sprite_render()` (`src/sprite.c:82`)** —
  формально безопасно (в C `const Framebuffer *` замораживает только сам
  указатель `fb`, а не поле `fb->pixels`, так что запись
  `fb->pixels[i] = color` осталась бы легальной), но семантически вредно:
  единственная задача `sprite_render()` — писать в `fb->pixels`, и
  сигнатура с `const` заявляла бы обратное тому, что функция на самом
  деле делает. Опираться на то, что `const` в C сквозной только на один
  уровень указателей, а не читать его как «функция не трогает
  framebuffer», — ловушка для следующего читателя сигнатуры.

Если кто-то захочет донести константность и на эти два места —
разбирать нужно отдельно и после того, как `05-04` и связанные задачи
уберут экранный хитскан; сама по себе находка cppcheck сюда не
относится.
