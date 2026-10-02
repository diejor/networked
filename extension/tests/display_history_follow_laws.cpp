#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/netw_cells.h"
#include "support/value_flow_stand.h"

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/tests.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/runtime.hpp"
#include "netw/repl/property_set.hpp"

namespace TestNetwDisplayHistoryFollowLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwNativeTests;
using netw::repl::PropertySet;
using netw_test::FlowPair;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;
using netw_test::WorldDecl;

const char *DRAWN_ON_A_VISUAL = R"(extends Node2D

func _init() -> void:
	var visual := Node2D.new()
	visual.name = &"Visual"
	add_child(visual)
	var e := Netw.configure_entity(self)
	e.interpolation.visual_root = ^"Visual"
	var smoothing := NetwInterpolate.new().lerp().smooth(0.0).to(&"position")
	Netw.configure_property(self, &"position").state().interpolate(smoothing)
)";

struct FollowScenario {
    String label;
    int ticks = 8;
    double stride = 10.0;
};

FollowScenario a_moving_entity() {
    FollowScenario scenario;
    scenario.label = "a-moving-entity";
    return scenario;
}

struct Side {
    int64_t pump = -1;
    bool follows = false;
    bool ring = true;
    int64_t held = 0;
    int64_t newest_tick = -1;
    double newest_x = 0.0;
    int64_t older_tick = -1;
    double older_x = 0.0;
    double sampled_x = 0.0;
};

struct FollowEvidence {
    Side author;
    Side remote;
    double authored_last_x = 0.0;
    HashSet<int64_t> authored_x;
};

Side read_side(NetwMultiplayer *p_core, Node *p_node) {
    Side side;
    const RID rid = NetwEntity::of(p_node)->get_rid_handle();
    netw::display::Runtime *runtime
        = NetwNativeTests::display_runtime_of(p_core, rid);
    REQUIRE(runtime != nullptr);
    side.pump = runtime->get_pump_mode();
    netw::display::Channel *channel
        = runtime->channel_named(StringName("position"));
    REQUIRE(channel != nullptr);
    netw::display::History &history = channel->display_history();
    side.follows = history.follows_table();
    side.ring = history.holds_ring();
    side.held = history.size();
    side.newest_tick = history.newest_tick();
    if (side.newest_tick < 0) {
        return side;
    }
    side.newest_x = Vector2(history.get_at(side.newest_tick)).x;
    side.older_tick = history.bracketing_ticks(side.newest_tick - 1).x;
    if (side.older_tick < 0) {
        return side;
    }
    side.older_x = Vector2(history.get_at(side.older_tick)).x;
    const Variant sampled = history.sample(
        side.older_tick,
        0.5,
        Variant(),
        1,
        false,
        0,
        0.0,
        Variant(),
        false
    );
    side.sampled_x = Vector2(sampled).x;
    return side;
}

class FollowRun {
    FollowScenario declared;
    FollowEvidence seen;

public:
    explicit FollowRun(const FollowScenario &p_scenario)
        : declared(p_scenario) {
        LoopbackRig rig(1);
        rig.declare_world(WorldDecl().clocked(30));
        rig.mount();
        NetwMultiplayer *server = netw_test::flow_core(rig.server());
        NetwMultiplayer *client = netw_test::flow_core(rig.client(0));
        const FlowPair pair
            = netw_test::stand_flow_pair(rig, DRAWN_ON_A_VISUAL, "Followed");
        rig.step_ticks(4);

        for (int step = 1; step <= declared.ticks; ++step) {
            const int64_t tick = netw_test::clock_tick(rig, -1) + 1;
            const double x = 100.0 + declared.stride * step;
            netw_test::author_at(
                pair.authored,
                StringName("position"),
                Vector2(x, 0.0),
                PropertySet::RECORD_STATE,
                tick
            );
            seen.authored_x.insert(int64_t(x));
            seen.authored_last_x = x;
            rig.step_ticks(1);
        }
        rig.step_ticks(2);
        seen.author = read_side(server, pair.authored);
        seen.remote = read_side(client, pair.mirror(0));
    }

    FollowRun(const FollowRun &) = delete;
    FollowRun &operator=(const FollowRun &) = delete;

