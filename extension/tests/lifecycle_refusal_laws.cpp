#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/promise.hpp"
#include "netw/lifecycle/rule.hpp"
#include "support/minted_script.h"
#include "support/netw_cells.h"

#include <godot_cpp/classes/logger.hpp>

namespace TestLifecycleRefusalLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;
using netw_test::LoopbackRig;

const char *BODY_ID = "lifecycle_body";

constexpr const char *RECORDER_SOURCE = R"gd(extends Logger

var rows: Array[Dictionary] = []
var lock := Mutex.new()

func _log_error(function: String, file: String, line: int,
		code: String, rationale: String, editor_notify: bool,
		error_type: int, script_backtraces: Array[ScriptBacktrace]) -> void:
	lock.lock()
	rows.append({"type": error_type, "text": code + " " + rationale})
	lock.unlock()

func drain() -> Array[Dictionary]:
	lock.lock()
	var taken: Array[Dictionary] = rows.duplicate()
	rows.clear()
	lock.unlock()
	return taken
)gd";

class FaultRecorder {
    Ref<RefCounted> writer;

public:
    void open() {
        const Ref<Script> script = netw_test::minted_script(RECORDER_SOURCE);
        if (script.is_null()) {
            return;
        }
        writer = Ref<RefCounted>(Object::cast_to<RefCounted>(
            netw::gd::live_object(script->call("new"))
        ));
        if (writer.is_valid()) {
            OS::get_singleton()->add_logger(Ref<Logger>(writer));
        }
    }

    Array close() {
        if (writer.is_null()) {
            return Array();
        }
        OS::get_singleton()->remove_logger(Ref<Logger>(writer));
        const Array drained = writer->call("drain");
        writer.unref();
        return drained;
    }
};

Node *build_body(
    const Variant &p_name,
    const Variant &p_lifecycle,
    const Variant &p_transfer
) {
    Node3D *made = memnew(Node3D);
    made->set_name(String(p_name));
    Node3D *pocket = memnew(Node3D);
    pocket->set_name("Pocket");
    made->add_child(pocket);
    const Ref<NetwEntity> entity = NetwEntity::ensure(made);
    entity->set_lifecycle(NetwEntity::Lifecycle(int(p_lifecycle)));
    entity->set_transfer(NetwEntity::Transfer(int(p_transfer)));
    return made;
}

Array body_args(const char *p_name, bool p_declared, bool p_requestable) {
    Array out;
    out.push_back(String(p_name));
    out.push_back(
        int(p_declared ? NetwEntity::LIFECYCLE_CONTROLLER
                       : NetwEntity::LIFECYCLE_SESSION)
    );
    out.push_back(
        int(p_requestable ? NetwEntity::TRANSFER_REQUESTABLE
                          : NetwEntity::TRANSFER_FIXED)
    );
    return out;
}

Array body_types() {
    Array out;
    out.push_back(int(Variant::STRING));
    out.push_back(int(Variant::INT));
    out.push_back(int(Variant::INT));
    return out;
}

enum Verb {
    VERB_REPARENT,
    VERB_DESPAWN,
    VERB_SPAWN_UNDER,
    VERB_SPAWN,
};

enum Standing {
    STANDING_NONE,
    STANDING_CONTROLLER,
    STANDING_CLAIM_WAITING,
    STANDING_PREDICTED,
    STANDING_NATIVE,
    STANDING_PLAYER,
    STANDING_HALTED,
};

enum Target {
    TARGET_SHELF,
    TARGET_OUTSIDE,
    TARGET_POCKET,
};

struct RefusalScenario {
    String label;
    Verb verb = VERB_REPARENT;
    bool declared = false;
    Standing standing = STANDING_NONE;
    Target target = TARGET_SHELF;
    Error code = ERR_UNAUTHORIZED;
    const char *says = "";
};

struct RefusalEvidence {
    bool materialized = false;
    bool answered = false;
    Error code = OK;
    int errors = 0;
    String first_error;
    String root;
    bool local_unchanged = false;
    bool session_unchanged = false;
    int64_t gate_frames = -1;
};

