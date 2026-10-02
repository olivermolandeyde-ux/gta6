#include "render/SdlGlWindow.h"

#include <SDL.h>
#include <cstdio>
#include <cstring>

namespace engine {

bool SdlGlWindow::create(const char* title, int w, int h) {
    std::memset(keys, 0, sizeof(keys));
    shiftDown = false;
    mouseDeltaX = mouseDeltaY = 0.f;
    mouseCaptured = true;
    shouldClose = false;
    window = nullptr;
    gl_context = nullptr;
    width = w;
    height = h;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::printf("[sdl] SDL_Init failed: %s\n", SDL_GetError());
        std::fflush(stdout);
        return false;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
#if defined(__APPLE__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

    window = SDL_CreateWindow(title ? title : "Leonida", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w,
                              h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
    if (!window) {
        std::printf("[sdl] SDL_CreateWindow failed: %s\n", SDL_GetError());
        std::fflush(stdout);
        return false;
    }

    gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        std::printf("[sdl] SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        std::fflush(stdout);
        return false;
    }
    SDL_GL_MakeCurrent(window, static_cast<SDL_GLContext>(gl_context));
    if (SDL_GL_SetSwapInterval(1) != 0) {
        std::printf("[sdl] vsync request failed: %s\n", SDL_GetError());
    } else {
        std::printf("[sdl] vsync on (swap interval 1)\n");
    }
    SDL_SetRelativeMouseMode(SDL_TRUE);

    std::printf("[sdl] OpenGL 3.3 core window %dx%d\n", w, h);
    std::fflush(stdout);
    return true;
}

void SdlGlWindow::pollEvents() {
    mouseDeltaX = 0.f;
    mouseDeltaY = 0.f;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            shouldClose = true;
        } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) {
            shouldClose = true;
        } else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            const bool down = e.type == SDL_KEYDOWN;
            const SDL_Keycode k = e.key.keysym.sym;
            if (k >= 0 && k < 256) {
                keys[static_cast<u32>(k)] = down;
            }
            if (k == SDLK_ESCAPE && down) {
                mouseCaptured = !mouseCaptured;
                SDL_SetRelativeMouseMode(mouseCaptured ? SDL_TRUE : SDL_FALSE);
            }
        } else if (e.type == SDL_MOUSEMOTION && mouseCaptured) {
            mouseDeltaX += static_cast<float>(e.motion.xrel);
            mouseDeltaY += static_cast<float>(e.motion.yrel);
        }
    }
    const Uint32 mods = SDL_GetModState();
    shiftDown = (mods & KMOD_SHIFT) != 0;
    int nw = 0, nh = 0;
    SDL_GL_GetDrawableSize(window, &nw, &nh);
    if (nw > 0 && nh > 0) {
        width = nw;
        height = nh;
    }
}

void SdlGlWindow::swap() {
    if (window) {
        SDL_GL_SwapWindow(window);
    }
}

void SdlGlWindow::destroy() {
    if (gl_context) {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_context));
        gl_context = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

bool SdlGlWindow::isKeyDown(int keyCode) const {
    return keys[static_cast<u32>(keyCode) & 255u];
}

} // namespace engine
