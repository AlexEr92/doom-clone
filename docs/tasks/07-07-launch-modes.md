# 07-07 — Режимы запуска и headless-сборка

**Этап:** 7 — Сеть: транспорт, протокол, снапшоты
**Зависит от:** 07-05
**Блокирует:** 09-04
**Оценка:** ~180 строк изменений: ~120 на режимы и ~60 на вынесенный разбор аргументов и его тест

## Контекст

`main.c` умеет одиночную игру и клиента (`--connect`, 07-05): клиент
разведён с одиночной игрой ветками под `#ifdef ENABLE_MULTIPLAYER` прямо в
цикле. Нужно три режима, отличающихся тем, что крутится в фикс-шаге.

## Что сделать

- [ ] Разбор аргументов: `parse_options()` в `main.c` знает `--connect` и
      `--port`, к ним добавить остальные:
  ```
  ./doom-clone                          # solo (как сейчас)
  ./doom-clone --host [--port N]        # listen-server + локальный клиент
  ./doom-clone --connect <ip> [--port N]# клиент
  ./doom-clone --host --dedicated       # headless-сервер
  ./doom-clone --name <nick>
  ./doom-clone --host --snaprate N      # снапшотов в секунду, 15–60 (N10)
  ```
      `--snaprate` передаётся в `server_set_snapshot_rate()`. `--name`
      доходит до `MsgJoin.name`: `client_connect()` имени не принимает, и
      клиент шлёт его пустым
- [ ] Вынести разбор аргументов из `main.c` в `src/options.c`/`options.h`
      (`Options` и `parse_options()`, без SDL и без других модулей клиента),
      потому что `main.c` не входит ни в один тест. Добавить `tests/test_options.c`
      (`doom_test()` в `tests/CMakeLists.txt`): каждый флаг, значение по
      умолчанию, `--port` вне 1–65535 и нечисловой, флаг без значения,
      неизвестный аргумент, `--snaprate` вне 15–60, взаимоисключающие
      сочетания (`--dedicated` без `--host`)
- [ ] Ввести абстракцию над источником мира, чтобы не плодить три цикла:
  ```c
  typedef struct {
      World *(*world)(void *ctx);
      void   (*tick)(void *ctx, const PlayerInput *local, double dt);
      int    (*local_player)(void *ctx);
      void  *ctx;
  } GameSession;
  ```
      Три реализации: solo (прямой `world_step()`), host (при старте
      `server_add_local_player()`, затем `server_tick(s, local, dt)`; на
      `--dedicated` локального игрока нет и `local` — `NULL`), client
      (`client_send_input()` + `client_poll()`). Клиентские ветки цикла
      `main.c` — `join()`, отправка ввода в тике, `client_poll()` с уходом в
      меню при разрыве — переезжают в эту реализацию
- [ ] `--dedicated`: не вызывать `engine_init()`, `assets_init()`,
      `audio_init()`; цикл на `SDL_Delay`/`get_time_seconds()` без рендера.
      Логи в stdout: подключения, отключения, тикрейт
- [ ] Собрать headless без SDL_video: SDL всё равно нужен для таймера, но
      `SDL_Init(SDL_INIT_TIMER)` вместо `SDL_INIT_VIDEO`
- [ ] Корректное завершение: `Ctrl+C` на сервере → рассылка
      `MSG_DISCONNECT` клиентам перед выходом
- [ ] Удалить ручной сервер `tests/net_serve.c` и его цель в
      `tests/CMakeLists.txt`: его заменяет `--host --dedicated`. В CLAUDE.md
      («Tests») и architecture.md («Сервер») вместо него назвать флаги

## Затрагиваемые файлы

`main.c`, `options.c`/`options.h` (новые), `tests/test_options.c` (новый),
`session.h` (новый), `engine.c`, `CMakeLists.txt`, `tests/net_serve.c`
(удаляется), `tests/CMakeLists.txt`

## Критерий готовности

- [ ] Все четыре режима запускаются и работают
- [ ] `--dedicated` работает на машине без дисплея
      (проверить: `unset DISPLAY` или через ssh без X-форвардинга)
- [ ] Solo-режим не создаёт ни одного сокета
      (`strace -f -e trace=socket ./doom-clone` — пусто)
- [ ] Неверные аргументы → внятное сообщение и `usage`
- [ ] `ctest -R options` проходит и проверяет каждый отказ из списка выше
      (код возврата и то, что `Options` не принимает неверное значение);
      `coverage.sh` держит пороги

## Замечания

Абстракция `GameSession` стоит того: без неё цикл в `main.c` обрастает
тремя ветками `if (mode == ...)` в каждом месте, и режимы начинают
расходиться в поведении.

Solo-режим сохраняется намеренно — это опорная точка при отладке: если
баг воспроизводится в solo, он не в сети.
