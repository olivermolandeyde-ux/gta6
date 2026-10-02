#pragma once

#include "core/Types.h"

namespace engine {

class World;

// Systems are function pointers with a stage tag. No System base class, no
// virtual tick(), no RTTI dispatch. The scheduler is a stable sort by stage
// then registration order.
enum class PipelineStage : u32 {
    PreSimulation = 0,
    Simulation    = 1,
    Animation     = 2,
    Physics       = 3,
    PostPhysics   = 4,
    Presentation  = 5,
    Count
};

using SystemFn = void (*)(World& world, f32 dt);

struct SystemDesc {
    const char*    name  = "unnamed_system";
    PipelineStage  stage = PipelineStage::Simulation;
    SystemFn       fn    = nullptr;
};

inline constexpr u32 kMaxSystems = 128;

class SystemScheduler {
public:
    void add(const SystemDesc& desc);
    void run(World& world, f32 dt);

    [[nodiscard]] u32 system_count() const noexcept { return count_; }

private:
    SystemDesc systems_[kMaxSystems]{};
    u32        count_ = 0;
    bool       sorted_ = false;

    void sort_stable();
};

} // namespace engine
