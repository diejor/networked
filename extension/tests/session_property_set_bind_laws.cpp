#include "support/netw_test.h"

#include "support/netw_cells.h"

#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"

namespace TestNetwSessionPropertySetBindLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

constexpr int64_t ROUTE = 7;
constexpr int64_t COMP = 0;
constexpr int64_t HEALTH = 17;
const char *SYNCED = "position";
const char *STORED_ONLY = "stored_only";

enum Plant {
    PLANT_NONE,
    PLANT_A_SET_THAT_BINDS_EVERY_COLUMN,
    PLANT_A_SET_NOBODY_SEALED,
    PLANT_A_STRIDE_NOBODY_DECLARED,
};

struct BindScenario {
    String label;
    int stride = 1;
    bool binds_the_stored_column = false;
    bool mints_a_set = true;
    bool reads_a_node_property = false;
};

BindScenario one_column_of_two() {
    BindScenario scenario;
    scenario.label = "one-column-of-two";
    return scenario;
}

BindScenario both_columns() {
    BindScenario scenario;
    scenario.label = "both-columns";
    scenario.binds_the_stored_column = true;
    return scenario;
}

BindScenario a_column_read_off_a_node() {
    BindScenario scenario;
    scenario.label = "a-column-read-off-a-node";
    scenario.reads_a_node_property = true;
    return scenario;
}

BindScenario a_strided_schema() {
    BindScenario scenario;
    scenario.label = "a-strided-schema";
    scenario.stride = 4;
    scenario.mints_a_set = false;
    return scenario;
}

class BindRun {
    BindScenario declared;
    Plant planted = PLANT_NONE;
    bool set_was_minted = false;
    RID minted_schema;
    RID minted_set;
    int64_t bound_columns = 0;
    Array record_keys;
    bool stored_column_has_a_member = true;
    Error seal_verdict = FAILED;
    Error bind_verdict = FAILED;
    Variant read_back;
    int schema_columns = -1;
    StringName stored_key;

public:
    BindRun(const BindScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        Ref<NetwMultiplayer> session;
        session.instantiate();

        Node3D *body = memnew(Node3D);
        netw::gd::scene_root()->add_child(body);
        body->set_position(Vector3(0.0, HEALTH, 0.0));
        const Ref<NetwEntity> wrapper = NetwEntity::ensure(body);
        const RID entity = session->entity_of(body);
        session->liveness_bind_route(ROUTE, wrapper.ptr());

        minted_schema = session->schema_create("BoundBody");
        const int stride
            = planted == PLANT_A_STRIDE_NOBODY_DECLARED ? 1 : declared.stride;
        const int synced = session->schema_add_column(
            minted_schema,
            StringName(SYNCED),
            NetwMultiplayer::COLUMN_VECTOR3,
            stride
        );
        const int stored = session->schema_add_column(
            minted_schema,
            StringName(STORED_ONLY),
            NetwMultiplayer::COLUMN_F64,
            1
        );
        session->schema_seal(minted_schema);
        schema_columns = session->schema_get_column_count(minted_schema);
        stored_key = session->schema_get_column_key(minted_schema, stored);

        minted_set = session->property_set_create(
            minted_schema,
            NetwMultiplayer::RECORD_KIND_STATE
        );
        set_was_minted = minted_set.is_valid();
        if (!set_was_minted) {
            body->queue_free();
            return;
        }

        session->property_set_add_column(minted_set, synced);
        const bool binds_stored = declared.binds_the_stored_column
            || planted == PLANT_A_SET_THAT_BINDS_EVERY_COLUMN;
        if (binds_stored) {
            session->property_set_add_column(minted_set, stored);
        }
        seal_verdict = planted == PLANT_A_SET_NOBODY_SEALED
            ? OK
            : session->property_set_seal(minted_set);

        const Ref<NetwPropertySet> record
            = session->property_set_record(minted_set);
        if (record.is_valid()) {
            bound_columns = record->get_columns().size();
            record_keys = record->keys();
            stored_column_has_a_member = record->member(stored).is_valid();
        }

        bind_verdict
            = session->entity_add_property_set(entity, minted_set, COMP);
        read_back = session->entity_get_property(entity, COMP, 0);

        session->clear_session_state();
        body->queue_free();
    }

    const BindScenario &scenario() const {
        return declared;
    }

    bool minted() const {
        return set_was_minted;
    }

    RID schema() const {
        return minted_schema;
    }

    RID set() const {
        return minted_set;
    }

    int64_t bound() const {
        return bound_columns;
    }

    const Array &keys() const {
        return record_keys;
    }

    bool stored_is_bound() const {
        return stored_column_has_a_member;
    }

    Error sealed() const {
        return seal_verdict;
    }

    Error bound_to_the_entity() const {
        return bind_verdict;
    }

    const Variant &value() const {
        return read_back;
    }

    int declared_columns() const {
        return schema_columns;
    }

    const StringName &stored_column_key() const {
        return stored_key;
    }
};

