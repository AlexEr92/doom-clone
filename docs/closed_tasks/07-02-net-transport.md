# 07-02 — `net_transport` — обёртка над ENet

**Этап:** 7 — Сеть: транспорт, протокол, снапшоты
**Зависит от:** 07-01
**Блокирует:** 07-04, 07-05
**Оценка:** ~200 строк нового кода

## Контекст

Изолируем ENet за узким интерфейсом: серверная и клиентская логика не должны
знать про `ENetPeer` и `ENetPacket`. Это даёт возможность подменить транспорт
(или замокать его в тестах с искусственной задержкой — понадобится в 08-03).

## Что сделать

- [ ] `src/net_transport.h`:
  ```c
  typedef struct NetHost NetHost;          /* непрозрачный */
  typedef int NetPeerId;                   /* -1 = невалидный */

  typedef enum { NET_EV_NONE, NET_EV_CONNECT,
                 NET_EV_DISCONNECT, NET_EV_DATA } NetEventType;

  typedef struct {
      NetEventType type;
      NetPeerId    peer;
      const void  *data;
      size_t       size;
  } NetEvent_t;

  NetHost *net_host_server(uint16_t port, int max_peers);
  NetHost *net_host_client(void);
  NetPeerId net_connect(NetHost *h, const char *addr, uint16_t port);
  void net_disconnect(NetHost *h, NetPeerId peer);
  void net_destroy(NetHost *h);

  /* channel 0 = reliable (handshake, чат), 1 = unreliable (ввод, снапшоты) */
  void net_send(NetHost *h, NetPeerId peer, int channel,
                const void *data, size_t size, int reliable);
  void net_broadcast(NetHost *h, int channel,
                     const void *data, size_t size, int reliable);

  /* Неблокирующий опрос; возвращает 0, когда событий больше нет. */
  int  net_poll(NetHost *h, NetEvent_t *out);

  float net_peer_rtt(const NetHost *h, NetPeerId peer);
  float net_peer_loss(const NetHost *h, NetPeerId peer);
  ```
- [ ] `net_transport.c` — реализация поверх ENet: 2 канала,
      `enet_host_service(host, &ev, 0)` в `net_poll()`
- [ ] `enet_initialize()` / `enet_deinitialize()` с счётчиком ссылок
- [ ] Сопоставление `ENetPeer*` ↔ `NetPeerId` (массив на `MAX_PLAYERS`,
      `peer->data` хранит id)
- [ ] Таймаут пира — 5 с; настроить через `enet_peer_timeout()`

## Затрагиваемые файлы

`net_transport.c/.h` (новые), `CMakeLists.txt`

## Критерий готовности

- [ ] Тестовая утилита: сервер принимает подключение, эхо-ответ,
      клиент получает — по локалхосту
- [ ] Отключение клиента (kill процесса) → сервер получает
      `NET_EV_DISCONNECT` в пределах таймаута
- [ ] `net_poll()` не блокирует: с ним игровой цикл держит те же FPS
- [ ] Заголовки ENet не включаются нигде, кроме `net_transport.c`
      (`grep -rn "enet.h" src/` — одна строка)

## Замечания

Канал 1 (unreliable) для снапшотов и ввода — принципиально: надёжная
доставка устаревшего снапшота хуже, чем его потеря. Свежий снапшот всё
равно перекрывает старый.

`net_peer_rtt()`/`net_peer_loss()` нужны для оверлея (08-04) и лаг-компенсации
(08-05) — ENet считает их сам (`peer->roundTripTime`, `packetLoss`).

## Закрыто

2026-10-17 — ENet спрятан за `src/net_transport.h`: хосты, пиры по номеру,
пакеты как байты. Заголовки ENet включает только `net_transport.c`. С
«Что сделать» реализация расходится в трёх местах, все три согласованы с
пользователем:

