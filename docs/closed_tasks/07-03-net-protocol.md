# 07-03 — `net_protocol.h` — структуры пакетов

**Этап:** 7 — Сеть: транспорт, протокол, снапшоты
**Зависит от:** 06-02
**Блокирует:** 07-04, 07-05
**Оценка:** ~150 строк нового кода

## Контекст

Мир целиком маленький: 10 игроков + 32 врага + 64 предмета + 64 двери.
Полный снапшот — ~990 Б в co-op и ~390 Б в deathmatch (врагов нет),
влезает в MTU без фрагментации. При 20 Гц это до 20 КБ/с на клиента.

Дельта-компрессия и битпакинг не нужны — они добавляют главный источник
трудноуловимых багов в netcode ради экономии, которая здесь не требуется.

## Что сделать

- [ ] `src/net_protocol.h`:
  ```c
  #define PROTO_VERSION 1
  #define NET_PORT_DEFAULT 27015

  typedef enum {
      MSG_JOIN = 1, MSG_ACCEPT, MSG_REJECT,
      MSG_INPUT, MSG_SNAPSHOT, MSG_DISCONNECT
  } MsgType;

  typedef struct { uint8_t type; uint8_t _pad[3]; } MsgHeader;

  /* --- reliable, канал 0 --- */
  typedef struct { MsgHeader h; uint16_t proto; char name[16]; } MsgJoin;
  typedef struct { MsgHeader h; uint8_t your_id; uint32_t seed;
                   char map[32]; } MsgAccept;
  typedef struct { MsgHeader h; uint8_t reason; } MsgReject;

  /* --- unreliable, канал 1 --- */
  typedef struct { MsgHeader h; uint32_t tick;
                   uint16_t buttons; float angle; } MsgInput;

  typedef struct { uint8_t id, flags, weapon, alive;
                   float x, y, angle; uint8_t hp, armor;
                   uint16_t ammo, frags, deaths; } NetPlayer;  /* 24 Б */
  typedef struct { uint8_t type, state; uint8_t hp;
                   float x, y; } NetEnemy;                     /* 12 Б */
  typedef struct { uint8_t kind, actor, arg;
                   float x, y; } NetEvent;                     /* 12 Б */

  typedef struct {
      MsgHeader h;
      uint32_t  tick, last_input_tick;
      uint8_t   player_count, enemy_count, event_count, _pad;
      NetPlayer players[MAX_PLAYERS];
      NetEnemy  enemies[MAX_ENEMIES];
      uint64_t  items_alive;      /* битовая маска, MAX_ITEMS=64 */
      uint8_t   doors[MAX_DOORS]; /* openness × 255 */
      NetEvent  events[MAX_EVENTS];
  } MsgSnapshot;
  ```
- [ ] Функции упаковки/распаковки:
      `snapshot_write(MsgSnapshot*, const World*, uint32_t last_input_tick)`,
      `snapshot_apply(World*, const MsgSnapshot*)`
- [ ] Порядок байт: все целевые платформы little-endian (x86, ARM64) —
      конвертацию не делаем, но зафиксировать это в комментарии и
      проверять `PROTO_VERSION` при handshake
- [ ] `static_assert` на размеры структур, чтобы не разъехались молча
- [ ] Обрезать снапшот по фактическому `enemy_count`/`event_count` при
      отправке (не слать хвост пустых слотов)

## Затрагиваемые файлы

`net_protocol.h/.c` (новые)

## Критерий готовности

- [ ] `sizeof(MsgSnapshot)` ≤ 1400 Б — умещается в MTU 1500 с запасом на
      заголовки IP/UDP/ENet. Расчёт: 16 + 10×24 + 32×12 + 8 + 64 + 32×12
      = 1096 Б
- [ ] `snapshot_write()` → `snapshot_apply()` в чистый `World`
      восстанавливает позиции игроков и врагов, состояние дверей и
      маску предметов
- [ ] `static_assert` ловит изменение размера структур
- [ ] Снапшот в DM (0 врагов) с 10 игроками и 5 событиями — около 390 Б

## Замечания

Квантование: `hp`/`armor` в `uint8_t` (0–100 и так влезает), openness двери
в байт. Позиции оставляем `float` — квантовать координаты имеет смысл только
при дельта-сжатии, а от него мы отказались.

`items_alive` как `uint64_t` жёстко привязана к `MAX_ITEMS 64` —
добавить `static_assert(MAX_ITEMS <= 64)`.

Запас до MTU при 10 игроках небольшой: ~400 Б. Если `MAX_ENEMIES`
или `MAX_EVENTS` когда-нибудь вырастут, снапшот пробьёт 1500 Б
и ENet начнёт его фрагментировать — а фрагментированный unreliable-пакет
теряется целиком при потере любого фрагмента. `static_assert` на размер
здесь не педантизм, а защита от тихой деградации.

