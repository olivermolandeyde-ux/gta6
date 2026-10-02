#include "ecs/Query.h"

#include "ecs/World.h"

namespace engine {

EntityQueryIterator::EntityQueryIterator(World* world, Query query, bool is_end)
    : world_(world), query_(query), end_(is_end) {
    if (!end_) {
        seek();
    }
}

void EntityQueryIterator::seek() {
    if (!world_) {
        end_ = true;
        return;
    }
    const u32 n = world_->archetype_count();
    while (arch_ < n) {
        Archetype& arch = world_->archetype_at(arch_);
        if (query_.matches(arch.signature)) {
            while (chunk_ < arch.chunk_count) {
                Chunk* chunk = arch.chunks[chunk_];
                const u16 count = chunk->header().count;
                if (row_ < count) {
                    value_ = chunk->entities()[row_];
                    end_   = false;
                    return;
                }
                ++chunk_;
                row_ = 0;
            }
        }
        ++arch_;
        chunk_ = 0;
        row_   = 0;
    }
    end_ = true;
}

EntityQueryIterator& EntityQueryIterator::operator++() {
    if (!end_) {
        ++row_;
        seek();
    }
    return *this;
}

bool EntityQueryIterator::operator!=(const EntityQueryIterator& other) const noexcept {
    if (end_ && other.end_) {
        return false;
    }
    return end_ != other.end_ || arch_ != other.arch_ || chunk_ != other.chunk_ || row_ != other.row_;
}

} // namespace engine
