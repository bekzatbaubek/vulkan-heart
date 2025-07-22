#!/bin/sh

mkdir -p build

glslangValidator -V shaders/heart.vert -o shaders/heart.vert.spv
glslangValidator -V shaders/heart.frag -o shaders/heart.frag.spv

clang++ \
    --std=c++17 \
    -D VKH_DEBUG \
    -g \
    -fno-exceptions \
    -fno-rtti \
    vkh_game.cpp \
    -shared \
    -o ./build/vkh_game.so

clang++ \
    --std=c++17 \
    -D VKH_DEBUG \
    -g \
    -fno-exceptions \
    -fno-rtti \
    vkh_platform_sdl.cpp \
    -o ./build/vkh_platform \
    -lSDL3 \
    -lvulkan

# Curse upon rpath
install_name_tool -add_rpath /usr/local/lib ./build/vkh_platform