`NetEvent.arg` — копия `GameEvent.arg`, третьего байта события, чей смысл
зависит от `kind`: тип предмета у `EV_PICKUP` (10-12), направление у
`EV_DOOR` (bug-015), жертва у `EV_PLAYER_DEATH` (09-02) — kill feed
(09-03) нужно знать и кто убил, и кого.

## Закрыто

2026-10-17 — формат сетевых сообщений — в `src/net_protocol.h`: `MsgJoin`,
`MsgAccept`, `MsgReject`, `MsgInput`, `MsgSnapshot` и записи `NetPlayer`,
`NetEnemy`, `NetEvent` с раскладкой из «Что сделать». Мир переводят в
снапшот и обратно `snapshot_write()`/`snapshot_apply()` в
`src/net_protocol.c`. Размер каждой структуры закреплён `_Static_assert`;
проверено подменой поля или лимита в копии `src/`: компиляцию валят
`uint32_t ammo` в `NetPlayer`, `uint16_t hp` в `NetEnemy`, новое поле в
голове снапшота, `uint16_t your_id`, `MAX_ENEMIES 64` (снапшот вырастает
до 1480 Б при пределе 1400) и `PROTO_VERSION 257`.

С «Что сделать» реализация расходится в следующем:

- **Обрезка по счётчикам** сделана отдельной парой функций,
  `snapshot_encode()`/`snapshot_decode()`. Враги лежат в середине
  структуры, и отрезать от неё префикс нельзя, поэтому на проводе поля
  идут подряд, а `players[]`, `enemies[]` и `events[]` — каждый своей
  длины. `snapshot_decode()` отбрасывает пакет короче головы, пакет
  чужого типа, счётчик сверх лимита, размер, не совпадающий со
  счётчиками, и id игрока, оружие, тип или состояние врага и вид события
  вне диапазона (requirements.md, раздел 7). Позиции не проверяются:
  сервер для клиента доверенный.
- **`NET_PORT_DEFAULT`** уже был в `config.h` и остался там, с прочими
  сетевыми константами. Туда же добавлен `SNAPSHOT_MAX_BYTES` (1400, N9).
  `PROTO_VERSION` — в `net_protocol.h`, рядом с раскладкой, которую он
  версионирует. `static_assert(MAX_ITEMS <= 64)` из «Замечаний» уже
  стоял в `config.h`.
- **Паддинг** записан явными полями `_pad` (`MsgAccept`, `MsgInput`,
  `NetEnemy`, `NetEvent`). Раскладка та же, но в сеть уходят нули, а не
  содержимое стека.
- **Сверх задачи:** `RejectReason` с двумя значениями, которые называют
  критерии 07-04: сервер полон, чужая версия. Ещё один `_Static_assert`
  требует, чтобы два байта `PROTO_VERSION` различались: только тогда
  собеседник с другим порядком байт читает чужую версию и получает отказ
  (requirements.md, раздел 1).

Что снапшот несёт и как:

- **Игроки:** только занятые слоты, каждый со своим `id`. Кого в снапшоте
  нет, `snapshot_apply()` выводит из мира, а `local_player` не трогает.
  Игрок в слоте, который у клиента был пуст, начинает с `player_init()`.
- **hp и броня** — целыми с округлением вверх: у живого hp не превращается
  в 0, а `main.c` считает `hp <= 0` смертью. Патроны — только у оружия в
  руках.
- **Двери:** снапшот несёт только `openness`, состояние выводится из неё.
  0 — `DOOR_CLOSED`, 255 — `DOOR_OPEN`, между ними — по направлению
  изменения.
- **Нули до появления данных.** `NetEvent.arg`, `NetPlayer.flags`,
  `frags` и `deaths` пишутся нулями: в `GameEvent` ещё нет `arg`
  (bug-015, 10-12, 09-02), в `PlayerState` — фрагов и смертей (09-03),
  битов `flags` не определено (09-02).

Тест — `tests/test_net_protocol.c`, 28 случаев. Круговой путь
`snapshot_write()` → `snapshot_encode()` → `snapshot_decode()` →
`snapshot_apply()` идёт в свежезагруженный `World` той же карты. Снапшот
DM с 10 игроками, без врагов и с 5 событиями — 388 Б, полный — 1096 Б.
Модуль и тест собираются только при `ENABLE_MULTIPLAYER`; сборка с
`-DENABLE_MULTIPLAYER=OFF` проходит.

Попутно найден пробел в спецификации: `World.events` держит события
одного тика, а снапшот уходит раз в три. В 07-04 добавлено накопление
событий между снапшотами, в 07-06 — отбор над накопленным списком.
Поправлены architecture.md (событие в снапшоте) и задачи, которые
описывали протокол иначе: 07-04 и 07-05 (кодирование снапшота), 09-03
(фраги в `NetPlayer` уже есть), bug-015, 10-12 и 09-02 (`arg` надо
переносить в `NetEvent`).
