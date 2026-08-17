#pragma once

/* One decoded frame, before anything has been written to a node.
 *
 * Decoding and applying are separate because the apply can be refused: a
 * predicting client reconciles against the authoritative row instead of
 * snapping its body, and a seam may reject the write outright. A decoder that
 * wrote as it read would have spent the write before either could say no.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwStagedWrites : public godot::RefCounted {
    GDCLASS(NetwStagedWrites, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    int64_t ordinal = 0;

    // -1 for a frame that carries no authoring tick.
    int64_t tick = -1;

    // -1 is the no-input-consumed sentinel, never a stray 0.
    int64_t ack = -1;

    godot::Array keys;
    godot::Array values;

    godot::Dictionary row;

    // Whether this frame itself carried every field, rather than a subset
    // merged onto what was already there.
    bool whole = true;

    godot::Array samples;

    int64_t get_ordinal() const { return ordinal; }
    void set_ordinal(int64_t p_ordinal) { ordinal = p_ordinal; }
    int64_t get_tick() const { return tick; }
    void set_tick(int64_t p_tick) { tick = p_tick; }
    int64_t get_ack() const { return ack; }
    void set_ack(int64_t p_ack) { ack = p_ack; }
    godot::Array get_keys() const { return keys; }
    void set_keys(const godot::Array &p_keys) { keys = p_keys; }
    godot::Array get_values() const { return values; }
    void set_values(const godot::Array &p_values) { values = p_values; }
    godot::Dictionary get_row() const { return row; }
    void set_row(const godot::Dictionary &p_row) { row = p_row; }
    bool get_whole() const { return whole; }
    void set_whole(bool p_whole) { whole = p_whole; }
    godot::Array get_samples() const { return samples; }
    void set_samples(const godot::Array &p_samples) { samples = p_samples; }

    bool is_valid() const;

    godot::Dictionary header() const;
};

} // namespace netw
