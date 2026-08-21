# 07-07 — Режимы запуска и headless-сборка

**Этап:** 7 — Сеть: транспорт, протокол, снапшоты
**Зависит от:** 07-05
**Блокирует:** 09-04
**Оценка:** ~120 строк изменений

## Контекст

`main.c` сейчас жёстко однопользовательский: `engine_init()` → `world_load()`
→ цикл. Нужно три режима, отличающихся тем, что крутится в фикс-шаге.

## Что сделать

- [ ] Разбор аргументов (`main.c:73` — сейчас `argc/argv` игнорируются):
  ```
  ./doom-clone                          # solo (как сейчас)
  ./doom-clone --host [--port N]        # listen-server + локальный клиент
  ./doom-clone --connect <ip> [--port N]# клиент
  ./doom-clone --host --dedicated       # headless-сервер
  ./doom-clone --name <nick>
  ```
- [ ] Ввести абстракцию над источником мира, чтобы не плодить три цикла:
  ```c
  typedef struct {
      World *(*world)(void *ctx);
      void   (*tick)(void *ctx, const PlayerInput *local, double dt);
      int    (*local_player)(void *ctx);
      void  *ctx;
  } GameSession;
  ```
      Три реализации: solo (прямой `world_step()`), host (`server_tick()`),
      client (`client_send_input()` + `client_poll()`)
- [ ] `--dedicated`: не вызывать `engine_init()`, `assets_init()`,
      `audio_init()`; цикл на `SDL_Delay`/`get_time_seconds()` без рендера.
      Логи в stdout: подключения, отключения, тикрейт
- [ ] Собрать headless без SDL_video: SDL всё равно нужен для таймера, но
      `SDL_Init(SDL_INIT_TIMER)` вместо `SDL_INIT_VIDEO`
- [ ] Корректное завершение: `Ctrl+C` на сервере → рассылка
      `MSG_DISCONNECT` клиентам перед выходом

## Затрагиваемые файлы

`main.c`, `session.h` (новый), `engine.c`, `CMakeLists.txt`

## Критерий готовности

- [ ] Все четыре режима запускаются и работают
- [ ] `--dedicated` работает на машине без дисплея
      (проверить: `unset DISPLAY` или через ssh без X-форвардинга)
- [ ] Solo-режим не создаёт ни одного сокета
      (`strace -f -e trace=socket ./doom-clone` — пусто)
- [ ] Неверные аргументы → внятное сообщение и `usage`

## Замечания

Абстракция `GameSession` стоит того: без неё цикл в `main.c` обрастает
тремя ветками `if (mode == ...)` в каждом месте, и режимы начинают
расходиться в поведении.

Solo-режим сохраняется намеренно — это опорная точка при отладке: если
баг воспроизводится в solo, он не в сети.
