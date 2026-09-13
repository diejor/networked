#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/property_set_builder.hpp"

namespace TestNetwPropertySetCompileLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
namespace property_set_builder = netw::property_set_builder;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

const char *SUBJECT_SCRIPT = netw_test::gdsrc::STATE_AND_INPUT;

enum Plant {
    PLANT_NONE,
    PLANT_A_COMPILE_THAT_NEVER_CACHES,
    PLANT_A_HASH_TAKEN_FOR_ANOTHER_SET,
};

struct CompileScenario {
    String label;
    int64_t record = NetwPropertySet::RECORD_STATE;
};

CompileScenario a_state_set() {
    CompileScenario scenario;
    scenario.label = "a-state-set";
    scenario.record = NetwPropertySet::RECORD_STATE;
    return scenario;
}

CompileScenario an_input_set() {
    CompileScenario scenario;
    scenario.label = "an-input-set";
    scenario.record = NetwPropertySet::RECORD_INPUT;
    return scenario;
}

Array shape_of(const Ref<NetwPropertySet> &p_set) {
    Array members;
    const TypedArray<netw::NetwPropertySetColumn> columns
        = p_set->get_columns();
    for (int index = 0; index < columns.size(); index++) {
        const Ref<netw::NetwPropertySetColumn> column = columns[index];
        Array row;
        row.push_back(column->get_key());
        row.push_back(column->get_type());
        row.push_back(column->get_lane());
        row.push_back(column->get_property_class());
        row.push_back(column->get_carry_channel());
        members.push_back(row);
    }
    Array shape;
    shape.push_back(members);
    shape.push_back(p_set->get_record());
    shape.push_back(p_set->get_masked());
    shape.push_back(p_set->get_window());
    shape.push_back(p_set->get_audience());
    shape.push_back(p_set->get_policy());
    shape.push_back(p_set->get_channel());
    shape.push_back(p_set->get_reliable());
    return shape;
}

class CompileRun {
    CompileScenario declared;
    Plant planted = PLANT_NONE;
    bool valid = false;
    bool sealed = false;
    bool distinct_objects = false;
    bool shapes_agree = false;
    bool cached = false;
    int64_t session_hash = -1;
    int64_t set_hash = -2;

public:
    CompileRun(const CompileScenario &p_scenario, Plant p_plant = PLANT_NONE)
        : declared(p_scenario), planted(p_plant) {
        const Ref<Script> script = netw_test::script_from(SUBJECT_SCRIPT);
        REQUIRE(script.is_valid());
        Node *node = Object::cast_to<Node>(script->call("new"));
        REQUIRE(node != nullptr);

        Ref<NetwMultiplayer> session;
        session.instantiate();

        const Ref<NetwPropertySet> legacy = property_set_builder::from_script(
            script,
            declared.record,
            nullptr,
            node
        );
        const Ref<NetwPropertySet> flat = property_set_builder::from_script(
            script,
            declared.record,
            session.ptr(),
            node
        );
        REQUIRE(legacy.is_valid());
        REQUIRE(flat.is_valid());

        valid = flat->get_rid_handle().is_valid();
        sealed = flat->get_sealed();
        distinct_objects = legacy != flat;
        shapes_agree = shape_of(flat) == shape_of(legacy);
        session_hash
            = session->property_set_get_wire_hash(flat->get_rid_handle());
        set_hash = flat->wire_hash();
        if (planted == PLANT_A_HASH_TAKEN_FOR_ANOTHER_SET) {
            session_hash = session->property_set_get_wire_hash(RID());
        }

        const Ref<NetwPropertySet> again = property_set_builder::from_script(
            script,
            declared.record,
            planted == PLANT_A_COMPILE_THAT_NEVER_CACHES ? nullptr
                                                         : session.ptr(),
            node
        );
        cached = again.is_valid()
            && again->get_rid_handle() == flat->get_rid_handle();

        memdelete(node);
    }

    const CompileScenario &scenario() const {
        return declared;
    }

    bool minted_a_rid() const {
        return valid;
    }

    bool is_sealed() const {
        return sealed;
    }

    bool is_its_own_object() const {
        return distinct_objects;
    }

    bool shape_is_the_same() const {
        return shapes_agree;
    }

    bool compiled_once() const {
        return cached;
    }

    int64_t hash_the_session_holds() const {
        return session_hash;
    }

    int64_t hash_the_set_holds() const {
        return set_hash;
    }
};

typedef LawRowFor<CompileRun> CompileLaw;

LawVerdict law_sealed(const CompileRun &p_run) {
    if (!p_run.minted_a_rid()) {
        return law_broken("a compiled set carries no address");
    }
    if (!p_run.is_sealed()) {
        return law_broken("a compiled set is open to further columns");
    }
    return law_held();
}

LawVerdict law_parity(const CompileRun &p_run) {
    if (!p_run.is_its_own_object()) {
        return law_broken("the flat compile answered the same object");
    }
    if (!p_run.shape_is_the_same()) {
        return law_broken(
            "the flat compile declares a different set than the object it "
            "replaced"
        );
    }
    return law_held();
}

LawVerdict law_agreed(const CompileRun &p_run) {
    if (p_run.hash_the_session_holds() != p_run.hash_the_set_holds()) {
        return law_broken(
            "the session hashes the set as %d and the set as %d",
            int(p_run.hash_the_session_holds()),
            int(p_run.hash_the_set_holds())
        );
    }
    return law_held();
}

LawVerdict law_cached(const CompileRun &p_run) {
    if (!p_run.compiled_once()) {
        return law_broken("compiling the same script twice minted two sets");
    }
    return law_held();
}

const CompileLaw L_SEALED = {
    "sealed",
    "a script compiles to one sealed set at one address",
    &law_sealed,
};

const CompileLaw L_PARITY = {
    "parity",
    "the flat compile declares what the object it replaced declared",
    &law_parity,
};

const CompileLaw L_AGREED = {
    "agreed",
    "the session and the set hash the same wire the same way",
    &law_agreed,
};

const CompileLaw L_CACHED = {
    "cached",
    "one script compiles once, so a second ask reads the same address",
    &law_cached,
};

const CompileLaw LAWS[] = {L_SEALED, L_PARITY, L_AGREED, L_CACHED};

TEST_CASE("[Networked][Session] the property set compile laws hold") {
    const CompileScenario CORPUS[] = {a_state_set(), an_input_set()};
    for (const CompileScenario &scenario : CORPUS) {
        const CompileRun run(scenario);
        for (const CompileLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE("[Networked][Session] a compile that names no session reds cached") {
    const CompileScenario scenario = a_state_set();
    const CompileRun run(scenario, PLANT_A_COMPILE_THAT_NEVER_CACHES);
    NETW_CELL(L_CACHED, scenario);
    NETW_LAW_BREAKS(L_CACHED, run);
}

TEST_CASE("[Networked][Session] a hash taken for another set reds agreed") {
    const CompileScenario scenario = a_state_set();
    const CompileRun run(scenario, PLANT_A_HASH_TAKEN_FOR_ANOTHER_SET);
    NETW_CELL(L_AGREED, scenario);
    NETW_LAW_BREAKS(L_AGREED, run);
}

} // namespace TestNetwPropertySetCompileLaws

#endif
