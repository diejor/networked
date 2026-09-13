#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/display_stand.h"
#include "support/loopback_rig.h"
#include "support/netw_cells.h"

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/ring_buffer.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/display/decl.hpp"
#include "netw/property_set_builder.hpp"

namespace TestNetwDisplayAuthoringTickLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwRingBuffer;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_THE_RECEIVE_TICK_IS_THE_KEY,
};

struct AuthoringScenario {
    String label;
    int64_t delay_ticks = 0;
    int64_t authored_ticks = 6;
};

AuthoringScenario an_undelayed_stream() {
    AuthoringScenario scenario;
    scenario.label = "an-undelayed-stream";
    return scenario;
}

AuthoringScenario a_delayed_stream() {
    AuthoringScenario scenario;
    scenario.label = "a-delayed-stream";
    scenario.delay_ticks = 5;
    return scenario;
}

Vector2 authored_at(int64_t p_tick) {
    return Vector2(real_t(p_tick), real_t(-p_tick));
}

struct Evidence {
    int64_t newest_key = -1;
    Vector2 value_at_newest;
    int64_t displayed_tick = -1;
    Vector2 value_at_displayed;
    bool holds_displayed_key = false;
    int64_t local_tick = -1;
};

class AuthoringRun {
    AuthoringScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    void drive() {
        netw_test::LoopbackRig rig(1);
        rig.mount();
        const Ref<NetwMultiplayer> core = Ref<NetwMultiplayer>(
            Object::cast_to<NetwMultiplayer>(rig.server())
        );
        REQUIRE(core.is_valid());
        netw_test::install_clock(core.ptr());
        const netw_test::DisplaySubject subject
            = netw_test::stand_subject(core, 0, rig.branch(-1));
        const RID entity = subject.entity->get_rid_handle();

        Dictionary configs;
        configs[StringName("position")] = netw::Netw::configure_property(
                                              subject.body,
                                              StringName("position"),
                                              false
        )
                                              ->state();
        const Ref<netw::NetwPropertySet> set
            = netw::property_set_builder::from_property_configs(
                configs,
                netw::NetwPropertySet::RECORD_STATE
            );
        REQUIRE(set.is_valid());
        set->set_sealed(true);
        NETW_CHECK_EQ(
            int(
                core->sync_pipeline()->register_property_set(subject.body, set)
            ),
            int(OK)
        );
        core->display_mark_dirty(entity, netw::display::DIRT_RUNTIME);
        core->session_flush_deferred();

        for (int64_t authored = 0; authored < declared.authored_ticks;
             ++authored) {
            const int64_t received = authored + declared.delay_ticks;
            core->clock_engine().set_tick(int(received));
            netw_test::record_sample(
                core,
                subject.body,
                authored_at(authored),
                plant_is(PLANT_THE_RECEIVE_TICK_IS_THE_KEY) ? received
                                                            : authored,
                true
            );
        }

        const int64_t live = declared.authored_ticks + declared.delay_ticks;
        netw_test::pump_at(core, live, 2, 0.0);
        read.local_tick = live;

        const Ref<NetwRingBuffer> buffer = core->display_get_track_stat(
            entity,
            StringName("position"),
            StringName("buffer")
        );
        if (buffer.is_valid()) {
            read.newest_key = buffer->newest_tick();
            read.value_at_newest = buffer->get_at(read.newest_key);
        }
        read.displayed_tick = core->display_get_tick(entity);
        if (buffer.is_valid() && read.displayed_tick >= 0) {
            const Variant held = buffer->get_at(read.displayed_tick);
            read.holds_displayed_key = held.get_type() != Variant::NIL;
            read.value_at_displayed = held;
        }

        netw_test::retire_subject(subject);
    }

public:
    explicit AuthoringRun(
        const AuthoringScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        drive();
    }

    AuthoringRun(const AuthoringRun &) = delete;
    AuthoringRun &operator=(const AuthoringRun &) = delete;

    const AuthoringScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return read;
    }
};

typedef LawRowFor<AuthoringRun> AuthoringLaw;

LawVerdict law_keyed_by_authoring(const AuthoringRun &p_run) {
    const Evidence &read = p_run.evidence();
    const int64_t owed = p_run.scenario().authored_ticks - 1;
    if (read.newest_key != owed) {
        return law_broken(
            "the newest key is %d, the newest authored tick is %d",
            int(read.newest_key),
            int(owed)
        );
    }
    if (!read.value_at_newest.is_equal_approx(authored_at(read.newest_key))) {
        return law_broken(
            "key %d holds the value authored at %d",
            int(read.newest_key),
            int(read.value_at_newest.x)
        );
    }
    return law_held();
}

LawVerdict law_names_a_shown_tick(const AuthoringRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.displayed_tick < 0) {
        return law_broken("the display names no authoring tick at all");
    }
    if (!read.holds_displayed_key) {
        return law_broken(
            "the display names tick %d, which the buffer never recorded",
            int(read.displayed_tick)
        );
    }
    if (!read.value_at_displayed.is_equal_approx(
            authored_at(read.displayed_tick)
        )) {
        return law_broken(
            "tick %d holds the value authored at %d",
            int(read.displayed_tick),
            int(read.value_at_displayed.x)
        );
    }
    if (read.displayed_tick >= read.local_tick) {
        return law_broken(
            "the display names tick %d at local tick %d, so it does not trail",
            int(read.displayed_tick),
            int(read.local_tick)
        );
    }
    return law_held();
}

const AuthoringLaw L_KEYED = {
    "keyed",
    "a stamped stream keys its history by the frame's authoring tick rather "
    "than by the tick it arrived on",
    &law_keyed_by_authoring,
};

const AuthoringLaw L_NAMES = {
    "names",
    "the displayed authoring tick names a tick the buffer really holds, "
    "carrying the value authored there, and it trails the local clock",
    &law_names_a_shown_tick,
};

const AuthoringLaw LAWS[] = {L_KEYED, L_NAMES};

TEST_CASE(
    "[Networked][Display][SceneTree] the authoring tick keying laws hold"
) {
    const AuthoringScenario CORPUS[] = {
        an_undelayed_stream(),
        a_delayed_stream(),
    };
    for (const AuthoringScenario &scenario : CORPUS) {
        const AuthoringRun run(scenario);
        for (const AuthoringLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a stream keyed by the tick it "
    "arrived on reds keyed"
) {
    const AuthoringScenario scenario = a_delayed_stream();
    const AuthoringRun run(scenario, PLANT_THE_RECEIVE_TICK_IS_THE_KEY);
    NETW_CELL(L_KEYED, scenario);
    NETW_LAW_BREAKS(L_KEYED, run);
}

} // namespace TestNetwDisplayAuthoringTickLaws

#endif
