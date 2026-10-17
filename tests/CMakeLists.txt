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
doom_test(map    SRC map.c door.c)
doom_test(door   SRC door.c)
doom_test(player SRC player.c map.c door.c)
doom_test(enemy  SRC enemy.c raycast_world.c map.c door.c)
doom_test(item   SRC item.c)
# Хитскан мировой: тест линкует трассировку, карту, двери и обе сущности,
# которым он наносит урон.
doom_test(weapon SRC weapon.c raycast_world.c enemy.c player.c map.c door.c)
doom_test(input  SRC input.c)
doom_test(game   SRC game.c)
# Кадр оружия в HUD. Рисование не проверяется; weapon.c линкуется, чтобы
# Weapon.anim затухал так же, как в тике, — с хитсканом и его зависимостями.
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

    # Ручная проверка транспорта между двумя процессами — эхо, kill -9
    # клиента, частота цикла с net_poll(). Сама не завершается, поэтому в
    # ctest не входит.
    add_executable(net_echo net_echo.c ${CMAKE_SOURCE_DIR}/src/net_transport.c)
    target_include_directories(net_echo PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(net_echo PRIVATE doom_enet)
endif()
