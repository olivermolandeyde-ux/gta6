#pragma once

#include "core/Types.h"

namespace engine {

struct InputState {
    bool  keys[256];
    bool  mouseButtons[5];
    float mouseX;
    float mouseY;
    float mouseDeltaX;
    float mouseDeltaY;
    bool  keyJustPressed[256];
};

struct InputSystem {
    InputState currentState;
    InputState previousState;

    void update();
    [[nodiscard]] bool  isKeyDown(int keyCode) const;
    [[nodiscard]] bool  isKeyPressed(int keyCode) const;
    [[nodiscard]] float getMouseDeltaX() const;
    [[nodiscard]] float getMouseDeltaY() const;
};

void input_set_key(InputSystem& input, int keyCode, bool down);
void input_set_mouse(InputSystem& input, float x, float y, bool left_down);

} // namespace engine