    const FollowScenario &scenario() const {
        return declared;
    }

    const FollowEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<FollowRun> FollowLaw;

LawVerdict law_author_reads_the_pull(const FollowRun &p_run) {
    const FollowEvidence &seen = p_run.evidence();
    const Side &author = seen.author;
    if (author.pump != netw::display::PUMP_BRACKETED) {
        return law_broken(
            "the author pumps %d, not bracketed",
            int(author.pump)
        );
    }
    if (!author.follows) {
        return law_broken("the author's channel keeps a history of its own");
    }
    if (author.held < 2) {
        return law_broken(
            "the author's channel reads %d samples from the pull",
            int(author.held)
        );
    }
    if (!Math::is_equal_approx(author.newest_x, seen.authored_last_x)) {
        return law_broken(
            "the author's newest sample holds %.1f, not the last authored "
            "%.1f",
            author.newest_x,
            seen.authored_last_x
        );
    }
    return law_held();
}

LawVerdict law_remote_brackets(const FollowRun &p_run) {
    const FollowEvidence &seen = p_run.evidence();
    const Side &remote = seen.remote;
    if (remote.pump != netw::display::PUMP_REMOTE || !remote.follows) {
        return law_broken(
            "the remote copy pumps %d and follows a table %d",
            int(remote.pump),
            int(remote.follows)
        );
    }
    if (remote.older_tick < 0 || remote.newest_tick <= remote.older_tick) {
        return law_broken(
            "the remote history holds no two samples, %d and %d",
            int(remote.older_tick),
            int(remote.newest_tick)
        );
    }
    if (!seen.authored_x.has(int64_t(remote.older_x))
        || !seen.authored_x.has(int64_t(remote.newest_x))) {
        return law_broken(
            "the remote samples hold %.1f and %.1f, which were never "
            "authored",
            remote.older_x,
            remote.newest_x
        );
    }
    const double gap = double(remote.newest_tick - remote.older_tick);
    const double expected
        = remote.older_x + (remote.newest_x - remote.older_x) * (0.5 / gap);
    if (Math::abs(remote.sampled_x - expected) > 0.001) {
        return law_broken(
            "halfway past tick %d the channel reads %.3f, not %.3f between "
            "%.1f and %.1f",
            int(remote.older_tick),
            remote.sampled_x,
            expected,
            remote.older_x,
            remote.newest_x
        );
    }
    return law_held();
}

LawVerdict law_followed_channel_holds_no_ring(const FollowRun &p_run) {
    const FollowEvidence &seen = p_run.evidence();
    if (seen.author.ring || seen.remote.ring) {
        return law_broken(
            "a channel that follows its row holds a ring of its own, author "
            "%d and remote %d",
            int(seen.author.ring),
            int(seen.remote.ring)
        );
    }
    return law_held();
}

const FollowLaw L_FOLLOWED_CHANNEL_HOLDS_NO_RING = {
    "followed-channel-holds-no-ring",
    "a channel that reads its row's history allocates no tick ring, on the "
    "author and on a copy",
    &law_followed_channel_holds_no_ring,
};

const FollowLaw L_AUTHOR_READS_THE_PULL = {
    "author-reads-the-pull",
    "an author's bracketed channel reads the samples the pull appended to "
    "its row, and records none of its own",
    &law_author_reads_the_pull,
};

const FollowLaw L_REMOTE_BRACKETS = {
    "remote-brackets",
    "a remote channel reads its row's received history and interpolates "
    "between the two samples around its render tick",
    &law_remote_brackets,
};

const FollowLaw FOLLOW_LAWS[] = {
    L_AUTHOR_READS_THE_PULL,
    L_REMOTE_BRACKETS,
    L_FOLLOWED_CHANNEL_HOLDS_NO_RING,
};

TEST_CASE(
    "[Networked][Display][SceneTree] a display channel reads its row's "
    "history, the pull's on the author and the received one on a copy"
) {
    const FollowScenario scenario = a_moving_entity();
    const FollowRun run(scenario);
    for (const FollowLaw &law : FOLLOW_LAWS) {
        NETW_CELL(law, scenario);
        NETW_LAW_HOLDS(law, run);
    }
}

} // namespace TestNetwDisplayHistoryFollowLaws

#endif
