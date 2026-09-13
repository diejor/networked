#include "netw/property_set_builder.hpp"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/log.hpp"
#include "netw/script/model.hpp"
#include "netw/subsystems.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace netw {

namespace {

uint8_t declared_channel(const char *p_name) {
    static const wire::WireRegistry declared
        = wire::WireRegistry::create_default();
    const wire::ChannelDecl *found
        = declared.find_channel_by_name(StringName(p_name));
    return found != nullptr ? found->id : 0;
}

String record_kind_name(int64_t p_record) {
    if (p_record == NetwPropertySet::RECORD_INPUT) {
        return String("input");
    }
    if (p_record == NetwPropertySet::RECORD_BROADCAST) {
        return String("broadcast");
    }
    return String("state");
}

void warn_set_conflict(
    int64_t p_record,
    const char *p_axis,
    const StringName &p_first,
    const StringName &p_second
) {
    NETW_WARN(
        sys::WIRE,
        "NetwPropertySet: %s set-level %s set by both '%s' and '%s'",
        String(record_kind_name(p_record)).utf8().get_data(),
        p_axis,
        String(p_first).utf8().get_data(),
        String(p_second).utf8().get_data()
    );
}

void warn_broadcast_ignored(const char *p_axis, const StringName &p_property) {
    NETW_WARN(
        sys::WIRE,
        "NetwPropertySet: broadcast set forces %s; ignoring the ask on '%s'",
        p_axis,
        String(p_property).utf8().get_data()
    );
}

void warn_broadcast_exclusivity(const StringName &p_property) {
    NETW_WARN(
        sys::WIRE,
        "NetwPropertySet: '%s' marked broadcast() and state()/input(); "
        "dropping the broadcast mark in favor of the recorded set",
        String(p_property).utf8().get_data()
    );
}

} // namespace

