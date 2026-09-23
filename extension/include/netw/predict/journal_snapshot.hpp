#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/predict/journal.hpp"

namespace netw::predict {

int32_t fnv1a(const godot::PackedByteArray &p_bytes);

class JournalSnapshot {
    Journal journal;
    godot::Dictionary witness_details;

public:
    void adopt(
        const Journal &p_journal,
        const godot::Dictionary &p_witness_details
    );

    godot::Dictionary row_at(int64_t p_transition) const;
    godot::PackedInt64Array transitions() const;
    int64_t last_closed() const;
    int size() const;
    int64_t epoch() const;
};

} // namespace netw::predict
