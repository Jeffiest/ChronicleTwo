#pragma once
#include <cstdio>
#include <SDL3/SDL.h>
#include "gfx/gfx.hpp"

struct Wp12Renderer {
    SDL_Window *window = nullptr;
    bool active = false;
    bool Init() {
        if (!SDL_Init(SDL_INIT_VIDEO)) return Skip();
        window = SDL_CreateWindow("Renderer contract", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) return Skip();
        gfx::RendererInit(window, gfx::RendererConfig{});
        active = gfx::PipelineCount() != 0;
        return active ? true : Skip();
    }
    bool Skip() { std::puts("SKIP: renderer contract requires a Vulkan device"); return false; }
    ~Wp12Renderer() {
        if (active) gfx::RendererShutdown();
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
    }
};
