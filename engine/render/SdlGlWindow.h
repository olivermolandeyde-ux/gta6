#pragma once

#include "core/Types.h"

struct SDL_Window;

namespace engine {

// SDL2 + OpenGL 3.3 core window. Not a generic Renderer — only windowing/input.
struct SdlGlWindow {
    SDL_Window* window;
    void*       gl_context; // SDL_GLContext
    int         width;
    int         height;
    bool        shouldClose;
    bool        keys[256];
    bool        shiftDown;
    float       mouseDeltaX;
    float       mouseDeltaY;
    bool        mouseCaptured;

    bool create(const char* title, int width, int height);
    void pollEvents();
    void swap();
    void destroy();
    [[nodiscard]] bool isKeyDown(int keyCode) const;
};

} // namespace engine