RefusalScenario row(
    const char *p_label,
    Verb p_verb,
    bool p_declared,
    Standing p_standing,
    Target p_target,
    Error p_code,
    const char *p_says
) {
    RefusalScenario scenario;
    scenario.label = p_label;
    scenario.verb = p_verb;
    scenario.declared = p_declared;
    scenario.standing = p_standing;
    scenario.target = p_target;
    scenario.code = p_code;
    scenario.says = p_says;
    return scenario;
}

Node3D *plain_node(Node *p_parent, const char *p_name) {
    Node3D *made = memnew(Node3D);
    made->set_name(p_name);
    p_parent->add_child(made);
    return made;
}

int64_t gate_frames(LoopbackRig &p_rig) {
    const Dictionary counted = p_rig.spawn_plane(-1)->counters();
    return int64_t(counted.get(StringName("drops_spawn_bad_sender"), -1))
        + int64_t(counted.get(StringName("drops_spawn_unresolved"), -1));
}

void take_standing(
    LoopbackRig &p_rig,
    const RefusalScenario &p_scenario,
    int p_route
) {
    const Ref<NetwEntity> host = NetwEntity::of(p_rig.route_node(p_route));
    const Ref<NetwEntity> here = NetwEntity::of(p_rig.route_node(p_route, 0));
    switch (p_scenario.standing) {
        case STANDING_NONE:
            return;
        case STANDING_CLAIM_WAITING:
            p_rig.hold(0);
            here->claim_authority();
            p_rig.pump(4);
            return;
        case STANDING_CONTROLLER:
        case STANDING_PREDICTED:
        case STANDING_NATIVE:
        case STANDING_PLAYER:
        case STANDING_HALTED:
            host->set_controller(p_rig.peer_id(0));
            p_rig.pump(6);
            break;
    }
    if (p_scenario.standing == STANDING_HALTED) {
        p_rig.client(0)->structure_halt_enter();
    }
    if (p_scenario.standing == STANDING_PREDICTED) {
        here->get_prediction()->set_archetype(
            netw::NetwPredict::ARCHETYPE_SCRIPTED
        );
    }
    if (p_scenario.standing == STANDING_NATIVE) {
        MultiplayerSynchronizer *native = memnew(MultiplayerSynchronizer);
        native->set_name("Native");
        here->get_owner()->add_child(native);
        native->set_owner(here->get_owner());
        here->invalidate_synchronizers_cache();
    }
}

Node *target_of(
    const RefusalScenario &p_scenario,
    Node *p_body,
    Node *p_shelf,
    Node *p_outside
) {
    switch (p_scenario.target) {
        case TARGET_OUTSIDE:
            return p_outside;
        case TARGET_POCKET:
            return p_body->get_node_or_null(NodePath("Pocket"));
        case TARGET_SHELF:
            break;
    }
    return p_shelf;
}

void act(
    LoopbackRig &p_rig,
    const RefusalScenario &p_scenario,
    int p_route,
    Node *p_target,
    RefusalEvidence &r_seen
) {
    NetwMultiplayer *client = p_rig.client(0);
    const RID here = client->entity_from_route(p_route);
    Node *body = p_rig.route_node(p_route, 0);
    switch (p_scenario.verb) {
        case VERB_REPARENT: {
            const Ref<NetwPromise> answered
                = client->entity_reparent(here, p_target);
            r_seen.answered = answered.is_valid() && answered->get_is_failed();
            r_seen.code
                = answered.is_valid() ? answered->get_code() : Error(OK);
            return;
        }
        case VERB_DESPAWN:
            r_seen.code
                = client->entity_despawn(here, Ref<netw::NetwDespawnOpts>());
            r_seen.answered = r_seen.code != OK;
            return;
        case VERB_SPAWN_UNDER:
            r_seen.answered = NetwEntity::of(body)->spawn_under(
                                  p_target,
                                  StringName("Copy")
                              )
                == nullptr;
            r_seen.code = p_scenario.code;
            return;
        case VERB_SPAWN:
            r_seen.root = "Spawned";
            r_seen.answered
                = !client
                       ->spawn_registered(
                           StringName(BODY_ID),
                           body_args("Spawned", p_scenario.declared, false),
                           nullptr
                       )
                       .is_valid();
            r_seen.code = p_scenario.code;
            return;
    }
}

