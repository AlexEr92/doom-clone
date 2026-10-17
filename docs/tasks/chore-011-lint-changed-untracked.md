# chore-011 — `lint.sh changed` не видит неотслеживаемые `src/*.c`

**Откуда:** замечено при ревью 07-02 (`net_transport`): `lint.sh changed`
напечатал «нет изменённых .c в src/», хотя вся правка была новым
неотслеживаемым `src/net_transport.c`
**Зависит от:** —
**Блокирует:** —
**Оценка:** ~5–10 строк в `lint.sh` плюс строка в `SKILL.md`: нужна одна
дополнительная команда в подстановке `mapfile` (ветка `changed`),
образец уже есть в `coverage.sh`

## Контекст

Ветка `changed` в `.claude/skills/lint/lint.sh` собирает список из
`git diff --name-only HEAD -- 'src/*.c'` и
`git diff --cached --name-only -- 'src/*.c'`. Ни одна из команд не
перечисляет файл, который ни разу не делали `git add`, поэтому новый
`src/*.c` в этом режиме невидим: cppcheck его не запускается, а скрипт
печатает «нет изменённых .c в src/» и сразу переходит к `check_sim`.

`lint.sh all` (его использует `check.sh`) берёт `src/*.c` целиком и файл
видит. Страдает агент `reviewer`: он вызывает именно `lint.sh changed`
(`.claude/agents/reviewer.md`, «the same scope `lint.sh changed` covers») и
при ревью новой единицы трансляции получает пустой, но «чистый» результат.

`.claude/skills/coverage/coverage.sh` (функция `changed_lines`) эту ситуацию
уже учитывает: добавляет `git ls-files --others --exclude-standard --
'src/*.c' 'src/*.h'`.

## Что сделать

- [ ] В ветке `changed` в `lint.sh` добавить к списку файлов вывод
      `git ls-files --others --exclude-standard -- 'src/*.c'`
- [ ] Обновить описание режима: шапка `lint.sh` («only files that differ
      from HEAD») и `.claude/skills/lint/SKILL.md:36` — упомянуть новые
      неотслеживаемые файлы
- [ ] Сверить текст про охват в `.claude/agents/reviewer.md` (строки про
      «the same scope `lint.sh changed` covers» и про диапазон ревизий) и
      поправить, если он теперь неточен

## Затрагиваемые файлы

- `.claude/skills/lint/lint.sh`
- `.claude/skills/lint/SKILL.md`
- `.claude/agents/reviewer.md` (только если формулировка охвата устарела)
- `docs/harness.md` — по правилу из `CLAUDE.md` его обновляют при любой
  правке под `.claude/`; если описание `lint.sh changed` там есть

## Критерий готовности

- [ ] Файл `src/zz_probe.c` с заведомым предупреждением уровня gate
      (например, `int f(void){ int a[2]; a[2]=0; return 0; }`), созданный и
      не добавленный в индекс, попадает в вывод
      `lint.sh changed` («проверяю: … src/zz_probe.c»), а скрипт завершается
      с ненулевым кодом; после удаления файла вывод снова «нет изменённых .c
      в src/», код 0
- [ ] Отслеживаемый и изменённый `src/*.c` по-прежнему попадает в список ровно
      один раз (дубликатов нет)
- [ ] `lint.sh all` и `lint.sh sim` ведут себя как раньше
- [ ] Шапка скрипта и `SKILL.md` описывают режим одинаково

## Замечания

Заголовки (`src/*.h`) cppcheck в этом режиме и так не получает отдельно, а
только через включения; расширять список на `.h` не нужно.

Файл-проба в критерии — временный, его удаляют после проверки; в
коммит он не попадает.
