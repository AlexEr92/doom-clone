cmake_minimum_required(VERSION 3.15)
project(doom-clone C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

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

# ---- Исходники ----
add_executable(doom-clone
    src/main.c
    src/engine.c
    src/input.c
    src/map.c
    src/player.c
    src/raycast.c
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