# 07-03 — `net_protocol.h` — структуры пакетов

**Неделя:** 7 — Сеть: транспорт, протокол, снапшоты
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
  typedef struct { uint8_t kind, actor, target;
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

`NetEvent.target` добавлен для kill feed (09-03): нужно знать и кто убил,
и кого.
