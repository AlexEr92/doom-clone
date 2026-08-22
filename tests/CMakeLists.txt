# Юнит-тесты на Unity. Каждый исполняемый файл линкует Unity и только те
# src/*.c, которые нужны именно ему, — не всю игру: часть модулей сращена с
# рендером и тянет за собой зависимости, к проверяемой логике отношения не
# имеющие.

# ---- Unity ----
# Собирается один раз и линкуется во все тесты.
add_library(unity STATIC ${CMAKE_SOURCE_DIR}/vendor/unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_SOURCE_DIR}/vendor/unity)

# ---- Заголовки SDL2 ----
# door.c/player.c/input.c/game.c/item.c компилируются с заголовками SDL2 (типы
# SDL_Event, SDLK_*, Mix_Chunk), но функций SDL не вызывают — им достаточно
# include-путей. Берём их так же, как основной таргет: из импортированной цели,
# если find_package дал её, иначе из переменных.
if(TARGET SDL2::SDL2)
    get_target_property(TEST_SDL2_INCLUDES SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    set(TEST_SDL2_LIBS SDL2::SDL2)
else()
    set(TEST_SDL2_INCLUDES ${SDL2_INCLUDE_DIRS})
    set(TEST_SDL2_LIBS ${SDL2_LIBRARIES})
endif()

if(TARGET SDL2_mixer::SDL2_mixer)
    get_target_property(TEST_SDL2_MIXER_INCLUDES SDL2_mixer::SDL2_mixer
                        INTERFACE_INCLUDE_DIRECTORIES)
    set(TEST_SDL2_MIXER_LIBS SDL2_mixer::SDL2_mixer)
else()
    set(TEST_SDL2_MIXER_INCLUDES ${SDL2_MIXER_INCLUDE_DIRS})
    set(TEST_SDL2_MIXER_LIBS ${SDL2_MIXER_LIBRARIES})
endif()

# get_target_property возвращает <VAR>-NOTFOUND, если свойства нет.
foreach(var TEST_SDL2_INCLUDES TEST_SDL2_MIXER_INCLUDES)
    if(NOT ${var})
        set(${var} "")
    endif()
endforeach()

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
        ${TEST_SDL2_MIXER_INCLUDES}
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
# sprite_add() и audio_play() заменены заглушками в самом тесте, поэтому
# sprite.c (а с ним raycast.c ради zBuffer) и audio.c здесь не нужны.
doom_test(enemy  SRC enemy.c raycast_world.c map.c door.c)
# item.c вызывает audio_play_volume() без проверки на NULL, поэтому линкуется
# настоящий audio.c: он безопасен для обнулённого Audio (available == 0).
doom_test(item   SRC item.c audio.c LINK ${TEST_SDL2_LIBS} ${TEST_SDL2_MIXER_LIBS})
# Хитскан стал мировым: тест линкует трассировку, карту, двери и обе
# сущности, которым он наносит урон. Заглушены только sprite_add() и
# аудио — ради них тянуть sprite.c (а с ним raycast.c) и audio.c незачем.
doom_test(weapon SRC weapon.c raycast_world.c enemy.c player.c map.c door.c)
doom_test(input  SRC input.c)
doom_test(game   SRC game.c)