RefusalEvidence run_refusal(const RefusalScenario &p_scenario) {
    RefusalEvidence seen;
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    Node *shelves[2] = {
        plain_node(arena, "Shelf"),
        plain_node(rig.branch(0)->get_node_or_null(NodePath("Arena")), "Shelf"),
    };
    Node3D *outside = plain_node(netw::gd::scene_root(), "OutsideTheSession");

    if (p_scenario.standing == STANDING_PLAYER) {
        rig.join(0, StringName("alpha"));
    }
    const int route = rig.spawn_registered(
        StringName(BODY_ID),
        callable_mp_static(&build_body),
        body_args(
            "Crate",
            p_scenario.declared,
            p_scenario.standing == STANDING_CLAIM_WAITING
        ),
        body_types(),
        arena,
        p_scenario.standing == STANDING_PLAYER ? Variant(rig.player(0))
                                               : Variant()
    );
    Node *body = rig.route_node(route, 0);
    Node *held = rig.route_node(route);
    seen.materialized = body != nullptr && held != nullptr;
    if (!seen.materialized) {
        outside->get_parent()->remove_child(outside);
        memdelete(outside);
        return seen;
    }
    seen.root = String(body->get_name());
    take_standing(rig, p_scenario, route);

    Node *before_local = body->get_parent();
    Node *before_session = held->get_parent();
    const int64_t local_stage = NetwEntity::of(body)->get_stage();
    const int shelf_children = shelves[1]->get_child_count();
    const int64_t gated = gate_frames(rig);

    FaultRecorder recorder;
    recorder.open();
    act(rig,
        p_scenario,
        route,
        target_of(p_scenario, body, shelves[1], outside),
        seen);
    const Array faults = recorder.close();
    for (int at = 0; at < faults.size(); ++at) {
        const Dictionary fault = faults[at];
        if (int(fault[StringName("type")]) != int(Logger::ERROR_TYPE_ERROR)) {
            continue;
        }
        if (seen.errors == 0) {
            seen.first_error = String(fault[StringName("text")]);
        }
        seen.errors += 1;
    }

    rig.release(0);
    rig.pump(8);
    seen.local_unchanged = rig.route_node(route, 0) == body
        && body->get_parent() == before_local
        && NetwEntity::of(body)->get_stage() == local_stage
        && shelves[1]->get_child_count() == shelf_children
        && outside->get_child_count() == 0;
    seen.session_unchanged = rig.route_node(route) == held
        && held->get_parent() == before_session
        && shelves[0]->get_child_count() == 0;
    seen.gate_frames = gate_frames(rig) - gated;
    outside->get_parent()->remove_child(outside);
    memdelete(outside);
    return seen;
}

class RefusalRun {
    RefusalScenario declared;
    RefusalEvidence seen;

public:
    explicit RefusalRun(const RefusalScenario &p_scenario)
        : declared(p_scenario), seen(run_refusal(p_scenario)) {
    }

    const RefusalScenario &scenario() const {
        return declared;
    }

    const RefusalEvidence &evidence() const {
        return seen;
    }
};

typedef LawRowFor<RefusalRun> RefusalLaw;

LawVerdict law_names_the_setting(const RefusalRun &p_run) {
    const RefusalEvidence &seen = p_run.evidence();
    if (!seen.materialized) {
        return law_broken("the client never held the crate");
    }
    if (seen.errors != 1) {
        return law_broken("%d errors were raised, not one", seen.errors);
    }
    const String says = p_run.scenario().says;
    if (!seen.first_error.contains(says)
        || !seen.first_error.contains(seen.root)) {
        return law_broken(
            "the error names neither '%s' nor the root, it reads '%s'",
            says.utf8().get_data(),
            seen.first_error.utf8().get_data()
        );
    }
    return law_held();
}

const RefusalLaw L_NAMES_THE_SETTING = {
    "names-the-setting",
    "a static refusal raises one error naming the root and what the "
    "declaration allows",
    &law_names_the_setting,
};

