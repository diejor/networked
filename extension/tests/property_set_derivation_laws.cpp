#include "support/netw_test.h"

#include "support/netw_cells.h"

#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/quantize.hpp"
#include "netw/property_set_builder.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwPropertySetDerivationLaws {

using namespace godot;
using netw::NetwMemberConfig;
using netw::NetwPropertyConfig;
using netw::NetwPropertySet;
namespace property_set_builder = netw::property_set_builder;
using netw::NetwPropertySetColumn;
using netw::NetwQuantizeScalar;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_A_MARK_THE_DRIVE_FORGOT,
    PLANT_DECLARATION_ORDER_REVERSED,
    PLANT_THE_LAST_KNOB_DECLARED_FIRST,
    PLANT_A_QUANTIZER_ON_THE_OTHER_FIELD,
};

enum Policy {
    POLICY_UNDECLARED,
    POLICY_ASKS_AUTHORITY,
    POLICY_ASKS_CONTROLLER,
};

struct FieldDecl {
    StringName key;
    bool in_state = false;
    bool in_input = false;
    bool in_broadcast = false;
    bool retained = false;
    bool persisted = false;
    bool masked = false;
    bool server_only = false;
    bool quantized = false;
    int64_t window = -1;
    Policy policy = POLICY_UNDECLARED;

    bool marked_for(int64_t p_record) const {
        if (p_record == NetwPropertySet::RECORD_INPUT) {
            return in_input;
        }
        if (p_record == NetwPropertySet::RECORD_BROADCAST) {
            return in_broadcast && !in_state && !in_input;
        }
        return in_state;
    }
};

struct DerivationScenario {
    String label;
    int64_t record = NetwPropertySet::RECORD_STATE;
    Vector<FieldDecl> fields;

    void add(const FieldDecl &p_field) {
        fields.push_back(p_field);
    }
};

FieldDecl state_field(const char *p_key) {
    FieldDecl field;
    field.key = StringName(p_key);
    field.in_state = true;
    return field;
}

FieldDecl input_field(const char *p_key) {
    FieldDecl field;
    field.key = StringName(p_key);
    field.in_input = true;
    return field;
}

FieldDecl broadcast_field(const char *p_key) {
    FieldDecl field;
    field.key = StringName(p_key);
    field.in_broadcast = true;
    return field;
}

FieldDecl unmarked_field(const char *p_key) {
    FieldDecl field;
    field.key = StringName(p_key);
    field.retained = true;
    return field;
}

DerivationScenario state_in_declaration_order() {
    DerivationScenario scenario;
    scenario.label = "state-in-declaration-order";
    scenario.add(state_field("position"));
    scenario.add(unmarked_field("health"));
    scenario.add(state_field("rotation"));
    return scenario;
}

DerivationScenario input_pair() {
    DerivationScenario scenario;
    scenario.label = "input-pair";
    scenario.record = NetwPropertySet::RECORD_INPUT;
    scenario.add(input_field("move"));
    scenario.add(input_field("aim"));
    return scenario;
}

DerivationScenario broadcast_masked() {
    DerivationScenario scenario;
    scenario.label = "broadcast-masked";
    scenario.record = NetwPropertySet::RECORD_BROADCAST;
    FieldDecl aim = broadcast_field("aim_dir");
    aim.masked = true;
    scenario.add(aim);
    return scenario;
}

DerivationScenario broadcast_asking_for_what_it_cannot_have() {
    DerivationScenario scenario;
    scenario.label = "broadcast-asking-for-what-it-cannot-have";
    scenario.record = NetwPropertySet::RECORD_BROADCAST;
    FieldDecl aim = broadcast_field("aim");
    aim.server_only = true;
    aim.window = 3;
    scenario.add(aim);
    return scenario;
}

DerivationScenario broadcast_yielding_to_a_recorded_mark() {
    DerivationScenario scenario;
    scenario.label = "broadcast-yielding-to-a-recorded-mark";
    scenario.record = NetwPropertySet::RECORD_BROADCAST;
    FieldDecl position = state_field("position");
    position.in_broadcast = true;
    scenario.add(position);
    scenario.add(broadcast_field("aim_dir"));
    return scenario;
}

