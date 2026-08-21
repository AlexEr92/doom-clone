# 05-01 — Разделить `Player` на состояние и камеру

**Неделя:** 5 — Развязка симуляции и рендера
**Зависит от:** —
**Блокирует:** 05-02, 05-03, 06-02, 07-03
**Оценка:** ~100 строк изменений

## Контекст

`player.h:10-16` смешивает симуляцию и рендер:

```c
typedef struct { float x, y; float dirX, dirY; float planeX, planeY; float hp, armor; } Player;
```

`planeX/planeY` — артефакт рендера (FOV 0.66), к игровой логике отношения не
имеет. Плюс векторы направления нельзя корректно интерполировать между
снапшотами, а углы — тривиально. В пакете это 4 float вместо 1.

## Что сделать

- [ ] Ввести `PlayerState` в `player.h`:
  ```c
  typedef struct {
      uint8_t id;
      float x, y, angle;      /* angle в радианах */
      float hp, armor;
      int   alive;
      float respawn_timer;
  } PlayerState;
  ```
- [ ] Ввести `Camera { float dirX, dirY, planeX, planeY; }` и функцию
      `void player_camera(const PlayerState *p, Camera *c);`
      (`dir = (cos a, sin a)`, `plane = FOV_PLANE * (-sin a, cos a)`)
- [ ] `player_init()` — задавать `angle`, а не векторы (текущее
      `dirX=-1, dirY=0` → `angle = π`)
- [ ] `player_update()` (`player.c:32-69`) — вместо поворота векторов
      прибавлять к `angle` и нормализовать в `[-π, π]`; движение считать от
      `cos/sin`, стрейф — от перпендикуляра
- [ ] Поменять сигнатуры потребителей камеры на `const Camera *`:
      `raycast_render()`, `sprite_render()`
- [ ] `weapon.c`, `enemy.c`, `door.c`, `item.c`, `hud.c` — на `PlayerState`

## Затрагиваемые файлы

`player.h/.c`, `raycast.h/.c`, `sprite.h/.c`, `weapon.h/.c`, `enemy.h/.c`,
`door.h/.c`, `item.h/.c`, `hud.h/.c`, `game.h/.c`, `main.c`

## Критерий готовности

- [ ] Собирается без предупреждений (`-Wall -Wextra`)
- [ ] Игра визуально и на ощупь не изменилась: движение, стрейф, мышь,
      скорость поворота те же
- [ ] `PlayerState` не содержит ничего от рендера; `grep -n "plane" player.h`
      ничего не находит

## Замечания

Нормализация угла обязательна: без неё `angle` уплывёт за много минут игры
и потеряет точность float, а на неделе 8 сломает интерполяцию по кратчайшей
дуге.
