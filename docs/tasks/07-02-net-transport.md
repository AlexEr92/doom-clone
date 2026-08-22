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
