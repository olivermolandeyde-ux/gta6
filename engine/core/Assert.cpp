#include "core/Assert.h"

// Translation unit exists so the static library always has a core object file.
// Assertions are header-inline for zero-cost in optimized builds that later
// strip ENGINE_ASSERT via a NDEBUG-style switch.
namespace engine {
u32 g_assert_anchor = 0;
}
