#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/display_stand.h"
#include "support/netw_cells.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/templates.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/runtime.hpp"
#include "netw/script/model.hpp"

namespace TestNetwDisplaySettleLaws {

using namespace godot;
using netw::Netw;
using netw::NetwDisplayHandle;
using netw::NetwEntity;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::display::Runtime;
namespace script_model = netw::script::model;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_THE_SETTLE_NEVER_RAN,
    PLANT_ONLY_THE_LAST_SUBJECT_REPOINTED,
};

struct SettleScenario {
    String label;
    int subjects = 1;
};

SettleScenario one_subject() {
    SettleScenario scenario;
    scenario.label = "one-subject";
    return scenario;
}

SettleScenario two_subjects_in_one_cascade() {
    SettleScenario scenario;
    scenario.label = "two-subjects-in-one-cascade";
    scenario.subjects = 2;
    return scenario;
}

const Vector2 START = Vector2(0.0, 0.0);
const Vector2 FINISH = Vector2(100.0, 0.0);

struct Evidence {
    Vector<bool> queued_before_settle;
    Vector<bool> queued_after_settle;
    Vector<Vector2> configured_visual;
    Vector<Vector2> retired_visual;
};

class SettleRun {
    SettleScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;
    Ref<NetwMultiplayer> core;
    Vector<netw_test::DisplaySubject> subjects;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    netw_test::DisplaySubject stand_up(int p_index) {
        return netw_test::stand_subject(core, p_index);
    }

    bool rebuild_queued(int64_t p_route) const {
        const Runtime *runtime
            = NetwNativeTests::display_runtime_at(core.ptr(), p_route);
        return runtime != nullptr && runtime->get_rebuild_queued();
    }

    void feed(Node *p_node) {
        record(p_node, START, 0);
        record(p_node, FINISH, 1);
    }

    void record(Node *p_node, const Vector2 &p_value, int64_t p_tick) {
        netw_test::record_sample(core, p_node, p_value, p_tick);
    }

    void display_at(int64_t p_tick, int p_offset, double p_factor) {
        netw_test::pump_at(core, p_tick, p_offset, p_factor);
    }

    void drive() {
        for (int index = 0; index < declared.subjects; ++index) {
            subjects.push_back(stand_up(index));
        }
        for (int index = 0; index < subjects.size(); ++index) {
            const bool skipped = plant_is(PLANT_ONLY_THE_LAST_SUBJECT_REPOINTED)
                && index + 1 < subjects.size();
            if (!skipped) {
                core->display_set_param(
                    subjects[index].entity->get_rid_handle(),
                    NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT,
                    NodePath("Second")
                );
            }
            read.queued_before_settle.push_back(
                rebuild_queued(subjects[index].route)
            );
        }

        if (!plant_is(PLANT_THE_SETTLE_NEVER_RAN)) {
            core->session_flush_deferred();
        }
        for (int index = 0; index < subjects.size(); ++index) {
            read.queued_after_settle.push_back(
                rebuild_queued(subjects[index].route)
            );
        }

        for (int index = 0; index < subjects.size(); ++index) {
            feed(subjects[index].body);
        }
        display_at(1, 1, 0.5);
        for (int index = 0; index < subjects.size(); ++index) {
            read.configured_visual.push_back(
                subjects[index].second->get_global_position()
            );
            read.retired_visual.push_back(
                subjects[index].first->get_global_position()
            );
        }
    }

public:
    explicit SettleRun(
        const SettleScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        core = netw_test::clocked_session();
        drive();
    }

    ~SettleRun() {
        for (int index = 0; index < subjects.size(); ++index) {
            netw_test::retire_subject(subjects[index]);
        }
    }

    SettleRun(const SettleRun &) = delete;
    SettleRun &operator=(const SettleRun &) = delete;

    const SettleScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return read;
    }
};

typedef LawRowFor<SettleRun> SettleLaw;

LawVerdict law_queued(const SettleRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.queued_before_settle.size() != p_run.scenario().subjects) {
        return law_broken(
            "%d runtimes for %d subjects",
            read.queued_before_settle.size(),
            p_run.scenario().subjects
        );
    }
    for (int index = 0; index < read.queued_before_settle.size(); ++index) {
        if (!read.queued_before_settle[index]) {
            return law_broken(
                "subject %d took a config write that invalidates its channel "
                "table and queued no rebuild",
                index
            );
        }
    }
    return law_held();
}

LawVerdict law_drained(const SettleRun &p_run) {
    const Evidence &read = p_run.evidence();
    for (int index = 0; index < read.queued_after_settle.size(); ++index) {
        if (read.queued_after_settle[index]) {
            return law_broken(
                "subject %d is still waiting for its rebuild after the settle",
                index
            );
        }
    }
    return law_held();
}

