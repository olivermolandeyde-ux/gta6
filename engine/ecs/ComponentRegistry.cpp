#include "ecs/ComponentRegistry.h"

// Template members live in the header. This TU exists for the static lib.
namespace engine {
u32 g_component_registry_anchor = 0;
}
