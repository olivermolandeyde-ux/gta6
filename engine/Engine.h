#pragma once

#include "ecs/SystemScheduler.h"
#include "ecs/World.h"
#include "memory/MemorySystem.h"

namespace engine {

// Top-level simulation host. Boots memory first, then the World, then the
// system pipeline. There is no hidden singleton: the game owns this object.
class Engine {
public:
    void boot(const MemoryBudget& budget = {}, u16 world_id = 1) {
        memory_.boot(budget);
        world_.boot(memory_, world_id);
        frame_ = 0;
        booted_ = true;
    }

    void shutdown() {
        if (!booted_) {
            return;
        }
        world_.shutdown();
        memory_.shutdown();
        booted_ = false;
    }

    void tick(f32 dt) {
        ENGINE_ASSERT(booted_, "Engine::tick before boot");
        world_.begin_frame(frame_);
        scheduler_.run(world_, dt);
        ++frame_;
    }

    [[nodiscard]] MemorySystem&     memory() noexcept { return memory_; }
    [[nodiscard]] World&            world() noexcept { return world_; }
    [[nodiscard]] SystemScheduler&  scheduler() noexcept { return scheduler_; }
    [[nodiscard]] u64               frame() const noexcept { return frame_; }

private:
    MemorySystem    memory_{};
    World           world_{};
    SystemScheduler scheduler_{};
    u64             frame_  = 0;
    bool            booted_ = false;
};

} // namespace engine
