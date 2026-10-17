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
- [ ] Удалить ручной сервер `tests/manual/net_serve.c` и его цель
      (`doom_manual(net_serve …)` в `tests/CMakeLists.txt`): его заменяет
      `--host --dedicated`. Его строку `--stats` — раз в секунду число тиков,
      игроков, среднее и худшее время `server_tick()`, худшее опоздание тика
      (P4) — `--dedicated` печатает в своих логах. В CLAUDE.md («Tests»),
      architecture.md («Сервер») и `SKILL.md` скилла `run-doom-clone`
      («Network game») вместо `net_serve` назвать флаги

## Затрагиваемые файлы

`main.c`, `options.c`/`options.h` (новые), `tests/test_options.c` (новый),
`session.h` (новый), `engine.c`, `CMakeLists.txt`, `tests/manual/net_serve.c`
(удаляется), `tests/CMakeLists.txt`, `.claude/skills/run-doom-clone/SKILL.md`

## Критерий готовности

- [ ] Все четыре режима запускаются и работают
- [ ] `--dedicated` работает на машине без дисплея
      (проверить: `unset DISPLAY` или через ssh без X-форвардинга)
- [ ] `--host --dedicated` стартует из каталога, где из `assets/` есть
      только `maps/`: графику сервер не читает
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

## Закрыто

2026-10-18 — четыре режима запуска одного `doom-clone`: одиночная игра,
`--host` (listen-server с окном), `--host --dedicated` (сервер без окна) и
`--connect` (клиент). Командную строку разбирает `src/options.c/.h`, цикл
кадра в `main.c` один на три режима с окном, а разница между режимами
спрятана в `GameSession` (`src/session.h`, `src/session.c`).
`tests/manual/net_serve.c` удалён: сервер теперь — сама игра.

С «Что сделать» реализация расходится в следующем:

- **`GameSession` шире наброска.** Кроме `world`, `tick`, `local_player` и
  `ctx`, в нём есть `poll` (раз в кадр после тиков; возвращает
  `SESS_SAME` / `SESS_JOINED` / `SESS_LOST`, туда переехала реакция клиента
  на смену состояния соединения), `restart`, `close` и два свойства, которые
  цикл читает сам. `shared` значит, что мир идёт под меню и паузой: цикл
  тикает его с `local = NULL` и не замораживает анимации. `remote` значит,
  что события приходят с `poll()`, а не с тиком. Три реализации лежат в
  `session.c`, а не в `main.c`: так они не видят SDL и тестируются в
  `tests/test_session.c`. «Начать заново» у хоста ничего не делает: мир
  общий.
- **Правила флагов.** Флаг, которому в выбранном режиме нечего делать,
  отвергается, а не пропускается молча: `--dedicated` без `--host`,
  `--host` вместе с `--connect`, `--port` без `--host` и `--connect`,
  `--snaprate` без `--host`, `--name` без `--connect`. Сервер имён не хранит,
  поэтому имя хоста пока не нужно; разрешить `--name` с `--host` записано в
  09-03. При ошибке `Options` остаётся нетронутым, `-h` / `--help` печатает
  `usage` в stdout с кодом 0. Сборка без сети отвергает сетевые флаги.
- **`MSG_DISCONNECT` сервер раньше не слал вовсе.** Теперь
  `server_destroy()` шлёт его каждому вошедшему клиенту и отпускает того
  через `net_disconnect()`. Одного этого не хватало. `net_destroy()` звал
  `enet_peer_disconnect_now()` для всех пиров, а тот выбрасывает очередь.
  Даже с отправкой очереди перед отключением ENet на приёме роняет пакет,
  пришедший в одном приёме с отключением: клиент получал
  `NET_EV_DISCONNECT` вместо данных — так показал тест. Поэтому
  `net_destroy()` отправляет очередь и молча сбрасывает пиров в
  `ENET_PEER_STATE_DISCONNECT_LATER`. Затронуты `net_server.c/.h` и
  `net_transport.c/.h`, которых нет в «Затрагиваемых файлах».
