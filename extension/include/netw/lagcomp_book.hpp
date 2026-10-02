#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/wire/code_row.hpp"

namespace netw {

class LagBook {
    predict::FieldCodec codec;
    bool compiled = false;
    uint32_t window = DEFAULT_WINDOW;
    uint32_t words = 0;
    uint32_t fields = 0;
    int64_t floor_tick = -1;

    godot::LocalVector<int64_t> ticks;
    godot::LocalVector<uint64_t> present;
    godot::LocalVector<uint64_t> coded;
    godot::LocalVector<uint64_t> images;
    godot::LocalVector<godot::Variant> carried;
    uint32_t head = 0;
    uint32_t held = 0;

    godot::LocalVector<int64_t> recorded;
    uint32_t recorded_head = 0;
    uint32_t recorded_held = 0;

    wire::CodeRow scratch;
    godot::LocalVector<godot::Variant> staged;
    uint64_t staged_present = 0;
    uint64_t staged_coded = 0;
    mutable godot::LocalVector<godot::PackedInt64Array> element_codes;
    mutable wire::CodeRow reader;

    uint32_t slot_of_age(uint32_t p_age) const {
        return (head + held - 1 - p_age) & (window - 1);
    }

    uint32_t slot_at(uint32_t p_index) const {
        return (head + p_index) & (window - 1);
    }

    void size_rings();
    void copy_entry(uint32_t p_to, uint32_t p_from);
    int64_t oldest_recorded() const;
    void stage(const godot::Dictionary &p_payload);
    bool staged_equals(uint32_t p_at) const;
    void write_staged(uint32_t p_at, int64_t p_tick);
    void note_recorded(int64_t p_tick);
    int find_sample(int64_t p_tick) const;

public:
    static constexpr uint32_t DEFAULT_WINDOW = 64;

    LagBook() = default;
    LagBook(const LagBook &) = delete;
    LagBook &operator=(const LagBook &) = delete;

    void set_window(int64_t p_samples);

    uint32_t get_window() const {
        return window;
    }

    void compile(
        const predict::FieldCodec &p_source,
        const godot::LocalVector<godot::StringName> &p_declared
    );
    void compile_keys(const godot::Array &p_keys);

    bool is_compiled() const {
        return compiled;
    }

    bool record(int64_t p_tick, const godot::Dictionary &p_payload);

    int64_t sample_tick(int64_t p_tick) const;
    godot::Dictionary sample(int64_t p_tick) const;
    bool recorded_at(int64_t p_tick) const;

    void trim_before(int64_t p_tick);

    int64_t floor() const {
        return floor_tick;
    }

    uint32_t samples() const {
        return held;
    }

    int64_t newest_tick() const;
    int64_t byte_count() const;
};

} // namespace netw
