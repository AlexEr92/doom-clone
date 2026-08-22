# 05-05 — Убрать `SCREEN_W/H` из игровой логики

**Этап:** 5 — Развязка симуляции и рендера
**Зависит от:** 05-04
**Блокирует:** 06-01
**Оценка:** ~40 строк изменений

## Контекст

Финальная проверка этапа 5: симуляция не должна знать ничего про экран.
Это условие того, что `world_step()` (задача 06-01) соберётся в headless-сервер.

## Что сделать

- [ ] Пройти `grep -rn "SCREEN_W\|SCREEN_H\|zBuffer\|Framebuffer\|SDL_" src/`
      и убедиться, что они остались только в: `engine.*`, `raycast.c`,
      `sprite.c`, `hud.c`, `assets.*`, `game.c` (оверлеи), `input.c`, `main.c`
- [ ] В `enemy.c`, `weapon.c`, `item.c`, `door.c`, `player.c`,
      `map.c`, `raycast_world.c` — ни одного вхождения
- [ ] `audio.h` включает `<SDL.h>`/`<SDL_mixer.h>`: развязать логику от
      аудио — вместо прямых вызовов `audio_play()` из `enemy.c:139`,
      `enemy.c:173`, `item.c:51`, `weapon.c:65` завести очередь событий:
  ```c
  typedef enum { EV_SHOT, EV_ENEMY_HURT, EV_ENEMY_DEATH,
                 EV_PLAYER_HURT, EV_PICKUP, EV_DOOR, EV_NO_AMMO } GameEventKind;
  typedef struct { uint8_t kind; uint8_t actor; float x, y; } GameEvent;

  #define MAX_EVENTS 32
  typedef struct { GameEvent items[MAX_EVENTS]; int count; } EventQueue;
  ```
      Логика пишет события в очередь, клиент после тика их проигрывает.
- [ ] Убрать параметр `Audio *au` из `enemy_update_all()`, `enemy_damage()`,
      `item_update()`, `weapon_try_fire()`

## Затрагиваемые файлы

`event.h` (новый), `enemy.c/.h`, `weapon.c/.h`, `item.c/.h`, `door.c/.h`,
`main.c`

## Критерий готовности

- [ ] Модули симуляции не включают `SDL.h` ни прямо, ни транзитивно
- [ ] Все звуки в одиночной игре звучат как раньше и в тех же местах
- [ ] Пробная компиляция симуляции отдельно от SDL:
      `gcc -fsyntax-only -Isrc src/enemy.c src/weapon.c src/item.c src/door.c`
      проходит без заголовков SDL в include-путях

## Замечания

Очередь событий — не «на будущее», а именно то, что на этапе 7 поедет в
снапшоте (`NetEvent`, см. 07-03) и позволит клиенту играть звук с правильной
дистанцией до **своего** игрока. Формат `GameEvent` стоит сразу делать
компактным и сериализуемым.
