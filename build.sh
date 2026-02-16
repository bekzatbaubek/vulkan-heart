#!/bin/sh

mkdir -p build

glslangValidator -V shaders/heart.vert -o shaders/heart.vert.spv
glslangValidator -V shaders/heart.frag -o shaders/heart.frag.spv

COMMON_FLAGS="-D VKH_DEBUG -g -fno-exceptions -fno-rtti --std=c++17"

clang++ $COMMON_FLAGS vkh_game.cpp -shared -o ./build/vkh_game.so.tmp
clang++ $COMMON_FLAGS vkh_platform_sdl.cpp -o ./build/vkh_platform -lSDL3 -lvulkan

mv ./build/vkh_game.so.tmp ./build/vkh_game.so
