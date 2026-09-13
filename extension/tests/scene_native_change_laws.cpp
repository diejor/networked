#include "support/frame_drive.h"
#include "support/minted_script.h"
#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/node2d.hpp>

using namespace godot;

namespace NetwTests {

namespace {

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

Ref<PackedScene> pack_plain_scene(const char *p_name) {
    Node2D *prototype = memnew(Node2D);
    prototype->set_name(p_name);
    Ref<PackedScene> packed;
    packed.instantiate();
    const Error packed_ok = packed->pack(prototype);
    memdelete(prototype);
    return packed_ok == OK ? packed : Ref<PackedScene>();
}

struct ChangeEvidence {
    bool driven = false;
    bool built = false;
    bool online = false;
    int asked = -1;
    int reports = 0;
    int first_type = -1;
    String first_text;
    bool mount_alive_after = false;
};

ChangeEvidence &shell_evidence() {
    static ChangeEvidence evidence;
    return evidence;
}

ChangeEvidence &damage_evidence() {
    static ChangeEvidence evidence;
    return evidence;
}

struct NativeChangeRun {
    Ref<netw::NetwMultiplayer> core;
    Ref<netw::LocalMultiplayerPeer> peer;
    Ref<PackedScene> next_scene;
    Ref<MultiplayerAPI> displaced;
    ObjectID presented;
    ObjectID mount;
    bool mount_resolved = false;
    FaultRecorder faults;

    NativeChangeRun(bool p_host, bool p_mount_inside_presented) {
        Node *stage = memnew(Node);
        stage->set_name("PresentedScene");
        netw::gd::scene_root()->add_child(stage);
        presented = netw::gd::instance_id(stage);
        netw::gd::scene_tree()->set_current_scene(stage);

        Node *seat = memnew(Node);
        seat->set_name("SessionMount");
        if (p_mount_inside_presented) {
            stage->add_child(seat);
        } else {
            netw::gd::scene_root()->add_child(seat);
        }
        mount = netw::gd::instance_id(seat);

        core.instantiate();
        core->session_set_root(Callable(seat, "get_node").bind(NodePath(".")));
        mount_resolved = core->session_root() == seat;
        SceneTree *tree = netw::gd::scene_tree();
        displaced = tree->get_multiplayer();
        tree->set_multiplayer(
            Ref<MultiplayerAPI>(Object::cast_to<MultiplayerAPI>(core.ptr()))
        );
        if (p_host) {
            core->session_prepare_join(StringName("host"), Array());
            peer.instantiate();
            peer->create_server();
            core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
            core->NETW_API_VIRTUAL(poll)();
        }
        next_scene = pack_plain_scene("NextScene");
    }

    bool built() const {
        return core.is_valid() && next_scene.is_valid() && mount_resolved;
    }

    bool online() const {
        return core->session_get_state()
            == netw::NetwMultiplayer::SESSION_STATE_ONLINE;
    }

    bool mount_alive() const {
        return netw::gd::object_of(mount) != nullptr;
    }

    static void drop(ObjectID p_id) {
        Node *node = Object::cast_to<Node>(netw::gd::object_of(p_id));
        if (node == nullptr || node->is_queued_for_deletion()) {
            return;
        }
        if (node->get_parent() != nullptr) {
            node->get_parent()->remove_child(node);
        }
        memdelete(node);
    }

    ~NativeChangeRun() {
        SceneTree *tree = netw::gd::scene_tree();
        if (core.is_valid()) {
            core->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
            core->scene_dispose();
        }
        tree->set_multiplayer(displaced);
        const Node *standing = tree->get_current_scene();
        const ObjectID presented_now = standing != nullptr
            ? netw::gd::instance_id(standing)
            : ObjectID();
        if (mount != presented_now) {
            drop(mount);
        }
        if (presented != presented_now) {
            drop(presented);
        }
        peer.unref();
        core.unref();
        next_scene.unref();
    }
};

class NativeChangeScenario final : public netw_test::FrameScenario {
    NativeChangeRun *live = nullptr;
    int step = 0;

    void collect(ChangeEvidence &seen) {
        const Array drained = live->faults.close();
        for (int at = 0; at < drained.size(); ++at) {
            const Dictionary row = drained[at];
            const String text = row[String("text")];
            if (!text.contains("Netw.change_scene_to_file")) {
                continue;
            }
            if (seen.reports == 0) {
                seen.first_type = int(row[String("type")]);
                seen.first_text = text;
            }
            seen.reports += 1;
        }
        seen.mount_alive_after = live->mount_alive();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        ChangeEvidence &standing = shell_evidence();
        ChangeEvidence &orphaned = damage_evidence();
        if (step == 0) {
            live = new NativeChangeRun(true, false);
            standing.driven = true;
            standing.built = live->built();
            if (!standing.built) {
                delete live;
                live = nullptr;
                return false;
            }
            standing.online = live->online();
            live->faults.open();
            standing.asked = int(
                netw::gd::scene_tree()->change_scene_to_packed(live->next_scene)
            );
            ++step;
            return true;
        }
        if (step < 3) {
            ++step;
            return true;
        }
        if (step == 3) {
            collect(standing);
            orphaned.driven = true;
            orphaned.built = standing.built;
            orphaned.online = live->online();
            NativeChangeRun::drop(live->mount);
            live->faults.open();
            orphaned.asked = int(
                netw::gd::scene_tree()->change_scene_to_packed(live->next_scene)
            );
            ++step;
            return true;
        }
        if (step < 7) {
            ++step;
            return true;
        }
        collect(orphaned);
        delete live;
        live = nullptr;
        return false;
    }
};

NETW_FRAME_SCENARIO(NativeChangeScenario, native_change_scenario);

} // namespace

TEST_CASE(
    "[Networked][Scene][Frame] NC1 a session mounted outside the presented "
    "scene hears nothing when the engine replaces that scene, because nothing "
    "it owns was in the scene that went and a local presentation is a game's "
    "own business"
) {
    const ChangeEvidence &seen = shell_evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.built);
    REQUIRE(seen.online);
    NETW_CHECK_EQ(seen.asked, int(OK));
    CHECK(seen.mount_alive_after);
    NETW_CHECK_EQ(seen.reports, 0);
}

TEST_CASE(
    "[Networked][Scene][Frame] NC2 a session whose root has been freed reports "
    "a fault on the next engine scene change, because the mount and every "
    "world under it are gone while the peers it was talking to are not"
) {
    const ChangeEvidence &seen = damage_evidence();
    REQUIRE(seen.driven);
    REQUIRE(seen.built);
    REQUIRE(seen.online);
    CHECK_FALSE(seen.mount_alive_after);
    NETW_CHECK_EQ(seen.reports, 1);
    NETW_CHECK_EQ(seen.first_type, int(Logger::ERROR_TYPE_ERROR));
}

} // namespace NetwTests

#endif
