# chore-013 — `lint.sh sim` не проверяет, что `net_client.c` и `net_interp.c` собираются без SDL

**Откуда:** предложено при ревью 07-05 (сетевой клиент); `net_interp.c` добавлен после ревью 08-01
**Зависит от:** —
**Блокирует:** —
**Оценка:** ~5 строк в `lint.sh` (два имени в массиве и поправка комментария над ним) плюс правки в трёх описаниях гейта
**Приоритет:** низкий: сегодня `net_client.c` включает только `net_client.h`, `net_protocol.h`, `net_transport.h`, `world.h`, `config.h` и стандартную библиотеку, а `net_interp.c` — `net_interp.h`, `world.h`, `utils.h`, `config.h`, `net_protocol.h` и стандартную библиотеку; проверка нужна как защита от регрессии

## Контекст

`src/net_client.c` (соединение, приём снапшотов, отправка ввода) и
`src/net_interp.c` (кольцо снапшотов `SnapshotBuffer` и `interp_view()`,
линкуется `net_client.c`) собираются без SDL и не должны получить её по
ошибке: клиентский сетевой слой не
рисует и не играет звук, а `tests/CMakeLists.txt` (`doom_test(net_client …)`)
линкует его с сервером в одном процессе, без клиентских модулей. Ни в `SIM=`, ни в `NOSDL=` этих файлов нет. Случайный
`#include "audio.h"` или `<SDL2/SDL.h>` не заметит и тест: пути заголовков
SDL в `tests/CMakeLists.txt` даны каждому тесту. Не заметит и ни одна
другая проверка.

Массив `NOSDL=` в `.claude/skills/lint/lint.sh` сегодня перечисляет
`src/net_protocol.c`, `src/net_server.c`, `src/options.c` и
`src/session.c`, а комментарий над ним обосновывает список так: «linked
into the dedicated server». Выделенный сервер — тот же `doom-clone`, но
`net_client.c` и `net_interp.c` в режиме `--dedicated` не работают, поэтому комментарий
придётся переформулировать:
в списке лежат сетевые файлы вне симуляции, которым нельзя видеть SDL, —
сервером они линкуются или нет.

## Что сделать

- [ ] Добавить `src/net_client.c` и `src/net_interp.c` в `NOSDL=` в `lint.sh` и переписать
      комментарий над массивом так, чтобы он не утверждал, что все файлы
      списка линкует сервер
- [ ] Поправить описания гейта, которые называют состав `NOSDL=` или
      причину его существования: `.claude/skills/lint/SKILL.md` (пункт «no
      SDL where the server needs none»), `docs/harness.md` (строка `lint`
      в таблице скиллов и абзац про хук `post-edit.sh`: «серверный файл из
      `NOSDL=`»), сообщение об ошибке в `.claude/hooks/post-edit.sh`, если
      оно называет файлы или слово «серверный», и `reviewer.md`, если там
      сказано, куда класть серверный файл вне симуляции

## Затрагиваемые файлы

- `.claude/skills/lint/lint.sh`
- `.claude/skills/lint/SKILL.md`
- `docs/harness.md`
- `.claude/hooks/post-edit.sh`, `.claude/agents/reviewer.md` — только если там есть формулировка про серверные файлы

## Критерий готовности

- [ ] `.claude/skills/lint/lint.sh sim` проходит на чистом дереве
- [ ] Временная строка `#include <SDL2/SDL.h>` в начале `src/net_client.c`,
      а затем в `src/net_interp.c`, каждый раз заставляет `lint.sh sim`
      завершиться с ненулевым кодом и назвать этот файл; после отката
      проверка снова зелёная
- [ ] `grep -n "net_client\|net_interp" .claude/skills/lint/lint.sh` находит
      оба файла в `NOSDL=`; `SIM=` не изменился
- [ ] `grep -n "dedicated server" .claude/skills/lint/lint.sh` не находит
      комментария, который называет сервер единственной причиной членства в
      списке
- [ ] `.claude/skills/check/check.sh` проходит

## Замечания

- Отклонено: добавить `net_client.c` в `SIM=`. Это стерло бы границу
  симуляции, на которой стоят `CLAUDE.md` и `docs/architecture.md`
  (см. chore-012).
- Файл, добавленный в массив, должен собираться при любой сборке: проверка
  только читает исходник, `ENABLE_MULTIPLAYER` ей не нужен.
- Если когда-нибудь `net_client.c` или `net_interp.c` потребует SDL-заголовок (например, для
  таймера), его придётся убрать из списка, а не обходить проверку.
