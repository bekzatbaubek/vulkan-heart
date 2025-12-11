#include "vkh_memory.h"
#include "vkh_renderer.h"
#ifdef _WIN64
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>

#ifdef VKH_DEBUG
#define Assert(expr) {if (!(expr)) {__builtin_debugtrap();}}
#else
#define Assert(expr)
#endif

#define InvalidCodePath Assert(false)

#define kilobytes(n) ((n) * 1024LL)
#define megabytes(n) (kilobytes(n) * 1024LL)
#define gigabytes(n) (megabytes(n) * 1024LL)

#include "vkh_game.h"
#include "vkh_memory.cpp"
#include "vkh_renderer.cpp"

bool GLOBAL_running = true;

time_t getLastModified(const char* path) {
    struct stat attr;
    return stat(path, &attr) == 0 ? attr.st_mtime : 0;
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
    Assert(gameCode.so_handle);
    gameCode.gameUpdateAndRender = (game_update_t)GetProcAddress(
        gameCode.so_handle, "game_update_and_render");
#else
    gameCode.so_handle = dlopen(sourcePath, RTLD_NOW);
    gameCode.gameUpdateAndRender =
        (game_update_t)dlsym(gameCode.so_handle, "game_update_and_render");
#endif
    Assert(gameCode.gameUpdateAndRender);

    return gameCode;
}

void platform_reload_game_code(GameCode* gameCode, const char* sourcePath) {
    time_t currentModified = getLastModified(sourcePath);
    if (currentModified > gameCode->lastModified) {
        gameCode->lastModified = currentModified;
        platform_free_game_code(gameCode);
        *gameCode = platform_load_game_code(sourcePath);
        fprintf(stderr, "Game code reloaded\n");
    }
}

void handle_SDL_event(SDL_Event* event, GameInput* input, VulkanContext* renderer_context, MemoryArena* renderer_arena) {
    switch (event->type) {
        case SDL_EVENT_QUIT: {
            GLOBAL_running = false;
        } break;
        case SDL_EVENT_KEY_DOWN: {
            switch (event->key.scancode) {
                case SDL_SCANCODE_W: {
                    input->digital_inputs[D_UP].is_down = true;
                } break;
                case SDL_SCANCODE_S: {
                    input->digital_inputs[D_DOWN].is_down = true;
                } break;
                case SDL_SCANCODE_A: {
                    input->digital_inputs[D_LEFT].is_down = true;
                } break;
                case SDL_SCANCODE_D: {
                    input->digital_inputs[D_RIGHT].is_down = true;
                } break;
                default: {
                }
            }
        } break;
        case SDL_EVENT_WINDOW_RESIZED: {
            // printf("Window resized: width: %d, height: %d\n", event->window.data1, event->window.data2);
            renderer_context->WindowDrawableAreaWidth = event->window.data1;
            renderer_context->WindowDrawableAreaHeight = event->window.data2;
            RecreateSwapchainResources(renderer_context, renderer_arena);
        } break;
        case SDL_EVENT_MOUSE_MOTION: {
            // printf("Mouse moved: x: %f y: %f, xrel: %f, yrel: %f\n", event->motion.x, event->motion.y, event->motion.xrel, event->motion.yrel);
            input->mouse_x = event->motion.x;
            input->mouse_y = event->motion.y;
        } break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            // printf("Mouse button down: button: %d, x: %f, y: %f\n", event->button.button, event->button.x, event->button.y);
        } break;
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            // printf("Mouse button up: button: %d, x: %f, y: %f\n", event->button.button, event->button.x, event->button.y);
        } break;
    }
}

int main(int argc, char** argv) {
    int window_width = 800;
    int window_height = 600;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window = SDL_CreateWindow("Vulkan Heart", window_width,
                                          window_height, SDL_WINDOW_VULKAN);
    assert(window);
    SDL_SetWindowResizable(window, true);

#ifdef _WIN64
    const char* sourcePath = "vkh_game.dll";
#else
    const char* sourcePath = "./build/vkh_game.so";
#endif
    GameCode gameCode = platform_load_game_code(sourcePath);

    MemoryArena renderer_arena = {};
    arena_init(&renderer_arena, megabytes(128));

    VulkanContext context = {};
    context.WindowDrawableAreaWidth = window_width;
    context.WindowDrawableAreaHeight = window_height;

    RendererInit(&context, window, &renderer_arena);

    GameMemory game_memory = {};
    game_memory.permanent_store_size = megabytes((uint64_t)256);
    game_memory.permanent_store = malloc(game_memory.permanent_store_size);

    game_memory.transient_store_size = gigabytes((uint64_t)2);
    game_memory.transient_store = malloc(game_memory.transient_store_size);

    uint64_t timer_frequency =
        SDL_GetPerformanceFrequency();  // counts per second

    // Main event loop
    SDL_Event event;
    GameInput input = {0};
    while (GLOBAL_running) {

        uint64_t ticks_start = SDL_GetPerformanceCounter();
        while (SDL_PollEvent(&event)) {
            handle_SDL_event(&event, &input, &context, &renderer_arena);
        }
        platform_reload_game_code(&gameCode, sourcePath);

        gameCode.gameUpdateAndRender(&game_memory, &input);

        GameState* game_state = (GameState*)(game_memory.permanent_store);
        RendererDrawFrame(&context, &renderer_arena,
                          &game_state->frame_push_buffer);

        uint64_t ticks_end = SDL_GetPerformanceCounter();
        uint64_t elapsed_ticks = ticks_end - ticks_start;
    }
}
