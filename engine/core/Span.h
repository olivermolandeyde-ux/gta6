#pragma once

#include "core/Types.h"

namespace engine {

template <typename T>
struct Span {
    T*    data = nullptr;
    usize count = 0;

    [[nodiscard]] T*       begin() noexcept { return data; }
    [[nodiscard]] const T* begin() const noexcept { return data; }
    [[nodiscard]] T*       end() noexcept { return data + count; }
    [[nodiscard]] const T* end() const noexcept { return data + count; }
    [[nodiscard]] usize    size() const noexcept { return count; }
    [[nodiscard]] bool     empty() const noexcept { return count == 0; }
    [[nodiscard]] T&       operator[](usize i) noexcept { return data[i]; }
    [[nodiscard]] const T& operator[](usize i) const noexcept { return data[i]; }
};

} // namespace engine