LawVerdict law_repointed(const SettleRun &p_run) {
    const Evidence &read = p_run.evidence();
    const Vector2 owed = START.lerp(FINISH, 0.5);
    for (int index = 0; index < read.configured_visual.size(); ++index) {
        if (!read.configured_visual[index].is_equal_approx(owed)) {
            return law_broken(
                "subject %d displays at %d, the configured visual is owed %d",
                index,
                int(read.configured_visual[index].x),
                int(owed.x)
            );
        }
        if (!read.retired_visual[index].is_equal_approx(START)) {
            return law_broken(
                "subject %d still writes the retired visual, at %d",
                index,
                int(read.retired_visual[index].x)
            );
        }
    }
    return law_held();
}

const SettleLaw L_QUEUED = {
    "queued",
    "a config write that invalidates the channel table queues the rebuild "
    "rather than running it, for every entity written",
    &law_queued,
};

const SettleLaw L_DRAINED = {
    "drained",
    "the session's own settle drains every queued rebuild, with no frame "
    "driven and nothing awaited",
    &law_drained,
};

const SettleLaw L_REPOINTED = {
    "repointed",
    "after the rebuild the display writes the configured visual and the one "
    "it retired stops receiving",
    &law_repointed,
};

const SettleLaw LAWS[] = {L_QUEUED, L_DRAINED, L_REPOINTED};

TEST_CASE("[Networked][Display][SceneTree] the display settle laws hold") {
    const SettleScenario CORPUS[] = {
        one_subject(),
        two_subjects_in_one_cascade(),
    };
    for (const SettleScenario &scenario : CORPUS) {
        const SettleRun run(scenario);
        for (const SettleLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a rebuild nobody drains reds "
    "drained"
) {
    const SettleScenario scenario = one_subject();
    const SettleRun run(scenario, PLANT_THE_SETTLE_NEVER_RAN);
    NETW_CELL(L_DRAINED, scenario);
    NETW_LAW_BREAKS(L_DRAINED, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] a cascade that repoints only its "
    "last entity reds queued"
) {
    const SettleScenario scenario = two_subjects_in_one_cascade();
    const SettleRun run(scenario, PLANT_ONLY_THE_LAST_SUBJECT_REPOINTED);
    NETW_CELL(L_QUEUED, scenario);
    NETW_LAW_BREAKS(L_QUEUED, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] recording a sample, pumping the runtime "
    "and writing a visual are three acts, and each leaves a row of its own "
    "kind on the route it ran for"
) {
    const Ref<NetwMultiplayer> core = netw_test::clocked_session();
    const netw_test::DisplaySubject subject = netw_test::stand_subject(core, 0);
    core->event_arm(true);

    netw_test::record_sample(core, subject.body, START, 0);
    netw_test::record_sample(core, subject.body, FINISH, 1);
    netw_test::pump_at(core, 1, 1, 0.5);

    int64_t records = 0;
    int64_t pumps = 0;
    int64_t writes = 0;
    const Array rows = core->event_ring(subject.route);
    for (int index = 0; index < rows.size(); ++index) {
        const Dictionary row = rows[index];
        REQUIRE(!row.is_empty());
        const int64_t event = int64_t(row[netw::event_key::event()]);
        records += event == netw::EventPlane::DISPLAY_RECORD ? 1 : 0;
        pumps += event == netw::EventPlane::DISPLAY_PUMP ? 1 : 0;
        writes += event == netw::EventPlane::DISPLAY_WRITE ? 1 : 0;
    }
    NETW_CHECK_EQ(records, int64_t(2));
    NETW_CHECK_EQ(pumps, int64_t(1));
    CHECK(writes > 0);
    NETW_CHECK_EQ(int64_t(core->event_ring(0).size()), int64_t(0));

    netw_test::retire_subject(subject);
}

TEST_CASE(
    "[Networked][Display][SceneTree] the handle and the flat verb write ONE "
    "record, so a setting authored before the session knew the entity is what "
    "the book publishes and a flat write reads back on the handle"
) {
    const Ref<NetwMultiplayer> core = netw_test::clocked_session();
    const netw_test::DisplaySubject subject = netw_test::stand_subject(core, 0);
    const RID entity = subject.entity->get_rid_handle();

    CHECK(
        NodePath(core->display_get_param(
            entity,
            NetwMultiplayer::DISPLAY_PARAM_VISUAL_ROOT
        ))
        == NodePath("First")
    );

    NETW_CHECK_EQ(
        int64_t(core->display_get_param(
            entity,
            NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS
        )),
        int64_t(6)
    );
    core->display_set_param(
        entity,
        NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS,
        5
    );
    NETW_CHECK_EQ(
        int64_t(core->display_get_param(
            entity,
            NetwMultiplayer::DISPLAY_PARAM_MAX_FORECAST_TICKS
        )),
        int64_t(5)
    );

    core->display_set_param(
        entity,
        NetwMultiplayer::DISPLAY_PARAM_TRACE_INTERVAL,
        20
    );
    NETW_CHECK_EQ(
        subject.entity->get_interpolation()->get_trace_interval(),
        int64_t(20)
    );

    netw_test::retire_subject(subject);
}

} // namespace TestNetwDisplaySettleLaws

#endif