LawVerdict law_answers_the_code(const RefusalRun &p_run) {
    const RefusalEvidence &seen = p_run.evidence();
    if (!seen.answered) {
        return law_broken("the verb answered as if it went through");
    }
    if (seen.code != p_run.scenario().code) {
        return law_broken(
            "the verb answered %d, not %d",
            int(seen.code),
            int(p_run.scenario().code)
        );
    }
    return law_held();
}

const RefusalLaw L_ANSWERS_THE_CODE = {
    "answers-the-code",
    "the verb answers its own failure value with the refusal's code",
    &law_answers_the_code,
};

LawVerdict law_changes_nothing(const RefusalRun &p_run) {
    const RefusalEvidence &seen = p_run.evidence();
    if (!seen.local_unchanged) {
        return law_broken("the refused verb changed the client's tree");
    }
    if (!seen.session_unchanged) {
        return law_broken("the refused verb changed the session's tree");
    }
    if (seen.gate_frames != 0) {
        return law_broken(
            "%d structure frames reached the session's gate",
            int(seen.gate_frames)
        );
    }
    return law_held();
}

const RefusalLaw L_CHANGES_NOTHING = {
    "changes-nothing",
    "a refused verb acts on no tree and sends no structure frame",
    &law_changes_nothing,
};

const RefusalLaw REFUSAL_LAWS[] = {
    L_NAMES_THE_SETTING,
    L_ANSWERS_THE_CODE,
    L_CHANGES_NOTHING,
};

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] a structure verb the local "
    "peer may not author is refused before anything moves or is sent, and "
    "its error names NetwEntity.lifecycle and the root"
) {
    const char *UNDECLARED
        = "NetwEntity.lifecycle = LIFECYCLE_CONTROLLER in its root's _init";
    const char *NOT_HIS = "neither the session authority nor its controller";
    const char *MOVING = "session authority is moving";
    const RefusalScenario CORPUS[] = {
        row("an-undeclared-move",
            VERB_REPARENT,
            false,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            UNDECLARED),
        row("a-move-by-a-peer-that-does-not-control-it",
            VERB_REPARENT,
            true,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            NOT_HIS),
        row("a-move-before-the-grant",
            VERB_REPARENT,
            true,
            STANDING_CLAIM_WAITING,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            "not granted yet"),
        row("a-predicted-move",
            VERB_REPARENT,
            true,
            STANDING_PREDICTED,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            "is predicted"),
        row("a-native-move",
            VERB_REPARENT,
            true,
            STANDING_NATIVE,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            "MultiplayerSynchronizer"),
        row("a-player-body-move",
            VERB_REPARENT,
            true,
            STANDING_PLAYER,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            "a player's body"),
        row("a-move-outside-the-session",
            VERB_REPARENT,
            true,
            STANDING_CONTROLLER,
            TARGET_OUTSIDE,
            ERR_INVALID_PARAMETER,
            "outside the session root"),
        row("a-move-into-itself",
            VERB_REPARENT,
            true,
            STANDING_CONTROLLER,
            TARGET_POCKET,
            ERR_INVALID_PARAMETER,
            "inside the entity itself"),
        row("an-undeclared-despawn",
            VERB_DESPAWN,
            false,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            UNDECLARED),
        row("a-despawn-by-a-peer-that-does-not-control-it",
            VERB_DESPAWN,
            true,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            NOT_HIS),
        row("an-undeclared-spawn-under",
            VERB_SPAWN_UNDER,
            false,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            UNDECLARED),
        row("an-undeclared-spawn",
            VERB_SPAWN,
            false,
            STANDING_NONE,
            TARGET_SHELF,
            ERR_UNAUTHORIZED,
            UNDECLARED),
        row("a-move-while-authority-moves",
            VERB_REPARENT,
            true,
            STANDING_HALTED,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            MOVING),
        row("a-despawn-while-authority-moves",
            VERB_DESPAWN,
            true,
            STANDING_HALTED,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            MOVING),
        row("a-spawn-under-while-authority-moves",
            VERB_SPAWN_UNDER,
            true,
            STANDING_HALTED,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            MOVING),
        row("a-spawn-while-authority-moves",
            VERB_SPAWN,
            true,
            STANDING_HALTED,
            TARGET_SHELF,
            ERR_UNAVAILABLE,
            MOVING),
    };
    for (const RefusalScenario &scenario : CORPUS) {
        const RefusalRun run(scenario);
        for (const RefusalLaw &law : REFUSAL_LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Lifecycle][Spawn][SceneTree] a raw Node.reparent by a peer "
    "that may not move the entity warns once per entity and leaves the "
    "session's copy where it was"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    Node *local_arena = rig.branch(0)->get_node_or_null(NodePath("Arena"));
    REQUIRE(local_arena != nullptr);
    plain_node(arena, "Shelf");
    Node *shelf = plain_node(local_arena, "Shelf");

    const int route = rig.spawn_registered(
        StringName(BODY_ID),
        callable_mp_static(&build_body),
        body_args("Crate", false, false),
        body_types(),
        arena
    );
    Node *body = rig.route_node(route, 0);
    Node *held = rig.route_node(route);
    REQUIRE(body != nullptr);
    REQUIRE(held != nullptr);

    FaultRecorder recorder;
    recorder.open();
    body->reparent(shelf);
    rig.pump(6);
    body->reparent(local_arena);
    rig.pump(6);
    body->reparent(shelf);
    rig.pump(6);
    const Array faults = recorder.close();

    int warnings = 0;
    int errors = 0;
    String warned;
    for (int at = 0; at < faults.size(); ++at) {
        const Dictionary fault = faults[at];
        const int type = int(fault[StringName("type")]);
        if (type == int(Logger::ERROR_TYPE_WARNING)) {
            warned = String(fault[StringName("text")]);
            warnings += 1;
        } else if (type == int(Logger::ERROR_TYPE_ERROR)) {
            errors += 1;
        }
    }
    NETW_CHECK_EQ(warnings, 1);
    NETW_CHECK_EQ(errors, 0);
    CHECK(warned.contains("NetwEntity.lifecycle"));
    CHECK(warned.contains("Crate"));
    const bool stays_local = body->get_parent() == shelf;
    const bool session_kept = held->get_parent() == arena;
    CHECK(stays_local);
    CHECK(session_kept);
}

int errors_raised(const Array &p_faults) {
    int errors = 0;
    for (int at = 0; at < p_faults.size(); ++at) {
        const Dictionary fault = p_faults[at];
        if (int(fault[StringName("type")]) == int(Logger::ERROR_TYPE_ERROR)) {
            errors += 1;
        }
    }
    return errors;
}

TEST_CASE(
    "[Networked][Lifecycle] a structure verb refused during a prediction "
    "replay answers ERR_BUSY and raises no error, because a replay never "
    "re-runs a verb its fresh tick already ran"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    netw::lifecycle::Facts replayed;
    replayed.kind = netw::lifecycle::Kind::SPAWN;
    replayed.peer_is_session = true;
    replayed.replaying = true;
    netw::lifecycle::Facts undeclared;
    undeclared.kind = netw::lifecycle::Kind::SPAWN;

    FaultRecorder recorder;
    recorder.open();
    String replay_text;
    const netw::lifecycle::Ruling replay_ruling = core->lifecycle_judge_verb(
        replayed,
        String("Netw.spawn"),
        nullptr,
        replay_text
    );
    const int replay_errors = errors_raised(recorder.close());

    recorder.open();
    String refused_text;
    const netw::lifecycle::Ruling refused = core->lifecycle_judge_verb(
        undeclared,
        String("Netw.spawn"),
        nullptr,
        refused_text
    );
    const int refused_errors = errors_raised(recorder.close());

    NETW_CHECK_EQ(int(replay_ruling.code), int(ERR_BUSY));
    NETW_CHECK_EQ(replay_errors, 0);
    NETW_CHECK_EQ(int(refused.code), int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(refused_errors, 1);
}

} // namespace TestLifecycleRefusalLaws

#endif