DerivationScenario a_doubly_marked_field_still_records() {
    DerivationScenario scenario;
    scenario.label = "a-doubly-marked-field-still-records";
    FieldDecl position = state_field("position");
    position.in_broadcast = true;
    scenario.add(position);
    return scenario;
}

DerivationScenario nothing_marked() {
    DerivationScenario scenario;
    scenario.label = "nothing-marked";
    scenario.add(unmarked_field("health"));
    FieldDecl gold = unmarked_field("gold");
    gold.retained = false;
    gold.persisted = true;
    scenario.add(gold);
    return scenario;
}

DerivationScenario lanes_split_by_field() {
    DerivationScenario scenario;
    scenario.label = "lanes-split-by-field";
    scenario.add(state_field("position"));
    FieldDecl inventory = state_field("inventory");
    inventory.retained = true;
    scenario.add(inventory);
    return scenario;
}

DerivationScenario one_quantizer_of_two_fields() {
    DerivationScenario scenario;
    scenario.label = "one-quantizer-of-two-fields";
    FieldDecl position = state_field("position");
    position.quantized = true;
    scenario.add(position);
    scenario.add(state_field("rotation"));
    return scenario;
}

DerivationScenario a_member_narrowing_the_kind() {
    DerivationScenario scenario;
    scenario.label = "a-member-narrowing-the-kind";
    FieldDecl secret = state_field("secret");
    secret.server_only = true;
    secret.policy = POLICY_ASKS_CONTROLLER;
    scenario.add(secret);
    return scenario;
}

DerivationScenario two_members_naming_one_window() {
    DerivationScenario scenario;
    scenario.label = "two-members-naming-one-window";
    scenario.record = NetwPropertySet::RECORD_INPUT;
    FieldDecl move = input_field("move");
    move.window = 2;
    scenario.add(move);
    FieldDecl aim = input_field("aim");
    aim.window = 5;
    scenario.add(aim);
    return scenario;
}

DerivationScenario masked_beside_a_window() {
    DerivationScenario scenario;
    scenario.label = "masked-beside-a-window";
    scenario.record = NetwPropertySet::RECORD_INPUT;
    FieldDecl move = input_field("move");
    move.masked = true;
    move.window = 3;
    scenario.add(move);
    return scenario;
}

DerivationScenario masked_state() {
    DerivationScenario scenario;
    scenario.label = "masked-state";
    FieldDecl position = state_field("position");
    position.masked = true;
    scenario.add(position);
    return scenario;
}

struct Evidence {
    bool derived = false;
    Vector<StringName> keys;
    Vector<int64_t> lanes;
    Vector<bool> watches;
    Vector<bool> quantized;
    int64_t record = -1;
    int64_t profile = -1;
    int64_t stamp = -1;
    int64_t cadence = -1;
    int64_t trigger = -1;
    int64_t audience = -1;
    int64_t policy = -1;
    int64_t channel = -1;
    int64_t window = -1;
    bool masked = false;
};

class DerivationRun {
    DerivationScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    Ref<NetwPropertyConfig> compile(const FieldDecl &p_field, int p_at) const {
        Ref<NetwPropertyConfig> config;
        config.instantiate();
        config->set_is_property(true);
        if (p_field.in_state
            && !(plant_is(PLANT_A_MARK_THE_DRIVE_FORGOT) && p_at > 0)) {
            config->state();
        }
        if (p_field.in_input
            && !(plant_is(PLANT_A_MARK_THE_DRIVE_FORGOT) && p_at > 0)) {
            config->input();
        }
        if (p_field.in_broadcast) {
            config->broadcast();
        }
        if (p_field.retained) {
            config->retained();
        }
        if (p_field.persisted) {
            config->persisted(0.0);
        }
        if (p_field.masked) {
            config->masked();
        }
        if (p_field.server_only) {
            config->audience(true);
        }
        if (p_field.policy == POLICY_ASKS_AUTHORITY) {
            config->authority();
        }
        if (p_field.policy == POLICY_ASKS_CONTROLLER) {
            config->controller();
        }
        if (p_field.window >= 0) {
            config->windowed(window_for(p_at));
        }
        if (p_field.quantized
            != plant_is(PLANT_A_QUANTIZER_ON_THE_OTHER_FIELD)) {
            Ref<NetwQuantizeScalar> codec;
            codec.instantiate();
            config->quantize(netw::gd::array_of(codec));
        }
        return config;
    }

