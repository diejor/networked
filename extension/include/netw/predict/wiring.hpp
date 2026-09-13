#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"

namespace netw::predict {

enum class StateFamily : int {
    POSE = 0,
    MOMENTUM = 1,
    CONTROLLER = 2,
};

enum class PropertyClass : int {
    CAUSAL = 0,
    DERIVED = 1,
    COSMETIC = 2,
};

struct FieldDecl {
    godot::StringName key;
    godot::StringName carry_channel;
    godot::Ref<NetwQuantize> quantizer;
    int property_class = int(PropertyClass::CAUSAL);
    int type = int(godot::Variant::NIL);
    double converge_stiffness = 0.0;
    double epsilon_override = -1.0;
    double teleport_at = -1.0;
    bool teleport_only = false;
    bool reconcile_only = false;
    bool angle = false;
};

struct FieldCodec {
    godot::LocalVector<godot::StringName> keys;
    godot::LocalVector<godot::Ref<NetwQuantize>> quantizers;
    godot::LocalVector<int> types;

    void append(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        int p_type
    );

    int count() const {
        return int(keys.size());
    }

    void clear();
};

godot::PackedByteArray canonical_bytes(
    const FieldCodec &p_codec,
    const godot::Dictionary &p_payload
);

godot::Dictionary canonicalize(
    const FieldCodec &p_codec,
    const godot::Dictionary &p_payload
);

godot::Dictionary zero_row(const FieldCodec &p_codec);

class FieldTable {
    godot::LocalVector<godot::StringName> names;
    godot::HashMap<godot::StringName, int> index;

public:
    void append(const godot::StringName &p_key);
    int index_of(const godot::StringName &p_key) const;
    const godot::StringName &name_at(int p_slot) const;

    int count() const {
        return int(names.size());
    }

    void clear();
};

struct Wiring {
    FieldTable fields;
    FieldCodec codec;

    godot::LocalVector<int> projection;
    godot::LocalVector<int> state_family;
    godot::LocalVector<double> converge_rate;
    godot::LocalVector<double> epsilon;
    godot::LocalVector<double> teleport;
    godot::LocalVector<uint8_t> pose;
    godot::LocalVector<uint8_t> withheld;
    godot::LocalVector<uint8_t> trigger_exclude;
    godot::LocalVector<uint8_t> vote_exclude;
    godot::LocalVector<uint8_t> angle;
    godot::LocalVector<uint8_t> causal;
    godot::LocalVector<int> property_class;
    godot::LocalVector<godot::StringName> carry_channel;

    void resize(int p_count);

    int count() const {
        return fields.count();
    }
};

Wiring compile(const godot::LocalVector<FieldDecl> &p_declaration);

struct Config {
    int schedule = 0;
    int role = 0;
    int correction = 0;
    int restore = 0;
    int max_restore_ticks = 6;
    int island = 0;
    bool island_declared = false;
    bool island_approximate = false;
    bool witness = false;
    double epsilon = 0.01;
    double teleport_threshold = 2.0;
    int collision_cooldown_ticks = 6;
};

} // namespace netw::predict
