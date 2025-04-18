#!/bin/sh

mkdir -p build

# glslangValidator -V shaders/heart.vert -o shaders/heart.vert.spv
# glslangValidator -V shaders/heart.frag -o shaders/heart.frag.spv

# clang++ \
#     --std=c++20 \
#     -g \
#     vulkan-heart.cpp \
#     -o ./build/vulkan-heart \
#     -Ivendor/ \
#     -lglfw \
#     -lvulkan

clang++ \
    --std=c++20 \
    -g \
    vkh.cpp \
    -shared \
    -o ./build/lib.so

clang++ \
    --std=c++20 \
    -g \
    vkh_platform.cpp \
    -o ./build/vkh_platform \
    -Ivendor/ \
    -lglfw \
    -lvulkan
