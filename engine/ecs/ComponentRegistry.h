#pragma once

#include "ecs/Component.h"

namespace engine {

class ComponentRegistry {
public:
    [[nodiscard]] u32 registered_count() const noexcept { return count_; }

    template <typename T>
    [[nodiscard]] u32 id_of() {
        static u32 cached = kInvalidIndex;
        if (cached != kInvalidIndex) {
            ENGINE_ASSERT(cached < count_, "stale component id after registry reset");
            return cached;
        }
        cached = register_type<T>();
        return cached;
    }

    [[nodiscard]] const ComponentType& type(u32 id) const {
        ENGINE_ASSERT(id < count_, "component id out of range");
        return types_[id];
    }

    void reset() noexcept { count_ = 0; }

private:
    template <typename T>
    u32 register_type() {
        using Traits = ComponentTraits<T>;
        ENGINE_ASSERT(count_ < kMaxComponents, "component type cap reached");

        ComponentType& t = types_[count_];
        t.id        = count_;
        t.size      = Traits::size;
        t.alignment = Traits::alignment;
        t.hash      = Traits::hash;
        t.name      = type_name_pretty<T>();
        t.construct = &Traits::construct;
        t.destroy   = &Traits::destroy;
        t.move      = &Traits::move;
        t.copy      = &Traits::copy;

        const u32 id = count_;
        ++count_;
        return id;
    }

    ComponentType types_[kMaxComponents]{};
    u32           count_ = 0;
};

} // namespace engine