namespace property_set_builder {

Ref<NetwPropertySet> for_record(int64_t p_record) {
    Ref<NetwPropertySet> set;
    set.instantiate();
    set->record = p_record;
    set->profile = NetwPropertySet::STAMPED;
    set->stamp = p_record == NetwPropertySet::RECORD_STATE
        ? NetwPropertySet::STAMP_TICK_ACK
        : NetwPropertySet::STAMP_TICK;
    set->cadence = NetwPropertySet::TICK;
    set->channel = declared_channel("SYNC");
    set->policy = p_record == NetwPropertySet::RECORD_STATE
        ? NetwMemberConfig::POLICY_AUTHORITY
        : NetwMemberConfig::POLICY_CONTROLLER;
    set->trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    set->window = p_record == NetwPropertySet::RECORD_INPUT ? 2 : 0;
    set->audience = p_record == NetwPropertySet::RECORD_INPUT
        ? NetwPropertySet::AUDIENCE_SERVER_ONLY
        : NetwPropertySet::AUDIENCE_PUBLIC;
    return set;
}

Ref<NetwPropertySet> from_property_config(
    const StringName &p_property,
    const Ref<NetwMemberConfig> &p_config
) {
    Ref<NetwPropertySet> set;
    set.instantiate();
    set->cadence = NetwPropertySet::ON_DEMAND;
    set->trigger = NetwPropertySet::TRIGGER_ON_DEMAND;
    set->profile = NetwPropertySet::PLAIN;
    set->channel = declared_channel("PROPERTY_SYNC");
    Ref<NetwQuantize> quantizer;
    if (p_config.is_valid()) {
        set->policy = p_config->get_write_policy();
        set->reliable = p_config->get_transfer_mode()
            == NetwMemberConfig::TRANSFER_RELIABLE;
        const Array declared = p_config->get_quantizers();
        if (!declared.is_empty()) {
            quantizer = Ref<NetwQuantize>(declared[0]);
        }
    }
    set->bind_column(
        NetwPropertySetColumn::create(
            p_property,
            quantizer,
            false,
            int64_t(SchemaCore::VARIANT)
        )
    );
    return set;
}

Ref<NetwPropertySet> from_property_configs(
    const Dictionary &p_configs,
    int64_t p_record
) {
    const bool want_input = p_record == NetwPropertySet::RECORD_INPUT;
    const bool want_broadcast = p_record == NetwPropertySet::RECORD_BROADCAST;
    const Ref<NetwPropertySet> set = for_record(p_record);

    NetwMemberConfig::Policy policy = p_record == NetwPropertySet::RECORD_STATE
        ? NetwMemberConfig::POLICY_AUTHORITY
        : NetwMemberConfig::POLICY_CONTROLLER;
    StringName policy_owner;
    int64_t trigger = NetwPropertySet::TRIGGER_ON_CHANGE;
    StringName trigger_owner;
    int64_t window = want_input ? 2 : 0;
    StringName window_owner;
    int64_t audience = want_input ? NetwPropertySet::AUDIENCE_SERVER_ONLY
                                  : NetwPropertySet::AUDIENCE_PUBLIC;
    bool masked = false;

    const Array declared = p_configs.keys();
    for (int index = 0; index < declared.size(); ++index) {
        const StringName property = declared[index];
        const Ref<NetwPropertyConfig> config
            = Ref<NetwPropertyConfig>(p_configs[property]);
        if (config.is_null()) {
            continue;
        }
        const bool marked = want_input ? config->get_in_input_set()
            : want_broadcast           ? config->get_in_broadcast_set()
                                       : config->get_in_state_set();
        if (!marked) {
            continue;
        }
        if (want_broadcast
            && (config->get_in_state_set() || config->get_in_input_set())) {
            warn_broadcast_exclusivity(property);
            continue;
        }

        Ref<NetwQuantize> quantizer;
        const Array quantizers = config->get_quantizers();
        if (!quantizers.is_empty()) {
            quantizer = Ref<NetwQuantize>(quantizers[0]);
        }
        const Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
            property,
            quantizer,
            config->get_lane() == NetwPropertySet::RETAINED,
            int64_t(SchemaCore::VARIANT)
        );
        column->property_class = config->get_property_class();
        column->converge_stiffness = config->get_converge_stiffness();
        column->carry_channel = config->get_carry_channel();
        column->explicit_teleport_only = config->get_explicit_teleport_only();
        column->explicit_reconcile_only = config->get_explicit_reconcile_only();
        column->epsilon_override = config->get_epsilon_override();
        column->teleport_at_override = config->get_teleport_at_override();
        set->bind_column(column);

        if (config->is_policy_declared()) {
            if (!String(policy_owner).is_empty()
                && config->get_write_policy() != policy) {
                warn_set_conflict(p_record, "policy", policy_owner, property);
            }
            policy = config->get_write_policy();
            policy_owner = property;
        }
        if (config->get_set_trigger() != NetwPropertyConfig::UNSET) {
            if (!String(trigger_owner).is_empty()
                && config->get_set_trigger() != trigger) {
                warn_set_conflict(p_record, "trigger", trigger_owner, property);
            }
            trigger = config->get_set_trigger();
            trigger_owner = property;
        }
        if (config->get_set_window() != NetwPropertyConfig::UNSET) {
            if (want_broadcast) {
                warn_broadcast_ignored("windowed", property);
            } else {
                if (!String(window_owner).is_empty()
                    && config->get_set_window() != window) {
                    warn_set_conflict(
                        p_record,
                        "window",
                        window_owner,
                        property
                    );
                }
                window = config->get_set_window();
                window_owner = property;
            }
        }
        if (config->get_set_audience()
            == NetwPropertySet::AUDIENCE_SERVER_ONLY) {
            if (want_broadcast) {
                warn_broadcast_ignored("audience", property);
            } else if (!want_input) {
                audience = NetwPropertySet::AUDIENCE_SERVER_ONLY;
            }
        }
        if (config->get_set_masked()) {
            masked = true;
        }
    }

    if (set->columns.is_empty()) {
        return Ref<NetwPropertySet>();
    }

    if (masked && window > 0) {
        NETW_WARN(
            sys::WIRE,
            "NetwPropertySet: masked and windowed are mutually exclusive; "
            "ignoring masked() since a redundant sample already defeats "
            "masking"
        );
        masked = false;
    }

    set->policy = policy;
    set->trigger = trigger;
    set->window = window;
    set->audience = audience;
    set->masked = masked;
    return set;
}

Ref<NetwPropertySet> from_script(
    const Ref<Script> &p_script,
    int64_t p_record,
    Object *p_api,
    Node *p_node
) {
    const Ref<NetwPropertySet> set = from_property_configs(
        netw::script::model::get_property_configs(p_script),
        p_record
    );
    if (set.is_null()) {
        return set;
    }
    NetwPropertySet::stamp_column_types(set, p_script, p_node);
    set->seal();
    if (p_api == nullptr) {
        return set;
    }
    NetwMultiplayer *core = godot::Object::cast_to<NetwMultiplayer>(p_api);
    if (core == nullptr) {
        const Variant held = p_api->get(StringName("_native_core"));
        core = godot::Object::cast_to<NetwMultiplayer>(
            static_cast<Object *>(held)
        );
    }
    if (core == nullptr) {
        return set;
    }
    return core->property_set_record(core->adopt_property_set(
        p_script,
        NetwMultiplayer::RecordKind(p_record),
        set,
        p_node
    ));
}

} // namespace property_set_builder

} // namespace netw
