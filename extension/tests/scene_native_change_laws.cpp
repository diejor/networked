#include "support/fault_log.h"
#include "support/frame_drive.h"
#include "support/loopback_rig.h"
#include "support/netw_test.h"
#include "support/scripted_loads.h"
#include "support/stock_stand.h"

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/packed_scene.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session.hpp"

#if defined(NETW_TIER_HOSTED)

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/node2d.hpp>

using namespace godot;

namespace NetwTests {

namespace {

class FaultRecorder {
    Ref<netw_test::FaultLog> writer;

public:
    void open() {
        writer.instantiate();
        netw_test::FaultLog::attach(writer);
    }

    Array close() {
        if (writer.is_null()) {
            return Array();
        }
        netw_test::FaultLog::detach(writer);
        writer->error_count_containing(
            "A SceneTree scene change freed the scene that held this session"
        );
        Array drained;
        for (const netw_test::FaultLine &line : writer->recorded()) {
            Dictionary row;
            row["type"] = line.error ? Logger::ERROR_TYPE_ERROR
                                     : Logger::ERROR_TYPE_WARNING;
            row["text"] = line.text;
            drained.push_back(row);
        }
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
    CHECK(seen.first_text.contains(
        "A SceneTree scene change freed the scene that held this session"
    ));
}

namespace {

constexpr int LOADING_PUMPS = 30;

Ref<PackedScene> a_minted_level(const char *p_stem) {
    Node2D *shape = memnew(Node2D);
    shape->set_name(p_stem);
    const Ref<PackedScene> packed = netw_test::pack_stock_scene(
        shape,
        netw_test::mint_scene_path(p_stem)
    );
    memdelete(shape);
    return packed;
}

Ref<netw::NetwPromise> change_session_to(
    netw_test::LoopbackRig &p_rig,
    const Ref<PackedScene> &p_level
) {
    return p_rig.server()->scene_change_to_file(
        nullptr,
        p_level->get_path(),
        netw::NetwMultiplayer::SCENE_CHANGE_SESSION
    );
}

} // namespace

TEST_CASE(
    "[Networked][Scene][SceneTree] LS1 LS2 a change to a scene that is still "
    "loading returns at once, refuses a second change while it loads, and "
    "settles once the scene is in place"
) {
    netw_test::ScriptedLoads loads;
    const Ref<PackedScene> level = a_minted_level("Arena");
    const Ref<PackedScene> other = a_minted_level("Lobby");
    netw_test::LoopbackRig rig;
    rig.mount();

    const Ref<netw::NetwPromise> changed = change_session_to(rig, level);
    const Ref<netw::NetwPromise> second = change_session_to(rig, other);
    rig.pump(LOADING_PUMPS);

    CHECK_FALSE(changed->get_is_settled());
    CHECK(rig.server()->live_scenes().is_empty());
    CHECK(second->get_is_failed());
    NETW_CHECK_EQ(second->get_code(), int(ERR_BUSY));

    loads.finish();
    rig.pump();

    REQUIRE(changed->get_is_completed());
    const Ref<netw::NetwScene> arena = changed->get_result();
    CHECK(arena.is_valid());
    NETW_CHECK_EQ(rig.server()->live_scenes().size(), 1);
}

TEST_CASE(
    "[Networked][Scene][SceneTree] LS3 a session that ends while its next "
    "scene loads fails the change and applies nothing"
) {
    netw_test::ScriptedLoads loads;
    const Ref<PackedScene> level = a_minted_level("Arena");
    netw_test::LoopbackRig rig;
    rig.mount();
    const Ref<netw::NetwPromise> changed = change_session_to(rig, level);

    rig.server()->clear_session_state();
    loads.finish();
    rig.pump();

    CHECK(changed->get_is_failed());
    NETW_CHECK_EQ(changed->get_code(), int(ERR_UNAVAILABLE));
    CHECK(rig.server()->live_scenes().is_empty());
}

TEST_CASE(
    "[Networked][Scene][SceneTree] LS6 a change still loading when the server "
    "moves to another peer fails and applies nothing, even once the server "
    "moves back"
) {
    const bool RETURNS[] = {false, true};
    for (const bool returns : RETURNS) {
        INFO(doctest::String(returns ? "moved back" : "moved away"));
        netw_test::ScriptedLoads loads;
        const Ref<PackedScene> level = a_minted_level("Arena");
        netw_test::LoopbackRig rig(1, true);
        rig.mount();
        rig.join(-1, StringName("host"));
        rig.join(0, StringName("guest"));
        rig.pump(LOADING_PUMPS);
        const Ref<netw::NetwPromise> changed = change_session_to(rig, level);

        const int64_t successor = rig.client(0)->get_unique_id();
        REQUIRE(
            rig.server()->get_session()->transfer_server(successor) == OK
        );
        rig.pump(LOADING_PUMPS);
        REQUIRE(rig.client(0)->is_host());
        if (returns) {
            const int64_t former = rig.server()->get_unique_id();
            REQUIRE(
                rig.client(0)->get_session()->transfer_server(former) == OK
            );
            rig.pump(LOADING_PUMPS);
            REQUIRE(rig.server()->is_host());
        }
        loads.finish();
        rig.pump();

        CHECK(changed->get_is_failed());
        NETW_CHECK_EQ(changed->get_code(), int(ERR_UNAVAILABLE));
        CHECK(rig.server()->live_scenes().is_empty());
    }
}

TEST_CASE(
    "[Networked][Scene][SceneTree] LS4 LS5 a change to a scene the game "
    "already holds settles before the call returns, by path or as a "
    "PackedScene"
) {
    const Ref<PackedScene> held = netw::gd::load_scene(
        String("res://tests/support/declared_level.tscn")
    );
    REQUIRE(held.is_valid());
    const bool AS_PACKED[] = {false, true};
    for (const bool as_packed : AS_PACKED) {
        INFO(doctest::String(as_packed ? "packed" : "path"));
        netw_test::LoopbackRig rig;
        rig.mount();
        const Ref<netw::NetwPromise> changed = as_packed
            ? rig.server()->scene_change_to_packed(
                  nullptr,
                  held,
                  netw::NetwMultiplayer::SCENE_CHANGE_SESSION
              )
            : change_session_to(rig, held);
        CHECK(changed->get_is_completed());
    }
}

} // namespace NetwTests

#endif
