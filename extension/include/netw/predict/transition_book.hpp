#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/wire/code_row.hpp"

namespace netw::predict {

uint64_t causal_scope(const Wiring &p_wiring);
uint64_t family_scope(const Wiring &p_wiring, StateFamily p_family);

int32_t fingerprint_codes(
    const FieldCodec &p_codec,
    const wire::CodeRow &p_row,
    uint64_t p_present,
    uint64_t p_coded,
    const godot::Variant *p_carried,
    uint64_t p_scope,
    bool &r_any
);

int32_t fingerprint_row(
    const FieldCodec &p_codec,
    const StateRow &p_row,
    uint64_t p_scope,
    wire::CodeRow &r_scratch,
    bool &r_any
);

class TransitionBook {
    uint32_t depth = DEPTH;
    uint32_t words = 0;
    uint32_t fields = 0;
    int64_t floor_key = -1;
    int64_t newest = -1;
    uint32_t held = 0;
    godot::LocalVector<int64_t> keys;
    godot::LocalVector<int64_t> labels;
    godot::LocalVector<uint8_t> fresh;
    godot::LocalVector<uint8_t> origins;
    godot::LocalVector<uint8_t> relays;
    godot::LocalVector<uint64_t> present;
    godot::LocalVector<uint64_t> coded;
    godot::LocalVector<uint64_t> images;
    godot::LocalVector<godot::Variant> carried;
    mutable wire::CodeRow scratch;
    mutable godot::LocalVector<godot::PackedInt64Array> element_codes;

    uint32_t at_of(int64_t p_key) const {
        return uint32_t(p_key) & (depth - 1);
    }

    bool open_entry(
        const FieldCodec &p_codec,
        int64_t p_key,
        int64_t p_label,
        bool p_fresh,
        uint8_t p_origin
    );
    void carry(uint32_t p_at, int p_field, const godot::Variant &p_value);
    bool code_field(
        const FieldCodec &p_codec,
        int p_field,
        const godot::Variant &p_value
    );
    void seal_entry(uint32_t p_at);
    bool field_value(
        const FieldCodec &p_codec,
        uint32_t p_at,
        int p_field,
        godot::Variant &r_value
    ) const;
    void release(uint32_t p_at);

public:
    static constexpr uint32_t DEPTH = 256;

    TransitionBook() = default;
    TransitionBook(const TransitionBook &p_other);
    TransitionBook &operator=(const TransitionBook &p_other);

    void reset(const FieldCodec &p_codec);
    void clear();
    void set_depth(uint32_t p_depth);

    uint32_t capacity() const {
        return depth;
    }

    bool record_row(
        const FieldCodec &p_codec,
        int64_t p_key,
        int64_t p_label,
        bool p_fresh,
        const StateRow &p_row,
        uint8_t p_origin = 0
    );

    bool holds(int64_t p_key) const;
    int64_t newest_at_or_before(int64_t p_key) const;
    int64_t newest_labeled_at_or_before(int64_t p_label) const;
    int64_t oldest_key() const;

    int64_t newest_key() const {
        return held == 0 ? -1 : newest;
    }

    int64_t label_of(int64_t p_key) const;
    bool fresh_at(int64_t p_key) const;
    int origin_of(int64_t p_key) const;
    void note_relayed(int64_t p_key);
    void forget_relays();
    bool relayed_at(int64_t p_key) const;
    bool carries_fields(int64_t p_key) const;
    const uint64_t *image_of(int64_t p_key) const;
    bool coded_whole(int64_t p_key) const;

    bool read_row(
        const FieldCodec &p_codec,
        int64_t p_key,
        StateRow &r_row
    ) const;
    int32_t fingerprint(
        const FieldCodec &p_codec,
        int64_t p_key,
        uint64_t p_scope,
        bool &r_any
    ) const;

    void drop_origin(uint8_t p_origin, bool p_matching);
    void trim_before(int64_t p_key);

    int64_t floor() const {
        return floor_key;
    }

    uint32_t count() const {
        return held;
    }

    int64_t byte_count() const;

    int64_t held_bytes() const {
        return netw::held::vector(keys) + netw::held::vector(labels)
            + netw::held::vector(fresh) + netw::held::vector(origins)
            + netw::held::vector(relays) + netw::held::vector(present)
            + netw::held::vector(coded) + netw::held::vector(images)
            + netw::held::vector(carried) + scratch.held_bytes()
            + netw::held::vector(element_codes);
    }
};

} // namespace netw::predict