- **Таймаут.** Один `enet_peer_timeout(peer, 0, 5000, 5000)` давал
  7.5–8.8 с от `kill -9` до DISCONNECT: ENet проверяет таймаут только в
  момент очередного ретрансмита, а интервал между ними каждый раз
  удваивается. Поэтому `net_poll()` сам отключает подключённого пира, от
  которого `NET_PEER_TIMEOUT_MS` (5000, `config.h`) не было ни одного
  подтверждения (`peer->lastReceiveTime`). Живой пир подтверждает пинги
  ENet не реже раза в 0.5 с. `enet_peer_timeout` с теми же 5 с остаётся:
  он завершает попытку подключения, на которую никто не отвечает.
- **Таблица пиров** — на `NET_MAX_PEERS` = `MAX_PLAYERS + 1` (`config.h`,
  строка в requirements.md, раздел 5), а не на `MAX_PLAYERS`. Чтобы 07-04
  отказал лишнему клиенту через `MSG_REJECT`, тот должен сначала
  подключиться на уровне транспорта.
- **Имя события** — `NetTransportEvent` и `NetTransportEventType`, а не
  `NetEvent_t`. `NetEvent` в 07-03 — событие игры в снапшоте, а суффикс
  `_t` противоречит конвенции об именах. Константы `NET_EV_*` — как в
  задаче.

Сверх задачи:

- каналы названы (`NET_CH_RELIABLE`, `NET_CH_UNRELIABLE`), невалидный
  номер пира — `NET_PEER_NONE`;
- `net_disconnect()` освобождает номер сразу и `NET_EV_DISCONNECT` по
  нему не присылает, а уже поставленное в очередь доставляет до разрыва —
  так `MSG_REJECT` доходит до отклонённого клиента;
- `net_destroy()` сообщает об уходе всем пирам, и сервер узнаёт о
  закрытом клиенте сразу, а не через 5 с;
- ненадёжный пакет больше датаграммы уходит ненадёжными фрагментами
  (`ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT`). Без этого флага ENet отправил
  бы его надёжно, а снапшот около 1400 Б (requirements.md, N9) уже
  фрагментируется.

Тестовая утилита из первого критерия — это два файла:

- `tests/test_net_transport.c` — 18 тестов в ctest по настоящим сокетам
  на 127.0.0.1. Таймауты проверяются сдвигом часов ENet
  (`enet_time_set()`), без ожидания;
- `tests/net_echo.c` — ручная проверка двумя процессами
  (`build/tests/net_echo server|client`). В ctest не входит, потому что
  не завершается сама.

Проверено на Linux:

- эхо между процессами доходит: надёжное — за 32 мс (два периода опроса
  по 60 Гц), ненадёжных возвращается около 60 в секунду;
- после `kill -9` клиента сервер получает `NET_EV_DISCONNECT` через
  4.86 с, после `kill -9` сервера клиент — через 4.87 с (R1, R2: ≤ 5 с);
- цикл `net_echo` с `net_poll()` держит 61–64 Гц, самый долгий опрос —
  90–210 мкс. Игра с временно вставленными в цикл `net_poll()` и
  `net_send()` на каждый кадр показала 64/62 FPS, как и без них. Сама
  игра транспорт пока не вызывает — это делают 07-04 и 07-05;
- буквальная команда критерия `grep -rn "enet.h" src/` печатает 7 строк,
  все из `net_transport.c`: точка в шаблоне совпадает ещё и с вызовами
  `enet_host_*`. Включений ENet два: `enet/enet.h` и `enet/time.h`, второе
  ради `ENET_TIME_DIFFERENCE`, которого `enet.h` не включает;
- сборка с `-DENABLE_MULTIPLAYER=OFF` даёт 12 тестов и ни одной сетевой
  цели.

ENet считает RTT по подтверждениям, и в него входит период опроса дальней
стороны: при опросе раз в тик `net_peer_rtt()` на loopback показывает
16–17 мс, а не около нуля. Это записано в 08-04, где оверлей показывает
пинг.

Попутно поправлены CLAUDE.md (как тестируется транспорт),
requirements.md, раздел 5 (строка о соединениях на хосте), 07-04 (как
создать сервер и отказать лишнему клиенту) и 08-04 (что входит в RTT).
