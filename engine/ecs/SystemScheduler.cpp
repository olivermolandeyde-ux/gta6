#include "ecs/SystemScheduler.h"

#include "core/Assert.h"
#include "ecs/World.h"

namespace engine {

void SystemScheduler::add(const SystemDesc& desc) {
    ENGINE_ASSERT(desc.fn != nullptr, "system function is null");
    ENGINE_ASSERT(count_ < kMaxSystems, "system cap reached");
    systems_[count_++] = desc;
    sorted_ = false;
}

void SystemScheduler::sort_stable() {
    // Insertion sort: N <= 128, must be stable so registration order inside
    // a stage is preserved (animation before foot IK, etc.).
    for (u32 i = 1; i < count_; ++i) {
        SystemDesc key = systems_[i];
        u32 j = i;
        while (j > 0 && static_cast<u32>(systems_[j - 1].stage) > static_cast<u32>(key.stage)) {
            systems_[j] = systems_[j - 1];
            --j;
        }
        systems_[j] = key;
    }
    sorted_ = true;
}

void SystemScheduler::run(World& world, f32 dt) {
    if (!sorted_) {
        sort_stable();
    }
    for (u32 i = 0; i < count_; ++i) {
        systems_[i].fn(world, dt);
    }
    world.frame_commands().playback(world);
}

} // namespace engine
