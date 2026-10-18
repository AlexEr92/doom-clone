# Задачи

Открытые задачи лежат в этой папке, закрытые — в
[../closed_tasks/](../closed_tasks/). `ls docs/tasks/` отвечает на вопрос
«что осталось сделать».

Целевая конфигурация мультиплеера: **до 10 игроков**, основной режим —
**deathmatch**, co-op вторым. Разбивка сделана из
[roadmap.md](../roadmap.md); требования к
производительности, задержке, лимитам и тестированию — в
[requirements.md](../requirements.md), критерии приёмки в задачах их не
дублируют и не отменяют.

## Два семейства задач

| Имя | Откуда | Когда брать |
|---|---|---|
| `05-01-slug.md` | из дорожной карты | по графу зависимостей |
| `bug-001-slug.md` | найдено попутно | в любой момент |

Шаблон, критерии приёмки и жизненный цикл одинаковые. Префикс говорит
только о происхождении: плановая задача связана зависимостями, `bug-*`
не связана ничем и берётся когда удобно.

Порядок плановых задач важен: этапы 5–6 не трогают сеть вообще и
проверяемы в одиночной игре, а всё остальное ждёт, пока симуляция не
перестанет зависеть от нарисованного кадра.

## Как закрыть задачу

Закрытие — часть выполнения задачи, а не отдельная работа: пока файл лежит
здесь, индекс утверждает, что задача открыта.

Закрытию предшествует ревью агентом `reviewer`: находки, которые ломают
критерий готовности, архитектурный инвариант или конвенцию, исправляются
до `git mv`. Скилл `task` (`/task 07-01`) проводит задачу через весь цикл —
реализацию, ревью, закрытие и коммит.

