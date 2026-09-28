#include "render/MetalWindow.h"

#include "core/Assert.h"

#ifndef LEONIDA_SOURCE_DIR
#define LEONIDA_SOURCE_DIR "."
#endif

#include <cstdio>
#include <cstring>

#if defined(__APPLE__)
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace engine {

#if defined(__APPLE__)

void MetalWindow::create(const char* title, int w, int h) {
    width = w;
    height = h;
    shouldClose = false;
    listen_fd = -1;
    listen_port = 0;

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSRect rect = NSMakeRect(80, 80, w, h);
    NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                       NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    NSWindow* ns = [[NSWindow alloc] initWithContentRect:rect
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    [ns setTitle:[NSString stringWithUTF8String:title ? title : "Leonida"]];
    NSView* view = [[NSView alloc] initWithFrame:rect];
    [view setWantsLayer:YES];
    CAMetalLayer* layer = [CAMetalLayer layer];
    layer.device = MTLCreateSystemDefaultDevice();
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.drawableSize = CGSizeMake(w, h);
    view.layer = layer;
    [ns setContentView:view];
    [ns makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    window = (__bridge_retained void*)ns;
    contentView = (__bridge_retained void*)view;
    metalLayer = (__bridge_retained void*)layer;
}

void MetalWindow::pollEvents() {
    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                        untilDate:[NSDate distantPast]
                                           inMode:NSDefaultRunLoopMode
                                          dequeue:YES];
    if (event) {
        [NSApp sendEvent:event];
    }
    NSWindow* ns = (__bridge NSWindow*)window;
    if (ns && ![ns isVisible]) {
        shouldClose = true;
    }
}

void* MetalWindow::getMetalLayer() {
    return metalLayer;
}

void MetalWindow::destroy() {
    if (window) {
        CFRelease(window);
        window = nullptr;
    }
}

#else

namespace {

constexpr u32 kHtmlMax = 256u * 1024u;

u32 load_html(char* dst, u32 cap, const char* rel) {
    const char* use = rel && rel[0] ? rel : "sandbox/visual_awakening.html";
    char abs_path[512];
    std::snprintf(abs_path, sizeof(abs_path), "%s/%s", LEONIDA_SOURCE_DIR, use);
    const char* paths[] = {use, abs_path, nullptr};
    for (u32 i = 0; paths[i]; ++i) {
        FILE* f = std::fopen(paths[i], "rb");
        if (!f) {
            continue;
        }
        const usize n = std::fread(dst, 1, cap - 1, f);
        std::fclose(f);
        dst[n] = 0;
        return static_cast<u32>(n);
    }
    const char* fallback =
        "<html><body style='background:#111;color:#eee;font-family:sans-serif'>"
        "<h1>Leonida Terrain & Sky</h1><p>Host is up.</p></body></html>";
    const u32 n = static_cast<u32>(std::strlen(fallback));
    std::memcpy(dst, fallback, n + 1);
    return n;
}

void serve_one(int client, const char* html, u32 html_len) {
    char header[256];
    const int hlen = std::snprintf(header, sizeof(header),
                                   "HTTP/1.1 200 OK\r\n"
                                   "Content-Type: text/html; charset=utf-8\r\n"
                                   "Content-Length: %u\r\n"
                                   "Cache-Control: no-store\r\n"
                                   "Connection: close\r\n\r\n",
                                   html_len);
    (void)send(client, header, hlen, 0);
    (void)send(client, html, static_cast<int>(html_len), 0);
}

} // namespace

void MetalWindow::create(const char* title, int w, int h) {
    (void)title;
    width = w;
    height = h;
    shouldClose = false;
    window = nullptr;
    contentView = nullptr;
    metalLayer = nullptr;
    if (!hosted_html_relpath) {
        hosted_html_relpath = "sandbox/visual_awakening.html";
    }
    listen_port = 8080;

    listen_fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    ENGINE_ASSERT(listen_fd >= 0, "visual socket");
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(listen_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        listen_port = 18080;
        addr.sin_port = htons(listen_port);
        ENGINE_ASSERT(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0,
                      "bind visual port");
    }
    ENGINE_ASSERT(::listen(listen_fd, 16) == 0, "listen visual");
    const int flags = fcntl(listen_fd, F_GETFL, 0);
    fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    std::printf("[visual] hosting on 0.0.0.0:%u\n", listen_port);
    std::fflush(stdout);
}

void MetalWindow::pollEvents() {
    if (listen_fd < 0) {
        return;
    }
    sockaddr_in cli{};
    socklen_t sl = sizeof(cli);
    const int c = static_cast<int>(
        ::accept(listen_fd, reinterpret_cast<sockaddr*>(&cli), &sl));
    if (c < 0) {
        return;
    }
    char req[1024];
    (void)recv(c, req, sizeof(req) - 1, 0);
    char html[kHtmlMax];
    const u32 n = load_html(html, kHtmlMax, hosted_html_relpath);
    serve_one(c, html, n);
    ::close(c);
}

void* MetalWindow::getMetalLayer() {
    return metalLayer;
}

void MetalWindow::destroy() {
    if (listen_fd >= 0) {
        ::close(listen_fd);
        listen_fd = -1;
    }
}

#endif

} // namespace engine
