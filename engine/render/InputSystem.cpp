#include "render/InputSystem.h"

#include <cstring>

namespace engine {

void InputSystem::update() {
    for (u32 i = 0; i < 256; ++i) {
        currentState.keyJustPressed[i] =
            currentState.keys[i] && !previousState.keys[i];
    }
    currentState.mouseDeltaX = currentState.mouseX - previousState.mouseX;
    currentState.mouseDeltaY = currentState.mouseY - previousState.mouseY;
    previousState = currentState;
}

bool InputSystem::isKeyDown(int keyCode) const {
    const u32 k = static_cast<u32>(keyCode) & 255u;
    return currentState.keys[k];
}

bool InputSystem::isKeyPressed(int keyCode) const {
    const u32 k = static_cast<u32>(keyCode) & 255u;
    return currentState.keyJustPressed[k];
}

float InputSystem::getMouseDeltaX() const {
    return currentState.mouseDeltaX;
}

float InputSystem::getMouseDeltaY() const {
    return currentState.mouseDeltaY;
}

void input_set_key(InputSystem& input, int keyCode, bool down) {
    const u32 k = static_cast<u32>(keyCode) & 255u;
    input.currentState.keys[k] = down;
}

void input_set_mouse(InputSystem& input, float x, float y, bool left_down) {
    input.currentState.mouseX = x;
    input.currentState.mouseY = y;
    input.currentState.mouseButtons[0] = left_down;
}

} // namespace engine
