#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

class CodeRow {
    godot::LocalVector<uint64_t> words;
    int64_t used_bits = 0;

public:
    static CodeRow for_plan(const WirePlan &plan);
    static CodeRow from_bytes(
        const WirePlan &plan,
        const godot::PackedByteArray &bytes
    );

    uint32_t word_count() const {
        return words.size();
    }

    bool is_empty() const {
        return words.is_empty();
    }

    bool valid_for(const WirePlan &plan) const;
    godot::PackedByteArray to_bytes() const;

    void clear();

    void copy_from(const CodeRow &other);

    bool write(const ColumnPlan &slot, int element, uint64_t code);
    uint64_t read(const ColumnPlan &slot, int element) const;

    bool write_bits(int64_t offset, int width, uint64_t code);
    uint64_t read_bits(int64_t offset, int width) const;

    static uint64_t changed_mask(
        const WirePlan &plan,
        const CodeRow &before,
        const CodeRow &after
    );

    bool equals(const CodeRow &other) const;
};

} // namespace netw::wire
