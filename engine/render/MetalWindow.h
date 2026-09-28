#pragma once

#include "core/Types.h"

namespace engine {

struct MetalWindow {
    void* window;      // NSWindow* on Apple
    void* contentView; // NSView*
    void* metalLayer;  // CAMetalLayer*
    int   width;
    int   height;
    bool  shouldClose;
    int   listen_fd;   // Linux hosted preview socket
    u16   listen_port;
    const char* hosted_html_relpath;

    void  create(const char* title, int width, int height);
    void  pollEvents();
    void* getMetalLayer();
    void  destroy();
};

} // namespace engine