1. `git mv docs/tasks/X.md docs/closed_tasks/X.md`
2. дописать в конец файла секцию «Закрыто»: дату и что именно изменилось
3. перенести строку из таблицы ниже в [«Закрытые»](#закрытые)
4. поправить документацию, если изменение ей противоречит

Четвёртый шаг — не ритуал: большинство задач ничему не противоречат, и
тогда делать нечего. Но [architecture.md](../architecture.md) описывает
игру такой, какая она сейчас, и этапы 5–6 отменяют написанное там прямо —
например, «хитскан считает попадания через глобальный Z-буфер» перестало
быть правдой после 05-04. Незамеченное расхождение здесь хуже отсутствия
документа: его читает каждый, кто приходит в проект.

Секция «Закрыто» описывает изменение, а **не ссылается на хеш коммита** —
хеши не переживают rebase и squash-merge:

```markdown
## Закрыто

2026-08-21 — формат текстуры в `engine_init()` приведён к упаковке
`make_color()`: `SDL_PIXELFORMAT_ABGR8888`.
```

Отказ фиксируется так же и ради этого папка и нужна — в git видно только,
что ничего не изменилось, а причина не видна нигде:

```markdown
## Закрыто

2026-08-21 — решено не чинить: поведение намеренное, см. «Ключевые
решения» в ../roadmap.md.
```

## Этап 8 — Качество netcode

| # | Задача | Зависит от |
|---|---|---|
| [08-01](08-01-snapshot-interpolation.md) | Буфер снапшотов и интерполяция | 07-05 |
| [08-02](08-02-client-prediction.md) | Предсказание своего игрока | 07-05 |
| [08-03](08-03-reconciliation.md) | Реконсиляция и сглаживание | 08-02 |
| [08-04](08-04-net-debug-overlay.md) | Отладочный оверлей (F3) | 07-05 |
| [08-05](08-05-lag-compensation.md) | Лаг-компенсация хитскана | 08-03 |

## Этап 9 — Deathmatch, геймплей и меню

| # | Задача | Зависит от |
|---|---|---|
| [09-01](09-01-spawn-points.md) | Пул точек спавна | 06-02 |
| [09-07](09-07-deathmatch-map.md) | Карта 48×48 для DM | 09-01 |
| [09-02](09-02-respawn.md) | Респавн и защита от спавн-килла | 09-01 |
| [09-03](09-03-frags-scoreboard.md) | Фраги, скорборд, kill feed | 09-02 |
| [09-04](09-04-menu-host-join.md) | Меню HOST/JOIN + ввод IP | 07-07 |
| [09-05](09-05-match-rules.md) | Правила матча и баланс DM | 09-03, 08-05, 09-07 |
| [09-06](09-06-docs.md) | Обновить README | 09-04 |

## Этап 10 — Графика и арсенал

| # | Задача | Зависит от |
|---|---|---|
| [10-06](10-06-world-weapons-pickups.md) | Оружие на земле и маркеры карты | 10-05, 09-07 |
| [10-07](10-07-grenade-launcher.md) | Гранатомёт: снаряд, полёт, взрыв | 10-05, 07-03 |
| [10-09](10-09-crosshair.md) | Прицел в центре экрана | 10-01 |
| [10-11](10-11-sound-files.md) | Звуки из файлов `assets/sounds/` | bug-013 |
| [10-10](10-10-weapon-fire-sounds.md) | Звуки выстрела автомата, винтовки и гранатомёта | 10-05, 10-11 |
| [10-12](10-12-pickup-sounds-per-type.md) | Свой звук подбора для аптечки, патронов и брони | 10-11 |

Что должно лежать в файлах графики и звука — раскладка каталогов, размеры кадров,
требования к фону и краям, формат и длины звуков — описано в
[../assets-spec.md](../assets-spec.md); задачи этапа его не дублируют.

Этап последний по порядку, но ждёт своей очереди не целиком: формат листа
кадров уже есть, и 10-05 упирается только в него — заказ графики идёт
неделями и параллельно всему остальному.

## Внеплановые

Не связаны с дорожной картой и не связаны зависимостями — берутся когда
угодно. Это либо ошибки, замеченные при работе над другими задачами, либо
идеи, поднятые из «Не запланировано» в [roadmap.md](../roadmap.md).

| # | Задача | Где |
|---|---|---|
| [bug-014](bug-014-player-hit-by-player-is-silent.md) | Попадание игрока по игроку не порождает `EV_PLAYER_HURT` | `player.c`, `weapon.c`, `enemy.c` |
| [bug-015](bug-015-door-closes-silently.md) | Дверь, закрывающаяся сама, и дверь, передумавшая закрываться, беззвучны | `door.c`, `world.c`, `main.c` |
| [bug-016](bug-016-level1-row-8-too-long.md) | Строка 8 `level1.txt` длиннее `MAP_MAX_W`, `map_load()` молча обрезает | `level1.txt`, `map.c` |
| [bug-017](bug-017-hud-face-mouth-inverted.md) | Рот лица в статус-баре перевёрнут: хмурится при полном здоровье | `hud.c` |
| [bug-018](bug-018-lint-changed-deleted-file.md) | `lint.sh changed` отдаёт cppcheck удалённый `src/*.c` и падает | `lint.sh`, `SKILL.md` |
| [bug-019](bug-019-hud-weapon-freezes-mid-flash.md) | Оружие в руках замирает на кадре вспышки под оверлеем победы/смерти | `main.c`, `sprite.c`, `hud.c` |
| [bug-021](bug-021-snapshot-lacks-weapon-ownership.md) | Снапшот не несёт `owned` и патроны стволов не в руках | `net_protocol.c/.h` |
| [chore-001](chore-001-github-actions-ci.md) | CI на трёх платформах через GitHub Actions | `.github/workflows/` |
| [chore-003](chore-003-const-correctness-lint.md) | Указатели без `const`, хотя только читаются | `enemy.c`, `game.c`, `player.c`, `world.c` |
| [chore-005](chore-005-sanitizer-build.md) | Сборка с ASan/UBSan для локальных и CI-прогонов | `CMakeLists.txt`, `driver.sh` |
| [chore-008](chore-008-net-smoke-driver.md) | `netdriver.sh`: сервер и два клиента без экрана, `net-smoke` | `.claude/skills/run-doom-clone/`, `check.sh` |
| [chore-009](chore-009-net-smoke-lag-loss.md) | `net-stress`: 4 клиента под `--netlag`/`--netloss` | `netdriver.sh` |
| [chore-010](chore-010-sanitizers-in-review-harness.md) | Санитайзеры в `check.sh` и у тестера | `check.sh`, `tester.md`, `CLAUDE.md` |
| [chore-013](chore-013-lint-net-client-no-sdl.md) | `lint.sh sim` не проверяет, что `net_client.c` собирается без SDL | `lint.sh`, `SKILL.md`, `harness.md` |
| [chore-017](chore-017-record-game-audio.md) | Запись звука игры в `driver.sh` и анализатор всплесков | `driver.sh`, `SKILL.md`, `harness.md` |
| [feat-003](feat-003-remote-shot-animation.md) | Выстрел чужого игрока и врага в снапшоте: `ANIM_SHOOT` по сети | `net_protocol.c/.h` |
| [feat-004](feat-004-player-chooses-skin.md) | Игрок сам выбирает персонажа из свободных: экран выбора после входа | `net_protocol.h`, `net_server.c`, `game.c`, `sprite.c` |
| [test-002](test-002-fps-with-network.md) | Замер FPS клиента с работающей сетью | `SKILL.md`, `netdriver.sh` |
| [test-003](test-003-text-module-layout-test.md) | Общий модуль шрифта 3×5 и тест раскладки глифов | `text.c`, `hud.c`, `game.c` |

## Закрытые

Переезжают в [../closed_tasks/](../closed_tasks/) и перечисляются здесь.

| # | Задача | Итог |
|---|---|---|
| [05-01](../closed_tasks/05-01-player-state-split.md) | Разделить `Player` на состояние и камеру | Сделано |
| [05-02](../closed_tasks/05-02-weapons-into-player.md) | Перенести `WeaponSystem` внутрь игрока | Сделано |
| [05-03](../closed_tasks/05-03-world-raycast.md) | Мировой DDA без фреймбуфера | Сделано |
| [05-04](../closed_tasks/05-04-hitscan-rewrite.md) | Переписать хитскан на мировой рейкаст | Сделано |
| [05-05](../closed_tasks/05-05-purge-screen-consts.md) | Убрать `SCREEN_W/H` из игровой логики | Сделано |
| [bug-001](../closed_tasks/bug-001-enemy-sees-through-walls.md) | Враг преследует игрока сквозь стены | Сделано |
| [bug-002](../closed_tasks/bug-002-door-closes-on-player.md) | Дверь закрывается в стоящем в проёме | Сделано |
| [test-001](../closed_tasks/test-001-unit-test-harness.md) | Подключить Unity, покрыть тестами то, что уже тестируемо | Сделано |
| [feat-001](../closed_tasks/feat-001-file-textures-weapon-enemy.md) | Файловые PNG-текстуры оружия и врагов | Отменена, см. этап 10 |
| [10-02](../closed_tasks/10-02-sheet-tools.md) | Подготовка и проверка листов кадров | Сделано |
| [bug-007](../closed_tasks/bug-007-purple-fringe-prepare-sheet.md) | Пурпурная кайма по контуру после `prepare_sheet` | Сделано |
| [10-01](../closed_tasks/10-01-sprite-sheet-loader.md) | Формат листа кадров и загрузчик файловых текстур | Сделано |
| [bug-009](../closed_tasks/bug-009-sprite-aspect-ignores-fov.md) | Спрайты сжаты по горизонтали на 17.5 % | Сделано |
| [06-01](../closed_tasks/06-01-world-module.md) | `world.c/.h` + единый `world_step()` | Сделано |
| [06-02](../closed_tasks/06-02-players-array.md) | Массив `players[MAX_PLAYERS]` | Сделано |
| [06-03](../closed_tasks/06-03-enemy-target-selection.md) | ИИ: выбор цели среди N игроков | Сделано |
| [06-04](../closed_tasks/06-04-items-doors-multiplayer.md) | Предметы и двери на N игроков | Сделано |
| [chore-004](../closed_tasks/chore-004-review-agent.md) | Агент-ревьюер: критерии приёмки, lint, smoke, конвенции | Сделано |
| [06-05](../closed_tasks/06-05-sprites-client-side.md) | Спрайты — клиентская сущность, `SPRITE_PLAYER` | Сделано |
| [06-06](../closed_tasks/06-06-input-per-tick.md) | Ввод на тик вместо на кадр | Сделано |
| [bug-003](../closed_tasks/bug-003-dead-fire-pressed-field.md) | Мёртвое поле `fire_pressed` | Сделано, вместе с 06-06 |
| [feat-002](../closed_tasks/feat-002-item-pickup-sheets.md) | Графика аптечки, брони и патронов из `assets/items/` | Сделано |
| [bug-010](../closed_tasks/bug-010-sprite-squashed-when-clipped.md) | Спрайт сплющивается при обрезке верхним краем экрана | Сделано |
| [bug-011](../closed_tasks/bug-011-corpse-draws-first-death-frame.md) | Труп рисуется первым кадром листа смерти | Сделано |
| [bug-012](../closed_tasks/bug-012-corpse-sheet-ignores-enemy-type.md) | Труп сержанта рисуется скином импа | Сделано |
| [bug-013](../closed_tasks/bug-013-procedural-sounds-play-double-speed.md) | Процедурные звуки играют вдвое быстрее и на октаву выше | Сделано |
| [bug-004](../closed_tasks/bug-004-audio-init-leak.md) | `audio_init()` теряет 8 WAV-буферов при запуске | Сделано, вместе с bug-013 |
| [chore-006](../closed_tasks/chore-006-test-coverage.md) | Измерение покрытия тестами (gcovr) | Сделано |
| [chore-007](../closed_tasks/chore-007-stale-doc-facts.md) | Устаревшие утверждения в SKILL.md и architecture.md | Сделано |
| [chore-002](../closed_tasks/chore-002-dead-ternary-text-width.md) | Убрать неработающий тернарник в `text_width()` | Сделано |
| [07-01](../closed_tasks/07-01-enet-build.md) | Подключить ENet к сборке | Сделано |
| [07-02](../closed_tasks/07-02-net-transport.md) | `net_transport` — обёртка над ENet | Сделано |
| [bug-005](../closed_tasks/bug-005-strafe-speed-fov-scaled.md) | Стрейф медленнее ходьбы в `FOV_PLANE` раз | Сделано |
| [bug-006](../closed_tasks/bug-006-hud-draw-int-digit-order.md) | `draw_int()` рисует цифры HUD в обратном порядке | Сделано |
| [07-03](../closed_tasks/07-03-net-protocol.md) | `net_protocol.h` — структуры пакетов | Сделано |
| [chore-011](../closed_tasks/chore-011-lint-changed-untracked.md) | `lint.sh changed` не видит неотслеживаемые `src/*.c` | Сделано |
| [chore-012](../closed_tasks/chore-012-lint-net-protocol-no-sdl.md) | `lint.sh sim` не проверяет, что `net_protocol.c` собирается без SDL | Сделано |
| [10-03](../closed_tasks/10-03-animation-playback.md) | Проигрывание анимаций на клиенте | Сделано |
| [07-04](../closed_tasks/07-04-net-server.md) | Сервер: тик, приём ввода, снапшоты | Сделано |
| [07-05](../closed_tasks/07-05-net-client.md) | Клиент: отправка ввода, приём снапшотов | Сделано |
| [chore-015](../closed_tasks/chore-015-net-bot.md) | `tests/manual/net_bot` (look, run, swarm) и папка `tests/manual/` | Сделано |
| [chore-014](../closed_tasks/chore-014-raise-coverage-thresholds.md) | Поднять пороги покрытия до измеренной базы | Сделано |
| [chore-016](../closed_tasks/chore-016-stale-coverage-docs.md) | Устаревшие числа и примеры в `SKILL.md` про покрытие и `check.sh` | Сделано |
| [bug-020](../closed_tasks/bug-020-coverage-stale-gcno-masks-failure.md) | `coverage.sh`: устаревшие `.gcno` ломают замер, сбой gcovr выдаётся за «НИЖЕ ПОРОГА» | Сделано |
| [10-04](../closed_tasks/10-04-player-skins.md) | Десять скинов игроков | Сделано: скин игрока — его `id` |
| [07-06](../closed_tasks/07-06-audio-events.md) | Звук через сетевые события | Сделано |
| [07-07](../closed_tasks/07-07-launch-modes.md) | Режимы запуска + headless-сборка | Сделано |
| [10-05](../closed_tasks/10-05-five-weapons.md) | Пять стволов и патроны к каждому | Сделано |
| [10-08](../closed_tasks/10-08-status-bar-graphics.md) | Графика статус-бара: лицо, иконки и новая панель | Сделано: панель 52 px из пяти отсеков |
| [bug-008](../closed_tasks/bug-008-load-png-leaks-replaced-pixels.md) | `assets_load_png()` теряет пиксели замещаемого слота | Сделано: освобождает после успешного чтения |