typedef LawRowFor<BindRun> BindLaw;

LawVerdict law_minted(const BindRun &p_run) {
    if (p_run.minted() != p_run.scenario().mints_a_set) {
        return law_broken(
            "the set %s minted where the schema says it %s",
            p_run.minted() ? "was" : "was not",
            p_run.scenario().mints_a_set ? "should" : "should not"
        );
    }
    return law_held();
}

LawVerdict law_declared(const BindRun &p_run) {
    if (p_run.declared_columns() != 2) {
        return law_broken(
            "the schema declares %d columns rather than two",
            p_run.declared_columns()
        );
    }
    if (String(p_run.stored_column_key()) != String(STORED_ONLY)) {
        return law_broken("the schema lost the name of its stored column");
    }
    return law_held();
}

LawVerdict law_narrow(const BindRun &p_run) {
    if (!p_run.minted()) {
        return law_held();
    }
    const int64_t expected = p_run.scenario().binds_the_stored_column ? 2 : 1;
    if (p_run.bound() != expected) {
        return law_broken(
            "the set binds %d columns of the two declared, against %d",
            int(p_run.bound()),
            int(expected)
        );
    }
    if (int(p_run.keys().size()) != int(expected)) {
        return law_broken(
            "the record names %d columns against %d",
            int(p_run.keys().size()),
            int(expected)
        );
    }
    if (p_run.stored_is_bound() != p_run.scenario().binds_the_stored_column) {
        return law_broken(
            "the stored column %s a lane no set gave it",
            p_run.stored_is_bound() ? "rides" : "does not ride"
        );
    }
    return law_held();
}

LawVerdict law_addressed(const BindRun &p_run) {
    if (!p_run.minted()) {
        return law_held();
    }
    if (p_run.sealed() != OK) {
        return law_broken("sealing the set answered %d", int(p_run.sealed()));
    }
    if (p_run.bound_to_the_entity() != OK) {
        return law_broken(
            "binding the set to its entity answered %d",
            int(p_run.bound_to_the_entity())
        );
    }
    return law_held();
}

LawVerdict law_read_through(const BindRun &p_run) {
    if (!p_run.minted() || p_run.bound_to_the_entity() != OK) {
        return law_held();
    }
    if (p_run.value().get_type() != Variant::VECTOR3) {
        return law_broken(
            "the bound column reads back as a %d",
            int(p_run.value().get_type())
        );
    }
    if (double(Vector3(p_run.value()).y) != double(HEALTH)) {
        return law_broken(
            "the bound column reads %d off the live node",
            int(Vector3(p_run.value()).y)
        );
    }
    return law_held();
}

const BindLaw L_MINTED = {
    "minted",
    "a set is minted only for a schema a node property can carry",
    &law_minted,
};

const BindLaw L_DECLARED = {
    "declared",
    "a schema keeps every column it was given, bound or not",
    &law_declared,
};

const BindLaw L_NARROW = {
    "narrow",
    "a column no set binds rides no lane, which is what keeps a stored value "
    "off the wire by construction",
    &law_narrow,
};

const BindLaw L_ADDRESSED = {
    "addressed",
    "a sealed set binds to the entity named by its own RID",
    &law_addressed,
};

const BindLaw L_READ_THROUGH = {
    "read-through",
    "a bound column reads the live node at its one column address",
    &law_read_through,
};

const BindLaw LAWS[]
    = {L_MINTED, L_DECLARED, L_NARROW, L_ADDRESSED, L_READ_THROUGH};

TEST_CASE(
    "[Networked][Session][SceneTree] the property set binding laws "
    "hold"
) {
    const BindScenario CORPUS[] = {
        one_column_of_two(),
        both_columns(),
        a_column_read_off_a_node(),
        a_strided_schema(),
    };
    for (const BindScenario &scenario : CORPUS) {
        const BindRun run(scenario);
        for (const BindLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Session][SceneTree] a set that binds every column reds "
    "narrow"
) {
    const BindScenario scenario = one_column_of_two();
    const BindRun run(scenario, PLANT_A_SET_THAT_BINDS_EVERY_COLUMN);
    NETW_CELL(L_NARROW, scenario);
    NETW_LAW_BREAKS(L_NARROW, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a set nobody sealed reds "
    "addressed"
) {
    const BindScenario scenario = one_column_of_two();
    const BindRun run(scenario, PLANT_A_SET_NOBODY_SEALED);
    NETW_CELL(L_ADDRESSED, scenario);
    NETW_LAW_BREAKS(L_ADDRESSED, run);
}

TEST_CASE(
    "[Networked][Session][SceneTree] a schema that drops its stride reds "
    "minted"
) {
    const BindScenario scenario = a_strided_schema();
    const BindRun run(scenario, PLANT_A_STRIDE_NOBODY_DECLARED);
    NETW_CELL(L_MINTED, scenario);
    NETW_LAW_BREAKS(L_MINTED, run);
}

} // namespace TestNetwSessionPropertySetBindLaws
