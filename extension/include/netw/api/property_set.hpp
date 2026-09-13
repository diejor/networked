#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/quantize.hpp"
#include "netw/table/schema_record.hpp"

namespace netw {

using table::SchemaColumn;
using table::SchemaRecord;

class NetwPropertySetColumn : public godot::RefCounted {
    GDCLASS(NetwPropertySetColumn, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum Delta {
        DELTA_AUTO = 0,
        DELTA_FULL = 1,
        DELTA_LADDER = 2,
    };

    int64_t schema_column = -1;
    SchemaColumn shape;
    int64_t delta_mode = DELTA_AUTO;
    bool watch = false;
    int64_t lane = 0;
    int64_t property_class = 0;
    double converge_stiffness = 0.0;
    godot::StringName carry_channel;
    bool explicit_teleport_only = false;
    bool explicit_reconcile_only = false;
    double epsilon_override = -1.0;
    double teleport_at_override = -1.0;

    static godot::Ref<NetwPropertySetColumn> create(
        const godot::StringName &p_key,
        const godot::Ref<NetwQuantize> &p_quantizer,
        bool p_watch,
        int64_t p_type
    );

    godot::StringName get_key() const;
    int64_t get_type() const;
    godot::Ref<NetwQuantize> get_quantizer() const;
    void set_quantizer(const godot::Ref<NetwQuantize> &p_quantizer);

    int64_t get_schema_column() const {
        return schema_column;
    }
    void set_schema_column(int64_t p_value) {
        schema_column = p_value;
    }
    int64_t get_delta_mode() const {
        return delta_mode;
    }
    void set_delta_mode(int64_t p_value) {
        delta_mode = p_value;
    }
    bool get_watch() const {
        return watch;
    }
    void set_watch(bool p_value) {
        watch = p_value;
    }
    int64_t get_lane() const {
        return lane;
    }
    void set_lane(int64_t p_value) {
        lane = p_value;
    }
    int64_t get_property_class() const {
        return property_class;
    }
    void set_property_class(int64_t p_value) {
        property_class = p_value;
    }
    double get_converge_stiffness() const {
        return converge_stiffness;
    }
    void set_converge_stiffness(double p_value) {
        converge_stiffness = p_value;
    }
    godot::StringName get_carry_channel() const {
        return carry_channel;
    }
    void set_carry_channel(const godot::StringName &p_value) {
        carry_channel = p_value;
    }
    bool get_explicit_teleport_only() const {
        return explicit_teleport_only;
    }
    void set_explicit_teleport_only(bool p_value) {
        explicit_teleport_only = p_value;
    }
    bool get_explicit_reconcile_only() const {
        return explicit_reconcile_only;
    }
    void set_explicit_reconcile_only(bool p_value) {
        explicit_reconcile_only = p_value;
    }
    double get_epsilon_override() const {
        return epsilon_override;
    }
    void set_epsilon_override(double p_value) {
        epsilon_override = p_value;
    }
    double get_teleport_at_override() const {
        return teleport_at_override;
    }
    void set_teleport_at_override(double p_value) {
        teleport_at_override = p_value;
    }
};

class NetwPropertySet : public godot::RefCounted {
    GDCLASS(NetwPropertySet, godot::RefCounted)

protected:
    static void _bind_methods();

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

    enum Trigger {
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

    SchemaRecord schema;
    godot::TypedArray<NetwPropertySetColumn> columns;
    int64_t cadence = TICK;
    int64_t profile = PLAIN;
    int64_t trigger = TRIGGER_TICK;
    int64_t stamp = STAMP_NONE;
    int64_t record = RECORD_NONE;
    int64_t window = 0;
    int64_t audience = AUDIENCE_PUBLIC;
    bool masked = false;
    NetwMemberConfig::Policy policy = NetwMemberConfig::POLICY_AUTHORITY;
    int64_t channel = -1;
    bool reliable = true;
    godot::RID rid;
    bool sealed = false;

    const SchemaRecord &get_volatile_schema();
    const SchemaRecord &get_retained_schema();

    void compile_against(godot::Node *p_node);
    godot::Ref<NetwPropertySetColumn> bind_column(
        const godot::Ref<NetwPropertySetColumn> &p_column
    );
    void reproject_lanes();
    godot::Ref<NetwPropertySetColumn> member(int64_t p_schema_column) const;
    godot::TypedArray<godot::StringName> keys() const;
    godot::Array quantizers() const;
    int64_t wire_hash() const;
    int64_t identity_hash() const;
    void seal();

    table::DeltaMode delta_of(
        const godot::Ref<NetwPropertySetColumn> &p_column
    ) const;

    static int64_t column_type_for(
        const godot::Ref<godot::Script> &p_script,
        godot::Node *p_node,
        const godot::StringName &p_property
    );
    static void stamp_column_types(
        const godot::Ref<NetwPropertySet> &p_set,
        const godot::Ref<godot::Script> &p_script,
        godot::Node *p_node
    );

    const SchemaRecord &get_schema() const {
        return schema;
    }
    void set_schema(const SchemaRecord &p_value) {
        schema = p_value;
    }
    godot::TypedArray<NetwPropertySetColumn> get_columns() const {
        return columns;
    }
    void set_columns(const godot::TypedArray<NetwPropertySetColumn> &p_value) {
        columns = p_value;
    }
    int64_t get_cadence() const {
        return cadence;
    }
    void set_cadence(int64_t p_value) {
        cadence = p_value;
    }
    int64_t get_profile() const {
        return profile;
    }
    void set_profile(int64_t p_value) {
        profile = p_value;
    }
    int64_t get_trigger() const {
        return trigger;
    }
    void set_trigger(int64_t p_value) {
        trigger = p_value;
    }
    int64_t get_stamp() const {
        return stamp;
    }
    void set_stamp(int64_t p_value) {
        stamp = p_value;
    }
    int64_t get_record() const {
        return record;
    }
    void set_record(int64_t p_value) {
        record = p_value;
    }
    int64_t get_window() const {
        return window;
    }
    void set_window(int64_t p_value) {
        window = p_value;
    }
    int64_t get_audience() const {
        return audience;
    }
    void set_audience(int64_t p_value) {
        audience = p_value;
    }
    bool get_masked() const {
        return masked;
    }
    void set_masked(bool p_value) {
        masked = p_value;
    }
    NetwMemberConfig::Policy get_policy() const {
        return policy;
    }
    void set_policy(NetwMemberConfig::Policy p_value) {
        policy = p_value;
    }
    int64_t get_channel() const {
        return channel;
    }
    void set_channel(int64_t p_value) {
        channel = p_value;
    }
    bool get_reliable() const {
        return reliable;
    }
    void set_reliable(bool p_value) {
        reliable = p_value;
    }
    godot::RID get_rid_handle() const {
        return rid;
    }
    void set_rid_handle(const godot::RID &p_value) {
        rid = p_value;
    }
    bool get_sealed() const {
        return sealed;
    }
    void set_sealed(bool p_value) {
        sealed = p_value;
    }

private:
    SchemaRecord volatile_schema;
    SchemaRecord retained_schema;
    bool lanes_projected = false;
    int64_t sealed_wire_hash = 0;

    void project_lanes();
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPropertySetColumn::Delta);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Cadence);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Profile);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Trigger);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Stamp);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Record);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Lane);
VARIANT_ENUM_CAST(netw::NetwPropertySet::PropertyClass);
VARIANT_ENUM_CAST(netw::NetwPropertySet::Audience);
