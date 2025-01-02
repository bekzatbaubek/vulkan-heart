@echo off

glslangValidator -V shaders\heart.vert -o shaders\heart.vert.spv
glslangValidator -V shaders\heart.frag -o shaders\heart.frag.spv

clang++ --std=c++20 vulkan-heart.cpp -o ./build/vulkan-heart.exe -g -I"%VULKAN_SDK%\Include" -I".\vendor" -L"%VULKAN_SDK%\Lib" -L".\lib-vc2022" -lglfw3 -lvulkan-1  -luser32 -lgdi32 -lshell32 -lmsvcrt -Xlinker /NODEFAULTLIB:libcmt
