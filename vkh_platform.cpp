#include <GLFW/glfw3.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>

#include "vkh.hpp"

time_t getLastModified(const char* path) {
    struct stat attr;
    return stat(path, &attr) == 0 ? attr.st_mtime : 0;
}

int main(int argc, char* argv[]) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(800, 600, "Vulkan Heart", 0, 0);

    const char* sourcePath = "./build/lib.so";
    game_update_t gameUpdateAndRender = 0;

    void* so_handle = dlopen(sourcePath, RTLD_NOW);
    gameUpdateAndRender = (game_update_t)dlsym(so_handle, "gameUpdateAndRender");
    time_t lastModified = getLastModified(sourcePath);

    void* gameMemory = malloc(1024);
    GameState state{};

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        time_t currentModified = getLastModified(sourcePath);
        if (currentModified > lastModified) {
            lastModified = currentModified;

            dlclose(so_handle);
            so_handle = dlopen(sourcePath, RTLD_NOW);
            gameUpdateAndRender = (game_update_t)dlsym(so_handle, "gameUpdateAndRender");
        }

        gameUpdateAndRender(&state);
    }

    dlclose(so_handle);

    return 0;
}
