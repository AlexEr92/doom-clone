cmake_minimum_required(VERSION 3.15)
project(doom-clone C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# build/compile_commands.json — флаги компиляции для clangd (плагин
# clangd-lsp и IDE): без них он не видит ни SDL, ни vendor/, ни тестовых
# таргетов.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ссылка на неё в корне — для инструментов, которые ищут базу только рядом с
# исходниками. Ставит её одно основное дерево build/: build-cov/ и прочие
# сборки с другими флагами её не перехватывают. RESULT — чтобы Windows без
# прав на симлинки не валила конфигурацию.
if(CMAKE_BINARY_DIR STREQUAL "${CMAKE_SOURCE_DIR}/build")
    file(CREATE_LINK build/compile_commands.json
         ${CMAKE_SOURCE_DIR}/compile_commands.json
         RESULT compile_commands_link SYMBOLIC)
endif()

# По умолчанию — Release, но можно задать через -DCMAKE_BUILD_TYPE
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# ---- Зависимости: SDL2 и SDL2_mixer ----
# Современные дистрибутивы SDL2 поставляют CMake-конфиги (цели SDL2::SDL2,
# SDL2_mixer::SDL2_mixer). Старые установки (и некоторые сборки) экспортируют
# только переменные *_INCLUDE_DIRS / *_LIBRARIES — поддерживаем оба варианта.
find_package(SDL2 REQUIRED)
find_package(SDL2_mixer REQUIRED)

# ---- Зависимости: ENet ----
# Сетевая игра идёт по UDP через ENet, и ENet один на все платформы: он
# собирается из submodule vendor/enet/ и линкуется статически, системный пакет
# не ищется. С -DENABLE_MULTIPLAYER=OFF ENet не нужен вовсе и собирается
# одиночная игра — requirements.md, раздел 1, требует такой сборки на всех
# платформах.
option(ENABLE_MULTIPLAYER "Build the network game with ENet from vendor/enet" ON)

if(ENABLE_MULTIPLAYER)
    if(NOT EXISTS ${CMAKE_SOURCE_DIR}/vendor/enet/CMakeLists.txt)
        message(FATAL_ERROR
            "vendor/enet/ is empty: run git submodule update --init vendor/enet, "
            "or configure with -DENABLE_MULTIPLAYER=OFF for a single-player build.")
    endif()
    # EXCLUDE_FROM_ALL — чтобы install не ставил статическую библиотеку и
    # заголовки ENet вместе с игрой. project(enet) в апстриме не указывает
    # языков и включает ещё и C++: сетевой сборке нужен компилятор C++.
    add_subdirectory(vendor/enet EXCLUDE_FROM_ALL)
    # Строгий -std=c11 проекта прячет в glibc getaddrinfo() и прочий POSIX,
    # на котором стоит unix.c.
    set_target_properties(enet PROPERTIES C_EXTENSIONS ON)
    # -Wall -Wextra проекта — для своего кода; предупреждения апстрима здесь
    # не правятся и только засоряли бы лог сборки.
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(enet PRIVATE -w)
    endif()

    # Игра и тесты получают ENet через эту цель: свой include-путь цель enet
    # наружу не отдаёт, а Winsock и winmm на Windows линкует только под MinGW.
    add_library(doom_enet INTERFACE)
    target_include_directories(doom_enet SYSTEM INTERFACE ${CMAKE_SOURCE_DIR}/vendor/enet/include)
    target_link_libraries(doom_enet INTERFACE enet)
    if(WIN32)
        target_link_libraries(doom_enet INTERFACE ws2_32 winmm)
    endif()
endif()

# ---- Исходники ----
add_executable(doom-clone
    src/main.c
    src/engine.c
    src/input.c
    src/map.c
    src/player.c
    src/world.c
    src/raycast.c
    src/raycast_world.c
    src/sprite.c
    src/enemy.c
    src/weapon.c
    src/hud.c
    src/audio.c
    src/assets.c
    src/utils.c
    src/door.c
    src/game.c
    src/item.c
)

# vendor/ — для stb_image.h
target_include_directories(doom-clone PRIVATE ${CMAKE_SOURCE_DIR}/vendor)

# SDL2
if(TARGET SDL2::SDL2)
    target_link_libraries(doom-clone PRIVATE SDL2::SDL2)
else()
    target_include_directories(doom-clone PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(doom-clone PRIVATE ${SDL2_LIBRARIES})
endif()

# SDL2_mixer
if(TARGET SDL2_mixer::SDL2_mixer)
    target_link_libraries(doom-clone PRIVATE SDL2_mixer::SDL2_mixer)
else()
    target_include_directories(doom-clone PRIVATE ${SDL2_MIXER_INCLUDE_DIRS})
    target_link_libraries(doom-clone PRIVATE ${SDL2_MIXER_LIBRARIES})
endif()

# ENet и сетевые исходники — только в сетевой сборке
if(ENABLE_MULTIPLAYER)
    target_sources(doom-clone PRIVATE
        src/net_transport.c
    )
    target_link_libraries(doom-clone PRIVATE doom_enet)
endif()

# libm есть на Unix-системах; на Windows (MSVC) её нет
if(NOT WIN32)
    target_link_libraries(doom-clone PRIVATE m)
endif()

# Удобный запуск из корня проекта (ассеты грузятся по относительным путям)
add_custom_target(run
    COMMAND $<TARGET_FILE:doom-clone>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    DEPENDS doom-clone
    COMMENT "Running doom-clone"
)

install(TARGETS doom-clone RUNTIME DESTINATION bin)

# ---- Юнит-тесты ----
# Отдельная опция: основной таргет doom-clone не зависит ни от Unity, ни от
# tests/, поэтому сборку тестов можно выключить целиком.
option(BUILD_TESTS "Build unit tests" ON)

# Покрытие снимается только с тестовых бинарников (см. doom_test() в
# tests/CMakeLists.txt): doom-clone под ctest не запускается, и его
# инструментирование лишь добавило бы в знаменатель нули по рендеру и звуку.
option(ENABLE_COVERAGE "Instrument unit tests for gcov/gcovr" OFF)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Инструменты подготовки графики ----
# Тоже за отдельной опцией: игра о tools/ не знает, а сами утилиты не тянут
# ни SDL, ни исходники из src/.
option(BUILD_TOOLS "Build the asset sheet tools in tools/" OFF)

if(BUILD_TOOLS)
    foreach(tool prepare_sheet check_sheets)
        add_executable(${tool} tools/${tool}.c)
        target_include_directories(${tool} SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/vendor)
        target_include_directories(${tool} PRIVATE ${CMAKE_SOURCE_DIR}/tools)
        if(NOT WIN32)
            target_link_libraries(${tool} PRIVATE m)
        endif()
    endforeach()
endif()
