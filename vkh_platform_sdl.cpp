#define kilobytes(n) ((n) * 1024LL)
#define megabytes(n) (kilobytes(n) * 1024LL)
#define gigabytes(n) (megabytes(n) * 1024LL)

#include "vkh_game.h"

#ifdef VKH_DEBUG
#define assert(expr) \
    if (!(expr)) { \
        __builtin_trap();\
    }
#else
#define assert(expr)
#endif

#include "vkh_memory.cpp"
#include "vkh_renderer.cpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>

bool GLOBAL_running = true;

struct GameCode {
#if SDL_PLATFORM_WINDOWS
    const char* sourcePath = "vkh_game.dll";
    const char *newpath = "gamecopy.dll";
#else
    const char *sourcePath = "./build/vkh_game.so";
    const char *newpath = "./build/game_copy.so";
#endif
    SDL_SharedObject* so_handle;
    game_update_t gameUpdateAndRender;
    SDL_Time lastModified;
};

void platform_free_game_code(GameCode* gameCode) {
    SDL_UnloadObject(gameCode->so_handle);
}

void platform_load_game_code(GameCode* gc) {
    SDL_CopyFile(gc->sourcePath, gc->newpath);
    gc->so_handle = SDL_LoadObject(gc->newpath);
    assert(gc->so_handle);

    gc->gameUpdateAndRender = (game_update_t)SDL_LoadFunction(
        gc->so_handle, "game_update_and_render");
    assert(gc->gameUpdateAndRender);
}

void platform_reload_game_code(GameCode* gameCode) {

    SDL_PathInfo info;
    SDL_GetPathInfo(gameCode->sourcePath, &info);

    if (info.modify_time > gameCode->lastModified) {
        gameCode->lastModified = info.modify_time;
        platform_free_game_code(gameCode);
        platform_load_game_code(gameCode);
        fprintf(stderr, "Game code reloaded\n");
    }
}

void handle_SDL_event(SDL_Event* event, GameInput* input,
                      VulkanContext* renderer_context,
                      MemoryArena* renderer_arena) {
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
            printf("Window resized: width: %d, height: %d\n",
                   event->window.data1, event->window.data2);
            renderer_context->WindowDrawableAreaWidth = event->window.data1;
            renderer_context->WindowDrawableAreaHeight = event->window.data2;
            RecreateSwapchainResources(renderer_context, renderer_arena);
        } break;
        case SDL_EVENT_MOUSE_MOTION: {
            // printf("Mouse moved: x: %f y: %f, xrel: %f, yrel: %f\n",
            // event->motion.x, event->motion.y, event->motion.xrel,
            // event->motion.yrel);
            input->mouse_x = event->motion.x;
            input->mouse_y = event->motion.y;
        } break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            // printf("Mouse button down: button: %d, x: %f, y: %f\n",
            // event->button.button, event->button.x, event->button.y);
        } break;
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            // printf("Mouse button up: button: %d, x: %f, y: %f\n",
            // event->button.button, event->button.x, event->button.y);
        } break;
    }
}

int main(int argc, char** argv) {
    int window_width = 800;
    int window_height = 600;

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window* window =
        SDL_CreateWindow("Vulkan Heart", window_width, window_height,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    assert(window);
    float window_pixel_density = SDL_GetWindowPixelDensity(window);
    printf("Window pixel density: %f\n", window_pixel_density);
    SDL_SetWindowResizable(window, true);

    GameCode gameCode;
    SDL_PathInfo info;
    SDL_GetPathInfo(gameCode.sourcePath, &info);
    gameCode.lastModified = info.modify_time;
    platform_load_game_code(&gameCode);

    MemoryArena renderer_arena = {};
    arena_init(&renderer_arena, megabytes(128));

    VulkanContext context = {};
    context.WindowDrawableAreaWidth = window_width;
    context.WindowDrawableAreaHeight = window_height;
    context.WindowPixelDensity = window_pixel_density;

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
    input.window_pixel_density = SDL_GetWindowPixelDensity(window);

    while (GLOBAL_running) {
        uint64_t ticks_start = SDL_GetPerformanceCounter();
        while (SDL_PollEvent(&event)) {
            handle_SDL_event(&event, &input, &context, &renderer_arena);
        }
        platform_reload_game_code(&gameCode);

        gameCode.gameUpdateAndRender(&game_memory, &input);

        GameState* game_state = (GameState*)(game_memory.permanent_store);
        RendererDrawFrame(&context, &renderer_arena,
                          &game_state->frame_push_buffer);

        uint64_t ticks_end = SDL_GetPerformanceCounter();
        uint64_t elapsed_ticks = ticks_end - ticks_start;
    }
}
