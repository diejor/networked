#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestLifecycleReplayFrameLaws {

using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr const char *AVATAR_ID = "lifecycle_replay_avatar";

constexpr const char *AVATAR_SOURCE = R"(extends Node3D

var beat := 0
var aim := 0
var trigger := -1
var trigger_calls := 0
var sparks := 0
var fresh_sparks := 0


func _init() -> void:
	var e := Netw.configure_entity(self)
	e.prediction.archetype = NetwPredict.ARCHETYPE_SCRIPTED
	Netw.configure_property(self, &"beat").state()
	Netw.configure_property(self, &"aim").input()
	Netw.configure_spawn(make_spark)


func make_spark() -> Node:
	var spark := Node3D.new()
	Netw.configure_entity(spark).lifecycle = NetwEntity.LIFECYCLE_CONTROLLER
	return spark


func _network_tick(_delta: float, tick: int, is_fresh: bool) -> void:
	beat += 1
	if tick != trigger:
		return
	trigger_calls += 1
	var spark := Netw.spawn(make_spark)
	if spark == null:
		return
	sparks += 1
	if is_fresh:
		fresh_sparks += 1
	get_parent().add_child(spark)
)";

constexpr int CLIENTS = 1;
constexpr int A = 0;
constexpr int TICKRATE = 30;
constexpr int WARM_FRAMES = 12;
constexpr int LEAD_TICKS = 8;
constexpr int PAST_TICKS = 4;
constexpr int ACK_DELAY_TICKS = 30;
constexpr int SETTLE_FRAMES = 2 * ACK_DELAY_TICKS;
constexpr int DEPTHS[] = {1, 3, 8};
constexpr int DEPTH_COUNT = int(sizeof(DEPTHS) / sizeof(int));

Script *avatar_script = nullptr;

Node *build_avatar(const Variant &p_name) {
    if (avatar_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(avatar_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct ReplayEvidence {
    bool driven = false;
    bool seated = false;
    int64_t trigger = -1;
    int windows[DEPTH_COUNT] = {};
    int64_t trigger_calls = 0;
    int64_t sparks = 0;
    int64_t fresh_sparks = 0;
    int64_t session_sparks = -1;
};

ReplayEvidence &replay_evidence() {
    static ReplayEvidence evidence;
    return evidence;
}

class ReplayScenario final : public netw_test::FrameScenario {
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    Node *host_arena = nullptr;
    int route = 0;
    int64_t trigger = -1;

    Node *avatar_at(int p_client) const {
        return stand->node_at(p_client, route);
    }

    int64_t tick_at(int p_client) const {
        return stand->session(p_client)->clock_engine().get_tick();
    }

    bool open() {
        script = netw_test::minted_script(AVATAR_SOURCE);
        if (script.is_null()) {
            return false;
        }
        avatar_script = script.ptr();
        stand = new netw_test::SimStand(CLIENTS);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(AVATAR_ID), callable_mp_static(&build_avatar));
        stand->mount();
        host_arena = stand->arena();
        stand->arm(TICKRATE);
        stand->session(A)->session_submit_join(StringName("a"), Array());
        stand->pump(4);
        NetwMultiplayer *server = stand->session(-1);
        const Ref<netw::NetwPlayer> owner
            = server->peer_get_player(stand->peer_id(A));
        Array args;
        args.push_back(String("Avatar"));
        const RID made = server->spawn_registered(
            StringName(AVATAR_ID),
            args,
            owner.ptr()
        );
        Node *built = server->entity_get_node(made);
        if (built == nullptr || host_arena == nullptr) {
            return false;
        }
        host_arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        if (avatar_at(A) == nullptr) {
            return false;
        }
        const Ref<netw::LocalLinkConditions> flight
            = netw::LocalLinkConditions::create(61);
        flight->set_latency_ms(double(ACK_DELAY_TICKS) * 1000.0 / TICKRATE);
        stand->loopback()
            ->set_link_conditions(stand->peer(-1), flight, stand->peer_id(A));
        replay_evidence().seated = true;
        return true;
    }

    void replay() {
        NetwMultiplayer *author = stand->session(A);
        netw::NetwPredictionEngine *engine = author->get_prediction_engine();
        const int64_t slot = engine->slot_of(NetwEntity::of(avatar_at(A)));
        for (int at = 0; at < DEPTH_COUNT; ++at) {
            replay_evidence().windows[at] = engine->replay_tick_window(
                slot,
                trigger - DEPTHS[at] + 1,
                trigger,
                Dictionary(),
                PackedStringArray(),
                Callable()
            );
        }
    }

    void read() {
        ReplayEvidence &evidence = replay_evidence();
        Node *avatar = avatar_at(A);
        evidence.trigger = trigger;
        evidence.trigger_calls = int64_t(avatar->get("trigger_calls"));
        evidence.sparks = int64_t(avatar->get("sparks"));
        evidence.fresh_sparks = int64_t(avatar->get("fresh_sparks"));
        evidence.session_sparks = host_arena->get_child_count() - 1;
    }

    void close() {
        replay_evidence().driven = true;
        delete stand;
        stand = nullptr;
        host_arena = nullptr;
        avatar_script = nullptr;
        script = Ref<Script>();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0 && !open()) {
            close();
            done = true;
            return false;
        }
        const int step = frame - WARM_FRAMES;
        if (step == 0) {
            trigger = tick_at(A) + LEAD_TICKS;
            avatar_at(A)->set("trigger", trigger);
        }
        if (step == LEAD_TICKS + PAST_TICKS) {
            replay();
        }
        if (step >= LEAD_TICKS + PAST_TICKS + SETTLE_FRAMES) {
            read();
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(ReplayScenario, lifecycle_replay_scenario);

TEST_CASE(
    "[Networked][Lifecycle][Frame] replay, a predicted avatar that calls "
    "Netw.spawn in _network_tick spawns once, on the fresh tick, whatever "
    "the replay depth"
) {
    const ReplayEvidence &evidence = replay_evidence();
    REQUIRE(evidence.driven);
    REQUIRE(evidence.seated);
    for (int at = 0; at < DEPTH_COUNT; ++at) {
        NETW_FORMAT_INT(depth_text, DEPTHS[at]);
        CAPTURE(depth_text);
        NETW_CHECK_GE(evidence.windows[at], DEPTHS[at]);
    }
    NETW_CHECK_GE(evidence.trigger_calls, int64_t(1 + DEPTH_COUNT));
    NETW_CHECK_EQ(evidence.sparks, int64_t(1));
    NETW_CHECK_EQ(evidence.fresh_sparks, int64_t(1));
    NETW_CHECK_EQ(evidence.session_sparks, int64_t(1));
}

} // namespace TestLifecycleReplayFrameLaws

#endif
