# Юнит-тесты на Unity. Каждый исполняемый файл линкует Unity и только те
# src/*.c, которые нужны именно ему, — не всю игру: часть модулей сращена с
# рендером и тянет за собой зависимости, к проверяемой логике отношения не
# имеющие.

# ---- Unity ----
# Собирается один раз и линкуется во все тесты.
add_library(unity STATIC ${CMAKE_SOURCE_DIR}/vendor/unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/vendor/unity)

# ---- Заголовки SDL2 ----
# input.c и game.c компилируются с заголовками SDL2 (типы SDL_Event, SDLK_*),
# но функций SDL не вызывают — им достаточно include-путей. Модули симуляции
# про SDL уже не знают. Берём заголовки так же, как основной таргет: из
# импортированной цели, если find_package дал её, иначе из переменных.
if(TARGET SDL2::SDL2)
    get_target_property(TEST_SDL2_INCLUDES SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    set(TEST_SDL2_LIBS SDL2::SDL2)
else()
    set(TEST_SDL2_INCLUDES ${SDL2_INCLUDE_DIRS})
    set(TEST_SDL2_LIBS ${SDL2_LIBRARIES})
endif()

# get_target_property возвращает <VAR>-NOTFOUND, если свойства нет.
if(NOT TEST_SDL2_INCLUDES)
    set(TEST_SDL2_INCLUDES "")
endif()

# ---- SDL2_mixer ----
# Только test_audio: audio.c открывает микшер по-настоящему, на драйвере
# dummy, и линкуется с SDL2 и SDL2_mixer, а не с одними заголовками.
if(TARGET SDL2_mixer::SDL2_mixer)
    set(TEST_MIXER_LIBS SDL2_mixer::SDL2_mixer ${TEST_SDL2_LIBS})
else()
    set(TEST_MIXER_LIBS ${SDL2_MIXER_LIBRARIES} ${TEST_SDL2_LIBS})
    list(APPEND TEST_SDL2_INCLUDES ${SDL2_MIXER_INCLUDE_DIRS})
endif()

# ---- Один тест на модуль ----
# doom_test(<модуль> [FILE <файл теста>] [SRC <файлы из src/>...] [LINK <библиотеки>...])
# FILE нужен только там, где имя файла не test_<модуль>.c: replay-тест назван
# в requirements.md, раздел 9, как tests/replay_test.c.
function(doom_test module)
    cmake_parse_arguments(T "" "FILE" "SRC;LINK" ${ARGN})
    if(NOT T_FILE)
        set(T_FILE test_${module}.c)
    endif()
    set(sources "")
    foreach(s IN LISTS T_SRC)
        list(APPEND sources ${CMAKE_SOURCE_DIR}/src/${s})
    endforeach()

    add_executable(test_${module} ${T_FILE} ${sources})
    target_include_directories(test_${module} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${TEST_SDL2_INCLUDES}
    )
    target_link_libraries(test_${module} PRIVATE unity ${T_LINK})
    if(NOT WIN32)
        target_link_libraries(test_${module} PRIVATE m)
    endif()
    if(ENABLE_COVERAGE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(test_${module} PRIVATE --coverage -O0 -g)
        target_link_options(test_${module} PRIVATE --coverage)
    endif()
    add_test(NAME ${module} COMMAND test_${module})
endfunction()

doom_test(utils)
doom_test(event)
doom_test(map    SRC map.c door.c)
doom_test(door   SRC door.c)
doom_test(player SRC player.c map.c door.c)
doom_test(enemy  SRC enemy.c raycast_world.c map.c door.c)
doom_test(item   SRC item.c)
# Хитскан мировой: тест линкует трассировку, карту, двери и обе сущности,
# которым он наносит урон.
doom_test(weapon SRC weapon.c raycast_world.c enemy.c player.c map.c door.c)
doom_test(input  SRC input.c)
# Командная строка: options.c не знает ни SDL, ни других модулей. Сетевые флаги
# он принимает только в сетевой сборке, и тест собирается с тем же макросом,
# что и игра, — в сборке без сети он проверяет, что они отвергаются.
doom_test(options SRC options.c)
if(ENABLE_MULTIPLAYER)
    target_compile_definitions(test_options PRIVATE ENABLE_MULTIPLAYER)
endif()
doom_test(game   SRC game.c)
# Кадр оружия и кадр лица в HUD. Рисование не проверяется; weapon.c линкуется,
# чтобы Weapon.anim затухал так же, как в тике, — с хитсканом и его
# зависимостями, player.c — чтобы попадание снимало hp, как в мире.
doom_test(hud    SRC hud.c weapon.c raycast_world.c enemy.c player.c map.c door.c)
# Скины, выбор анимации и кадра персонажей. Сам рендер спрайтов не
# проверяется: zBuffer тест определяет сам, raycast.c не линкуется.
doom_test(sprite SRC sprite.c assets.c enemy.c raycast_world.c map.c door.c)
# Загрузчик листов: assets.c тянет stb_image по относительному пути, а сами
# PNG тест пишет сам — фикстур в tests/ нет.
doom_test(assets SRC assets.c)
# Длительность синтезированных чанков в формате устройства. Звук вне охвата
# test-001, но драйвер dummy открывает микшер без звуковой карты, а тембр тест
# не проверяет — только длину данных.
doom_test(audio SRC audio.c LINK ${TEST_MIXER_LIBS})
# Детерминизм world_step() — требование requirements.md, раздел 9. Линкует всю
# симуляцию; графики в ней нет — спрайты строит клиент.
doom_test(replay FILE replay_test.c
    SRC world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c)
# ---- Ручные инструменты: tests/manual/ ----
# Их запускают руками, а «работает или нет» по их выводу решает человек или
# агент, поэтому в ctest их нет. Граница проведена папкой: всё, что лежит в
# tests/ напрямую, гоняет ctest, а в tests/manual/ — то, что нет. Собираются
# они вместе с игрой, чтобы правка API, на котором они стоят, ломала сборку, а
# не оставляла их устаревшими молча.
# doom_manual(<имя> SRC <файлы из src/>... [LINK <библиотеки>...]) собирает
# tests/manual/<имя>.c в build/tests/<имя>.
function(doom_manual name)
    cmake_parse_arguments(T "" "" "SRC;LINK" ${ARGN})
    set(sources "")
    foreach(s IN LISTS T_SRC)
        list(APPEND sources ${CMAKE_SOURCE_DIR}/src/${s})
    endforeach()
    add_executable(${name} manual/${name}.c ${sources})
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${TEST_SDL2_INCLUDES}
    )
    target_link_libraries(${name} PRIVATE ${T_LINK})
    if(NOT WIN32)
        target_link_libraries(${name} PRIVATE m)
    endif()
endfunction()

# Статус-бар, нарисованный hud_draw() с файлами из assets/ или без них, в
# состояниях, до которых в игре трудно дойти: ступени здоровья, попадание,
# смерть, каждый ствол в руках. Сети не нужно, поэтому он вне блока ниже.
doom_manual(hud_shot SRC hud.c assets.c player.c weapon.c raycast_world.c enemy.c map.c door.c)

if(ENABLE_MULTIPLAYER)
    # Сам ENet из vendor/enet/: заголовки и библиотека — один и тот же ENet.
    # Системный enet.h, найденный раньше vendor/enet/include, собрался бы и
    # слинковался, а разошёлся бы с библиотекой только во время работы.
    doom_test(enet LINK doom_enet)
    # Транспорт по настоящим сокетам на 127.0.0.1, сервер и клиенты в одном
    # процессе. Таймауты тест проверяет сдвигом часов ENet, а не ожиданием.
    doom_test(net_transport SRC net_transport.c LINK doom_enet)
    # Формат пакетов и снапшот мира. ENet ему не нужен — сокетов тест не
    # открывает, — но протокол есть только в сетевой сборке. Линкует всю
    # симуляцию: snapshot_apply() заводит игроков и врагов её же функциями.
    doom_test(net_protocol SRC net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c)
    # Буфер снапшотов и интерполяция чужих игроков и врагов: время теста своё,
    # сокетов нет. Линкует протокол и симуляцию — вид строится из мира,
    # загруженного с карты, функциями snapshot_apply().
    doom_test(net_interp SRC net_interp.c net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c)
    # Сервер по настоящим сокетам на 127.0.0.1: он и клиенты-боты, которые
    # говорят на протоколе вручную, в одном процессе. Таймауты — сдвигом часов
    # ENet, как в test_net_transport.
    doom_test(net_server SRC net_server.c net_transport.c net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c
        LINK doom_enet)

    # Клиент против настоящего сервера и против подставного, который говорит
    # на протоколе вручную, — всё в одном процессе, по 127.0.0.1. Карту из
    # MSG_ACCEPT клиент ищет в assets/maps/ рабочего каталога; тест пишет её
    # туда сам, каталог заводится здесь, рядом с тестом.
    doom_test(net_client SRC net_client.c net_interp.c net_server.c net_transport.c net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c
        LINK doom_enet)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/assets/maps)

    # Режимы запуска через таблицу GameSession, как их ведёт цикл кадра: solo,
    # host и dedicated на 127.0.0.1, клиент против сессии хоста и против
    # подставного, который говорит на протоколе вручную. Карту пишет в тот же
    # assets/maps/, что и test_net_client.
    doom_test(session SRC session.c net_client.c net_interp.c net_server.c net_transport.c net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c
        LINK doom_enet)
    target_compile_definitions(test_session PRIVATE ENABLE_MULTIPLAYER)

    # Транспорт между двумя процессами — эхо, kill -9 клиента, частота цикла
    # с net_poll().
    doom_manual(net_echo SRC net_transport.c LINK doom_enet)
    # Игроки без окна, через net_client.h: мир глазами клиента, игрок по
    # сценарию, нагрузка из нескольких игроков.
    doom_manual(net_bot SRC net_client.c net_interp.c net_transport.c net_protocol.c
        world.c player.c map.c door.c enemy.c item.c weapon.c raycast_world.c
        LINK doom_enet)
endif()
