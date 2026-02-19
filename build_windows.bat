@echo off

mkdir build

glslangValidator -V shaders\heart.vert -o shaders\heart.vert.spv
glslangValidator -V shaders\heart.frag -o shaders\heart.frag.spv

set COMMON_CXX_FLAGS=-DVKH_DEBUG --std=c++17 -Wall -Wno-unused-variable -g -fno-exceptions -fno-rtti

rem Create a short timestamp token (yyyyMMdd_HHmmss). Uses PowerShell to be locale-independent.
for /f "delims=" %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set TIMESTAMP=%%T

rem PDB filenames with timestamp so each build produces a new PDB instead of overwriting.
set "PDB_GAME=%CD%\build\vkh_game_%TIMESTAMP%.pdb"

clang++ ^
    %COMMON_CXX_FLAGS% ^
    vkh_game.cpp ^
    -shared ^
    -o .\build\vkh_game.dll ^
     -Xlinker "/PDB:%PDB_GAME%" ^
    -Xlinker /INCREMENTAL:NO


clang++ ^
    %COMMON_CXX_FLAGS% ^
    vkh_platform_sdl.cpp ^
    -o .\build\vkh_platform.exe ^
    -I"vendor\include" ^
    -L"vendor\lib" ^
    -I"C:\VulkanSDK\1.4.341.1\Include" ^
    -L"C:\VulkanSDK\1.4.341.1\Lib" ^
    -lvulkan-1 ^
    -lSDL3 ^
    -luser32 -lgdi32 -lshell32 -lmsvcrt -Xlinker /NODEFAULTLIB:libcmt -Xlinker /INCREMENTAL:NO
