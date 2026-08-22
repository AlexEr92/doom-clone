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

# ---- Один тест на модуль ----
# doom_test(<модуль> [SRC <файлы из src/>...] [LINK <библиотеки>...])
function(doom_test module)
    cmake_parse_arguments(T "" "" "SRC;LINK" ${ARGN})
    set(sources "")
    foreach(s IN LISTS T_SRC)
        list(APPEND sources ${CMAKE_SOURCE_DIR}/src/${s})
    endforeach()

    add_executable(test_${module} test_${module}.c ${sources})
    target_include_directories(test_${module} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${TEST_SDL2_INCLUDES}
    )
    target_link_libraries(test_${module} PRIVATE unity ${T_LINK})
    if(NOT WIN32)
        target_link_libraries(test_${module} PRIVATE m)
    endif()
    add_test(NAME ${module} COMMAND test_${module})
endfunction()

doom_test(utils)
doom_test(map    SRC map.c door.c)
doom_test(door   SRC door.c)
doom_test(player SRC player.c map.c door.c)
# sprite_add() заменён заглушкой в самом тесте, поэтому sprite.c (а с ним
# raycast.c ради zBuffer) здесь не нужен.
doom_test(enemy  SRC enemy.c raycast_world.c map.c door.c)
doom_test(item   SRC item.c)
# Хитскан стал мировым: тест линкует трассировку, карту, двери и обе
# сущности, которым он наносит урон. Заглушен только sprite_add() — ради него
# тянуть sprite.c (а с ним raycast.c) незачем.
doom_test(weapon SRC weapon.c raycast_world.c enemy.c player.c map.c door.c)
doom_test(input  SRC input.c)
doom_test(game   SRC game.c)
# Загрузчик листов: assets.c тянет stb_image по относительному пути, а сами
# PNG тест пишет сам — фикстур в tests/ нет.
doom_test(assets SRC assets.c)