    int64_t window_for(int p_at) const {
        if (!plant_is(PLANT_THE_LAST_KNOB_DECLARED_FIRST)) {
            return declared.fields[p_at].window;
        }
        Vector<int> naming;
        for (int index = 0; index < declared.fields.size(); ++index) {
            if (declared.fields[index].window >= 0) {
                naming.push_back(index);
            }
        }
        for (int slot = 0; slot < naming.size(); ++slot) {
            if (naming[slot] == p_at) {
                return declared.fields[naming[naming.size() - 1 - slot]].window;
            }
        }
        return declared.fields[p_at].window;
    }

    void drive() {
        Dictionary configs;
        const int count = declared.fields.size();
        for (int index = 0; index < count; ++index) {
            const int at = plant_is(PLANT_DECLARATION_ORDER_REVERSED)
                ? count - 1 - index
                : index;
            configs[declared.fields[at].key] = compile(declared.fields[at], at);
        }

        const Ref<NetwPropertySet> set
            = property_set_builder::from_property_configs(
                configs,
                declared.record
            );
        if (set.is_null()) {
            return;
        }
        read.derived = true;
        read.record = set->get_record();
        read.profile = set->get_profile();
        read.stamp = set->get_stamp();
        read.cadence = set->get_cadence();
        read.trigger = set->get_trigger();
        read.audience = set->get_audience();
        read.policy = set->get_policy();
        read.channel = set->get_channel();
        read.window = set->get_window();
        read.masked = set->get_masked();

        const TypedArray<StringName> keys = set->keys();
        for (int index = 0; index < keys.size(); ++index) {
            read.keys.push_back(StringName(keys[index]));
        }
        const Array quantizers = set->quantizers();
        for (int index = 0; index < quantizers.size(); ++index) {
            read.quantized.push_back(
                Ref<netw::NetwQuantize>(quantizers[index]).is_valid()
            );
        }
        for (int index = 0; index < set->columns.size(); ++index) {
            const Ref<NetwPropertySetColumn> column = set->columns[index];
            read.lanes.push_back(column->get_lane());
            read.watches.push_back(column->get_watch());
        }
    }

public:
    explicit DerivationRun(
        const DerivationScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        drive();
    }

    const DerivationScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return read;
    }

    Vector<FieldDecl> collected() const {
        Vector<FieldDecl> out;
        for (int index = 0; index < declared.fields.size(); ++index) {
            if (declared.fields[index].marked_for(declared.record)) {
                out.push_back(declared.fields[index]);
            }
        }
        return out;
    }
};

typedef LawRowFor<DerivationRun> DerivationLaw;

LawVerdict law_collected(const DerivationRun &p_run) {
    const Vector<FieldDecl> owed = p_run.collected();
    const Evidence &read = p_run.evidence();
    if (owed.is_empty()) {
        return read.derived
            ? law_broken(
                  "a set naming %d fields derived from no mark at all",
                  read.keys.size()
              )
            : law_held();
    }
    if (!read.derived) {
        return law_broken("%d marked fields derived no set", owed.size());
    }
    if (read.keys.size() != owed.size()) {
        return law_broken(
            "the set names %d fields for %d marks",
            read.keys.size(),
            owed.size()
        );
    }
    for (int index = 0; index < owed.size(); ++index) {
        if (read.keys[index] != owed[index].key) {
            return law_broken(
                "field %d reads '%s', declared '%s'",
                index,
                String(read.keys[index]).utf8().get_data(),
                String(owed[index].key).utf8().get_data()
            );
        }
    }
    return law_held();
}

LawVerdict law_kind_presets(const DerivationRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.derived) {
        return law_held();
    }
    const int64_t record = p_run.scenario().record;
    if (read.record != record) {
        return law_broken(
            "the set records kind %d, asked %d",
            int(read.record),
            int(record)
        );
    }
    if (read.profile != NetwPropertySet::STAMPED) {
        return law_broken("profile %d is not stamped", int(read.profile));
    }
    if (read.cadence != NetwPropertySet::TICK) {
        return law_broken("cadence %d is not the tick", int(read.cadence));
    }
    if (read.channel != netw::wire::builtin_channel("SYNC")) {
        return law_broken(
            "channel %d is not the shared sync frame",
            int(read.channel)
        );
    }
    const int64_t owed_stamp = record == NetwPropertySet::RECORD_STATE
        ? NetwPropertySet::STAMP_TICK_ACK
        : NetwPropertySet::STAMP_TICK;
    if (read.stamp != owed_stamp) {
        return law_broken(
            "stamp %d, kind %d is owed %d",
            int(read.stamp),
            int(record),
            int(owed_stamp)
        );
    }
    return law_held();
}