- **`--name`.** `client_connect()` получил четвёртый параметр `name` (NULL —
  без имени, длиннее `NET_NAME_MAX - 1` — обрезается); `net_bot` входит как
  `bot`.
- **`engine.c` не менялся.** `--dedicated` не вызывает `engine_init()`, а
  `SDL_Init(SDL_INIT_TIMER)` стоит в `serve()` в `main.c`.
- **Логи `--dedicated`.** `serving …` при старте, `player N joined` /
  `player N left` на вход и выход игрока. Раз в секунду печатается строка
  `--stats` бывшего `net_serve` — тики, игроки, среднее и худшее время тика,
  худшее опоздание (P4) — без флага. В конце `stopped`.
- **lint.** `src/options.c` и `src/session.c` добавлены в `NOSDL=`
  (`lint.sh`). Проверка без SDL теперь идёт с `-DENABLE_MULTIPLAYER`: так
  читаются серверные ветки `session.c`.

Тесты: `tests/test_options.c` (22 случая) — каждый флаг, значения по
умолчанию, каждый отказ из списка задачи с кодом возврата, нетронутыми
`Options` и сообщением, называющим флаг. Он собирается с тем же
`ENABLE_MULTIPLAYER`, что и игра; в сборке без сети прогнана ветка
`#else`, 16 из 16 тестов. `tests/test_session.c` (10 случаев, порт 27120):
solo, host, dedicated, клиент против сессии хоста, имя в `MsgJoin` и
частота снапшотов из `--snaprate` на подставном. Добавлены случаи в
`test_net_transport.c` (очередь отпущенного пира доходит при
`net_destroy()`), `test_net_server.c` (`MSG_DISCONNECT` вошедшим, разрыв
невошедшему) и `test_net_client.c` (имя в `MsgJoin.name`, причина «the
server closed the game»). Каждый новый тест падал без своей правки.
Цикл `serve()` в `main.c` тестами не покрыт.

Критерии проверены на Linux:

- `--host --dedicated` под `env -u DISPLAY` запущен из каталога, где есть
  только `assets/maps/level1.txt`. По `strace` это единственный файл из
  `assets/`; X-сокета нет, есть один UDP-сокет и подключения к D-Bus,
  которые делает сам `SDL_Init()` (без D-Bus они молча не удаются). Против
  него `net_bot look` и `swarm`; `SIGINT` — оба бота отключены сразу,
  сервер напечатал `stopped`. Худшее опоздание тика — около 0,4 мс.
- Хост с окном на Xvfb `:100` и клиент `--name` на `:101`. Ходьба хоста
  сдвинула игрока 0, ввод клиента дошёл до сервера. Пауза и меню хоста мир
  не останавливают, и клиент в это время ходит. Возврат из меню не
  сбрасывает игрока хоста. Выход хоста — у клиента в stderr
  `Disconnected from 127.0.0.1: the server closed the game` и меню. Из меню
  клиент снова вошёл на `--dedicated`; `Ctrl+C` сервера дал ту же причину,
  выход клиента сервер записал как `player 0 left`.
- Одиночная игра под `strace -f -e trace=socket` на Xvfb: только `AF_UNIX`
  (X-сервер и D-Bus), ни одного `AF_INET`. Буквально «пусто» с дисплеем
  недостижимо, поэтому проверялось отсутствие `AF_INET`.
- Неверные аргументы печатают сообщение о флаге и `usage`, код 1.

Поправлены architecture.md (новый раздел «Режимы запуска», «Сервер»,
«Клиент»), CLAUDE.md («Tests», «Main loop»), `SKILL.md` скиллов
`run-doom-clone` («Network game»: сервер по pid, `driver.sh` принимает
сервер за игру) и `lint`, `tests/README.md`, `docs/harness.md`, а также
задачи chore-008, chore-009 (формат строки выхода игрока), chore-013,
chore-017 и 09-03.
