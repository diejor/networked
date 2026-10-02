#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/quantize.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/slot_book.hpp"
#include "netw/table/schema_record.hpp"

namespace netw::repl {

struct PropertyColumn {
    enum Delta {
        DELTA_AUTO = 0,
        DELTA_FULL = 1,
        DELTA_LADDER = 2,
    };

    int64_t schema_column = -1;
    table::SchemaColumn shape;
    Delta delta_mode = DELTA_AUTO;
    bool watch = false;
    int64_t lane = 0;
    int64_t property_class = 0;
    double converge_stiffness = 0.0;
    godot::StringName carry_channel;
    bool explicit_teleport_only = false;
    bool explicit_reconcile_only = false;
    double epsilon_override = -1.0;
    double teleport_at_override = -1.0;

    static PropertyColumn create(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        bool p_watch,
        enums::NetwMultiplayer::ColumnType p_type
    );

    const godot::StringName &get_key() const {
        return shape.key;
    }
    enums::NetwMultiplayer::ColumnType get_type() const {
        return enums::NetwMultiplayer::ColumnType(shape.type);
    }
    godot::Ref<NetwQuantize> get_quantizer() const {
        return shape.quantizer;
    }
    void set_quantizer(const godot::Ref<NetwQuantize> &p_quantizer) {
        shape.quantizer = p_quantizer;
    }
    int64_t get_lane() const {
        return lane;
    }
    void set_lane(int64_t p_value) {
        lane = p_value;
    }
    bool get_watch() const {
        return watch;
    }
    void set_watch(bool p_value) {
        watch = p_value;
    }
};

class PropertySet {
public:
    enum Cadence {
        TICK = 0,
        ON_DEMAND = 1,
        ON_CHANGE = 2,
    };

    enum Profile {
        PLAIN = 0,
        STAMPED = 1,
    };

    enum Trigger : int64_t {
        TRIGGER_TICK = 0,
        TRIGGER_ON_CHANGE = 1,
        TRIGGER_ON_DEMAND = 2,
    };

    enum Stamp {
        STAMP_NONE = 0,
        STAMP_TICK = 1,
        STAMP_TICK_ACK = 2,
    };

    enum Record {
        RECORD_NONE = 0,
        RECORD_STATE = 1,
        RECORD_INPUT = 2,
        RECORD_BROADCAST = 3,
    };

    enum Lane {
        VOLATILE = 0,
        RETAINED = 1,
    };

    enum PropertyClass {
        CAUSAL = 0,
        DERIVED = 1,
        COSMETIC = 2,
    };

    enum Audience {
        AUDIENCE_PUBLIC = 0,
        AUDIENCE_SERVER_ONLY = 1,
    };

    PropertySet();

    const uint64_t id;
    table::SchemaRecord schema;
    godot::LocalVector<PropertyColumn> columns;
    int64_t cadence = TICK;
    int64_t profile = PLAIN;
    int64_t trigger = TRIGGER_TICK;
    int64_t stamp = STAMP_NONE;
    int64_t record = RECORD_NONE;
    int64_t window = 0;
    int64_t heartbeat = 0;
    int64_t audience = AUDIENCE_PUBLIC;
    bool masked = false;
    NetwMemberConfig::Policy policy = NetwMemberConfig::POLICY_AUTHORITY;
    int64_t channel = -1;
    bool reliable = true;
    godot::RID rid;
    bool sealed = false;

    const table::SchemaRecord &get_volatile_schema();
    const table::SchemaRecord &get_retained_schema();

    void compile_against(godot::Node *p_node);
    PropertyColumn &bind_column(const PropertyColumn &p_column);
    void reproject_lanes();
    const PropertyColumn *member(int64_t p_schema_column) const;
    const PropertyColumn *column_named(const godot::StringName &p_key) const;
    int64_t index_of(const PropertyColumn *p_column) const;
    godot::TypedArray<godot::StringName> keys() const;
    godot::Array quantizers() const;
    int64_t wire_hash() const;
    int64_t identity_hash() const;
    void seal();
    void stamp_column_types(
        const godot::Ref<godot::Script> &p_script,
        godot::Node *p_node
    );

    table::DeltaMode delta_of(const PropertyColumn &p_column) const;

    static enums::NetwMultiplayer::ColumnType column_type_for(
        const godot::Ref<godot::Script> &p_script,
        godot::Node *p_node,
        const godot::StringName &p_property
    );

    int64_t get_cadence() const {
        return cadence;
    }
    int64_t get_profile() const {
        return profile;
    }
    int64_t get_trigger() const {
        return trigger;
    }
    int64_t get_stamp() const {
        return stamp;
    }
    int64_t get_record() const {
        return record;
    }
    int64_t get_window() const {
        return window;
    }
    int64_t get_heartbeat() const {
        return heartbeat;
    }
    int64_t get_audience() const {
        return audience;
    }
    bool get_masked() const {
        return masked;
    }
    NetwMemberConfig::Policy get_policy() const {
        return policy;
    }
    int64_t get_channel() const {
        return channel;
    }
    bool get_reliable() const {
        return reliable;
    }
    const godot::RID &get_rid_handle() const {
        return rid;
    }
    bool get_sealed() const {
        return sealed;
    }

    const predict::FieldCodec &get_codec();

private:
    table::SchemaRecord volatile_schema;
    table::SchemaRecord retained_schema;
    predict::FieldCodec codec;
    bool lanes_projected = false;
    int64_t sealed_wire_hash = 0;

    void project_lanes();
};

using SetBook = SlotBook<PropertySet>;

} // namespace netw::repl