LawVerdict law_reach(const DerivationRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.derived) {
        return law_held();
    }
    const int64_t record = p_run.scenario().record;
    bool asked_server_only = false;
    Policy asked_policy = POLICY_UNDECLARED;
    const Vector<FieldDecl> owed = p_run.collected();
    for (int index = 0; index < owed.size(); ++index) {
        asked_server_only = asked_server_only || owed[index].server_only;
        asked_policy = owed[index].policy != POLICY_UNDECLARED
            ? owed[index].policy
            : asked_policy;
    }

    int64_t owed_audience = record == NetwPropertySet::RECORD_INPUT
        ? NetwPropertySet::AUDIENCE_SERVER_ONLY
        : NetwPropertySet::AUDIENCE_PUBLIC;
    if (asked_server_only && record == NetwPropertySet::RECORD_STATE) {
        owed_audience = NetwPropertySet::AUDIENCE_SERVER_ONLY;
    }
    if (read.audience != owed_audience) {
        return law_broken(
            "audience %d, kind %d is owed %d",
            int(read.audience),
            int(record),
            int(owed_audience)
        );
    }

    int64_t owed_policy = record == NetwPropertySet::RECORD_STATE
        ? NetwMemberConfig::POLICY_AUTHORITY
        : NetwMemberConfig::POLICY_CONTROLLER;
    if (asked_policy == POLICY_ASKS_AUTHORITY) {
        owed_policy = NetwMemberConfig::POLICY_AUTHORITY;
    }
    if (asked_policy == POLICY_ASKS_CONTROLLER) {
        owed_policy = NetwMemberConfig::POLICY_CONTROLLER;
    }
    if (read.policy != owed_policy) {
        return law_broken(
            "policy %d, the declaration is owed %d",
            int(read.policy),
            int(owed_policy)
        );
    }
    return law_held();
}

LawVerdict law_knobs(const DerivationRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.derived) {
        return law_held();
    }
    const int64_t record = p_run.scenario().record;
    const Vector<FieldDecl> owed = p_run.collected();

    int64_t owed_window = record == NetwPropertySet::RECORD_INPUT ? 2 : 0;
    bool owed_masked = false;
    for (int index = 0; index < owed.size(); ++index) {
        if (owed[index].window >= 0
            && record != NetwPropertySet::RECORD_BROADCAST) {
            owed_window = owed[index].window;
        }
        owed_masked = owed_masked || owed[index].masked;
    }
    owed_masked = owed_masked && owed_window <= 0;

    if (read.window != owed_window) {
        return law_broken(
            "window %d, the last member to name one asked %d",
            int(read.window),
            int(owed_window)
        );
    }
    if (read.masked != owed_masked) {
        return law_broken(
            "masked reads %d beside a window of %d",
            int(read.masked),
            int(read.window)
        );
    }
    return law_held();
}

LawVerdict law_lanes(const DerivationRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.derived) {
        return law_held();
    }
    const Vector<FieldDecl> owed = p_run.collected();
    if (read.lanes.size() != owed.size()
        || read.watches.size() != owed.size()) {
        return law_broken(
            "%d lanes and %d watches for %d fields",
            read.lanes.size(),
            read.watches.size(),
            owed.size()
        );
    }
    for (int index = 0; index < owed.size(); ++index) {
        const int64_t lane = owed[index].retained ? NetwPropertySet::RETAINED
                                                  : NetwPropertySet::VOLATILE;
        if (read.lanes[index] != lane) {
            return law_broken(
                "'%s' rides lane %d, declared %d",
                String(owed[index].key).utf8().get_data(),
                int(read.lanes[index]),
                int(lane)
            );
        }
        if (read.watches[index] != owed[index].retained) {
            return law_broken(
                "'%s' watches %d on lane %d",
                String(owed[index].key).utf8().get_data(),
                int(read.watches[index]),
                int(read.lanes[index])
            );
        }
    }
    return law_held();
}

