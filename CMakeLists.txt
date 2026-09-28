cmake_minimum_required(VERSION 3.22)

project(hud_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 0.2.2
)

FetchContent_MakeAvailable(preloader_android)

add_library(hud_optimizer SHARED
    src/HUDOptimizer.cpp
)

target_link_libraries(hud_optimizer
    PRIVATE
    preloader
    log
)

# Ensure Android linker tolerates deferred/external preloader symbols gracefully
target_link_options(hud_optimizer PRIVATE
    "-Wl,--allow-shlib-undefined"
)

target_compile_options(hud_optimizer PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
)

set_target_properties(hud_optimizer PROPERTIES
    OUTPUT_NAME "hud_optimizer"
)
