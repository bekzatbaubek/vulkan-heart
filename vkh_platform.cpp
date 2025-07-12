#include <string>
#ifdef _WIN64
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <sys/stat.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include "vkh_game.h"
#include "vkh_memory.cpp"
#include "vkh_renderer.cpp"

time_t getLastModified(const char* path) {
    struct stat attr;
    return stat(path, &attr) == 0 ? attr.st_mtime : 0;
}

void platform_handle_input(GLFWwindow* window, GameInput* input) {
    input->digital_inputs[D_LEFT].is_down =
        glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS;

    input->digital_inputs[D_RIGHT].is_down =
        glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS;

    input->digital_inputs[D_UP].is_down =
        glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS;

    input->digital_inputs[D_DOWN].is_down =
        glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS;

    input->digital_inputs[SELECT].is_down =
        glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;

    input->digital_inputs[START].is_down =
        glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS;
}

struct GameCode {
#ifdef _WIN64
    HMODULE so_handle;
#else
    void* so_handle;
#endif
    game_update_t gameUpdateAndRender;
    time_t lastModified;
};

void platform_free_game_code(GameCode* gameCode) {
#ifdef _WIN64
    FreeLibrary(gameCode->so_handle);
#else
    dlclose(gameCode->so_handle);
#endif
}

GameCode platform_load_game_code(const char* sourcePath) {
    GameCode gameCode = {0, 0, 0};
    gameCode.lastModified = getLastModified(sourcePath);

#ifdef _WIN64
    gameCode.so_handle = LoadLibrary(sourcePath);
    assert(gameCode.so_handle);
    gameCode.gameUpdateAndRender = (game_update_t)GetProcAddress(
        gameCode.so_handle, "game_update_and_render");
#else
    gameCode.so_handle = dlopen(sourcePath, RTLD_NOW);
    gameCode.gameUpdateAndRender =
        (game_update_t)dlsym(gameCode.so_handle, "game_update_and_render");
#endif
    assert(gameCode.gameUpdateAndRender);

    return gameCode;
}

void platform_reload_game_code(GameCode* gameCode, const char* sourcePath) {
    time_t currentModified = getLastModified(sourcePath);
    if (currentModified > gameCode->lastModified) {
        gameCode->lastModified = currentModified;
        platform_free_game_code(gameCode);
        platform_load_game_code(sourcePath);
        std::cerr << "Game code reloaded\n";
    }
}

int main(int argc, char* argv[]) {
    int window_width = 800;
    int window_height = 600;

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window =
        glfwCreateWindow(window_width, window_height, "Vulkan Heart", 0, 0);
    assert(window);

    uint64_t timer_frequency = glfwGetTimerFrequency();

#ifdef _WIN64
    const char* sourcePath = "vkh_game.dll";
#else
    const char* sourcePath = "./build/vkh_game.so";
#endif
    GameCode gameCode = platform_load_game_code(sourcePath);

    MemoryArena renderer_arena = {0};
    arena_init(&renderer_arena, megabytes(128));

    VulkanContext context = {0};
    RendererInit(&context, window, &renderer_arena);

    GameMemory game_memory = {0};
    game_memory.permanent_store_size = megabytes((uint64_t)256);
    game_memory.permanent_store = malloc(game_memory.permanent_store_size);

    game_memory.transient_store_size = gigabytes((uint64_t)2);
    game_memory.transient_store = malloc(game_memory.transient_store_size);

    GameInput input = {};
    while (!glfwWindowShouldClose(window)) {
        double start_time = glfwGetTime();

        glfwPollEvents();

        platform_reload_game_code(&gameCode, sourcePath);
        platform_handle_input(window, &input);

        gameCode.gameUpdateAndRender(&game_memory, &input);
        RendererDrawFrame(&context, &renderer_arena);

        double end_time = glfwGetTime();
        double time_elapsed_seconds = (end_time - start_time);

        std::string window_title =
            "Vulkan Heart - " + std::to_string(1.0f / time_elapsed_seconds) +
            " FPS" + " - " + std::to_string(time_elapsed_seconds * 1000.0f) +
            " ms";

        glfwSetWindowTitle(window, window_title.c_str());
    }

    vkDeviceWaitIdle(context.device);
    platform_free_game_code(&gameCode);
    return 0;
}
