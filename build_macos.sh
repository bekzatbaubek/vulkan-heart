#!/bin/sh

mkdir -p build

glslangValidator -V shaders/heart.vert -o shaders/heart.vert.spv
glslangValidator -V shaders/heart.frag -o shaders/heart.frag.spv

COMMON_FLAGS="-I/opt/homebrew/include -L/opt/homebrew/lib -D VKH_DEBUG -g -fno-exceptions -fno-rtti --std=c++17"

clang++ $COMMON_FLAGS vkh_game.cpp -shared -o ./build/vkh_game.so
clang++ $COMMON_FLAGS vkh_platform_sdl.cpp -o ./build/vkh_platform -lSDL3 -lvulkan

# Curse upon rpath
install_name_tool -add_rpath /usr/local/lib ./build/vkh_platform
