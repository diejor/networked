#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/held_bytes.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

class CodeRow {
    godot::LocalVector<uint64_t> words;
    godot::LocalVector<godot::Variant> values;
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

    const uint64_t *word_data() const {
        return words.ptr();
    }

    const godot::Variant *value_data() const {
        return values.ptr();
    }

    int64_t held_bytes() const {
        return held::vector(words) + held::vector(values);
    }

    bool assign(
        const WirePlan &plan,
        const uint64_t *p_words,
        const godot::Variant *p_values
    );

    bool valid_for(const WirePlan &plan) const;
    godot::PackedByteArray to_bytes() const;

    void clear();
    void clear_values();

    void copy_from(const CodeRow &other);

    bool write(const ColumnPlan &slot, int element, uint64_t code);
    uint64_t read(const ColumnPlan &slot, int element) const;

    bool write_bits(int64_t offset, int width, uint64_t code);
    uint64_t read_bits(int64_t offset, int width) const;

    bool write_value(uint32_t column, const godot::Variant &value);
    godot::Variant read_value(uint32_t column) const;

    static uint64_t changed_mask(
        const WirePlan &plan,
        const CodeRow &before,
        const CodeRow &after
    );

    bool equals(const CodeRow &other) const;
};

} // namespace netw::wire
