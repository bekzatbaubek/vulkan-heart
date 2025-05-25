#include <dlfcn.h>
#include <sys/stat.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>

#include "vkh_game.hpp"
#include "vkh_memory.cpp"
#include "vkh_renderer.cpp"

time_t getLastModified(const char* path) {
    struct stat attr;
    return stat(path, &attr) == 0 ? attr.st_mtime : 0;
}

void platform_handle_input(GLFWwindow* window, GameInput* input) {
    // Keyboard input for test
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

int main(int argc, char* argv[]) {
    int window_width = 800;
    int window_height = 600;

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window =
        glfwCreateWindow(window_width, window_height, "Vulkan Heart", 0, 0);
    assert(window);
    uint64_t timer_frequency = glfwGetTimerFrequency();

    const char* sourcePath = "./build/lib.so";
    game_update_t gameUpdateAndRender = 0;

    void* so_handle = dlopen(sourcePath, RTLD_NOW);
    gameUpdateAndRender =
        (game_update_t)dlsym(so_handle, "game_update_and_render");
    time_t lastModified = getLastModified(sourcePath);

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
        glfwPollEvents();

        time_t currentModified = getLastModified(sourcePath);
        if (currentModified > lastModified) {
            lastModified = currentModified;

            dlclose(so_handle);
            so_handle = dlopen(sourcePath, RTLD_NOW);
            gameUpdateAndRender =
                (game_update_t)dlsym(so_handle, "game_update_and_render");
        }

        uint64_t start_time = glfwGetTimerValue();
        platform_handle_input(window, &input);

        gameUpdateAndRender(&game_memory, &input);

        RendererDrawFrame(&context, &renderer_arena);

        uint64_t end_time = glfwGetTimerValue();
        double time_elapsed_seconds =
            ((double)end_time - (double)start_time) / timer_frequency;

#if 0
        std::cerr << "Elapsed time: " << time_elapsed_seconds * 1000.0f
                  << "ms\n"
                  << "FPS: " << 1.0f / (time_elapsed_seconds) << "\n";
#endif
    }

    dlclose(so_handle);
    vkDeviceWaitIdle(context.device);

    return 0;
}
