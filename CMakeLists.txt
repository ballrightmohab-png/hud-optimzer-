cmake_minimum_required(VERSION 3.22)

project(hud_optimizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hud_optimizer SHARED
    src/HUDOptimizer.cpp
)

target_compile_definitions(hud_optimizer PRIVATE
    HUD_OPTIMIZER_VERSION="0.1.0"
)

set_target_properties(hud_optimizer PROPERTIES
    OUTPUT_NAME "hud_optimizer"
)
