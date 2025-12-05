@echo off

mkdir build

glslangValidator -V shaders\heart.vert -o shaders\heart.vert.spv
glslangValidator -V shaders\heart.frag -o shaders\heart.frag.spv

set COMMON_CXX_FLAGS=-DVKH_DEBUG --std=c++17 -Wall -Wno-unused-variable -g -fno-exceptions -fno-rtti

clang++ ^
    %COMMON_CXX_FLAGS% ^
    vkh_game.cpp ^
    -shared ^
    -o .\build\vkh_game.dll ^
    -Xlinker /PDB:".\build\vkh_game.pdb" -Xlinker /INCREMENTAL:NO

clang++ ^
    %COMMON_CXX_FLAGS% ^
    vkh_platform_sdl.cpp ^
    -o .\build\vkh_platform.exe ^
    -I"vendor\include" ^
    -L"vendor\lib" ^
    -I"C:\VulkanSDK\1.4.321.1\Include" ^
    -L"C:\VulkanSDK\1.4.321.1\Lib" ^
    -lvulkan-1 ^
    -lSDL3 ^
    -luser32 -lgdi32 -lshell32 -lmsvcrt -Xlinker /NODEFAULTLIB:libcmt -Xlinker /INCREMENTAL:NO
