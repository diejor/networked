#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

constexpr uint32_t SNAPSHOT_STAGE_DEPTH = 64;
constexpr uint32_t SNAPSHOT_RING_DEPTH = 64;
constexpr uint64_t SNAPSHOT_DELTA_SPAN = 32;
constexpr uint64_t SNAPSHOT_FIRST_REVISION = 1;

enum class ReceiptVerdict : uint8_t {
    PROMOTED,
    IGNORED,
    EVICTED,
    UNEXPOSED,
};

enum class RowAdmission : uint8_t {
    ACCEPTABLE,
    DUPLICATE,
    STALE,
};

class SnapshotSender {
    struct Staged {
        uint64_t revision = 0;
        CodeRow row;
        bool pinned = false;
    };

    godot::LocalVector<Staged> staged;
    CodeRow confirmed_row;
    CodeRow desired_row;
    uint64_t next_revision = SNAPSHOT_FIRST_REVISION;
    uint64_t confirmed_revision = 0;
    uint64_t highest_exposed = 0;
    uint64_t repair_at = 0;
    bool has_confirmed = false;
    bool has_desired = false;

    int32_t slot_of(uint64_t p_revision) const;
    void evict_to_depth();

public:
    void desire(const CodeRow &p_row);

    bool has_target() const {
        return has_desired;
    }

    const CodeRow &target() const {
        return desired_row;
    }

    uint64_t reserve();

    uint64_t distance_for(uint64_t p_revision) const;

    const CodeRow *confirmed() const {
        return has_confirmed ? &confirmed_row : nullptr;
    }

    uint64_t confirmed_at() const {
        return confirmed_revision;
    }

    uint64_t exposed_high_water() const {
        return highest_exposed;
    }

    bool expose(uint64_t p_revision, const CodeRow &p_row);

    bool pin(uint64_t p_revision);
    bool unpin(uint64_t p_revision);

    bool holds(uint64_t p_revision) const {
        return slot_of(p_revision) >= 0;
    }

    uint32_t staged_count() const {
        return staged.size();
    }

    ReceiptVerdict receipt(uint64_t p_revision);

    bool quiet() const;

    bool awaiting_receipt() const;

    bool withdraw(uint64_t p_revision);

    uint64_t pinned_repair();

    uint64_t repair_at_revision() const {
        return repair_at;
    }

    void reset();
};

class SnapshotReceiver {
    struct Held {
        uint64_t revision = 0;
        CodeRow row;
    };

    godot::LocalVector<Held> ring;
    CodeRow accepted_row;
    uint64_t accepted_revision = 0;
    bool has_accepted = false;

public:
    RowAdmission admits(uint64_t p_revision) const;

    const CodeRow *baseline(uint64_t p_revision) const;

    bool commit(uint64_t p_revision, const CodeRow &p_row);

    uint64_t accepted_at() const {
        return accepted_revision;
    }

    const CodeRow *accepted() const {
        return has_accepted ? &accepted_row : nullptr;
    }

    uint32_t ring_count() const {
        return ring.size();
    }

    void reset();
};

} // namespace netw::wire