LawVerdict law_quantizers(const DerivationRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (!read.derived) {
        return law_held();
    }
    const Vector<FieldDecl> owed = p_run.collected();
    if (read.quantized.size() != owed.size()) {
        return law_broken(
            "%d quantizer slots for %d fields",
            read.quantized.size(),
            owed.size()
        );
    }
    for (int index = 0; index < owed.size(); ++index) {
        if (read.quantized[index] != owed[index].quantized) {
            return law_broken(
                "'%s' carries quantizer %d, declared %d",
                String(owed[index].key).utf8().get_data(),
                int(read.quantized[index]),
                int(owed[index].quantized)
            );
        }
    }
    return law_held();
}

const DerivationLaw L_COLLECTED = {
    "collected",
    "the set names exactly the fields carrying the kind's mark, in the order "
    "they were declared, and no marked field derives no set at all",
    &law_collected,
};

const DerivationLaw L_PRESETS = {
    "presets",
    "the kind stamps its own axis presets, and every derived set rides the "
    "shared sync frame",
    &law_kind_presets,
};

const DerivationLaw L_REACH = {
    "reach",
    "the kind's default reach and write policy stand until a member asks to "
    "narrow them",
    &law_reach,
};

const DerivationLaw L_KNOBS = {
    "knobs",
    "the last member to name a set-level knob owns it, a broadcast keeps "
    "none of them, and masked yields to a window",
    &law_knobs,
};

const DerivationLaw L_LANES = {
    "lanes",
    "every field rides the lane it declared and watches exactly when it is "
    "retained",
    &law_lanes,
};

const DerivationLaw L_QUANTIZERS = {
    "quantizers",
    "the quantizer array is parallel to the key array, slot for slot",
    &law_quantizers,
};

const DerivationLaw LAWS[] = {
    L_COLLECTED,
    L_PRESETS,
    L_REACH,
    L_KNOBS,
    L_LANES,
    L_QUANTIZERS,
};

TEST_CASE("[Networked][Sync][Hosted] the property set derivation laws hold") {
    const DerivationScenario CORPUS[] = {
        state_in_declaration_order(),
        input_pair(),
        broadcast_masked(),
        broadcast_asking_for_what_it_cannot_have(),
        broadcast_yielding_to_a_recorded_mark(),
        a_doubly_marked_field_still_records(),
        nothing_marked(),
        lanes_split_by_field(),
        one_quantizer_of_two_fields(),
        a_member_narrowing_the_kind(),
        two_members_naming_one_window(),
        masked_beside_a_window(),
        masked_state(),
    };
    for (const DerivationScenario &scenario : CORPUS) {
        const DerivationRun run(scenario);
        for (const DerivationLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Sync][Hosted] a mark the declaration never carried reds "
    "collected"
) {
    const DerivationScenario scenario = state_in_declaration_order();
    const DerivationRun run(scenario, PLANT_A_MARK_THE_DRIVE_FORGOT);
    NETW_CELL(L_COLLECTED, scenario);
    NETW_LAW_BREAKS(L_COLLECTED, run);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a declaration read back to front reds "
    "collected"
) {
    const DerivationScenario scenario = state_in_declaration_order();
    const DerivationRun run(scenario, PLANT_DECLARATION_ORDER_REVERSED);
    NETW_CELL(L_COLLECTED, scenario);
    NETW_LAW_BREAKS(L_COLLECTED, run);
}

TEST_CASE(
    "[Networked][Sync][Hosted] the last window declared first reds "
    "knobs"
) {
    const DerivationScenario scenario = two_members_naming_one_window();
    const DerivationRun run(scenario, PLANT_THE_LAST_KNOB_DECLARED_FIRST);
    NETW_CELL(L_KNOBS, scenario);
    NETW_LAW_BREAKS(L_KNOBS, run);
}

TEST_CASE(
    "[Networked][Sync][Hosted] a quantizer handed to the field beside "
    "the one that declared it reds quantizers"
) {
    const DerivationScenario scenario = one_quantizer_of_two_fields();
    const DerivationRun run(scenario, PLANT_A_QUANTIZER_ON_THE_OTHER_FIELD);
    NETW_CELL(L_QUANTIZERS, scenario);
    NETW_LAW_BREAKS(L_QUANTIZERS, run);
}

} // namespace TestNetwPropertySetDerivationLaws
