#include "support/frame_drive.h"
#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/callable.hpp"
#include "godot/collision_shape.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

using namespace godot;
using netw::NetwEntity;

namespace NetwTests {

namespace {

constexpr double POSE_TOLERANCE = 1e-4;
constexpr int SETTLE_FRAMES = 4;
constexpr int DESPAWN_FRAMES = 12;

enum Kind {
    KIND_RIGID_3D,
    KIND_CHARACTER_3D,
    KIND_COUNT,
};

const char *KIND_LABELS[KIND_COUNT] = {
    "RigidBody3D",
    "CharacterBody3D",
};

const char *DROPS[2] = {"drops_spawn_unresolved", "drops_despawn_unknown"};

struct DetachEvidence {
    bool driven = false;
    bool held_by_hand = false;
    bool session_detached = false;
    bool receiver_alive = false;
    bool receiver_detached = false;
    bool receiver_live = false;
    double origin_error = -1.0;
    double basis_error = -1.0;
    int64_t session_anchor = 0;
    int64_t receiver_anchor = 0;
    int64_t drops[2] = {-1, -1};
};

struct DetachBodyEvidence {
    DetachEvidence kinds[KIND_COUNT];
};

DetachBodyEvidence &detach_body_evidence() {
    static DetachBodyEvidence evidence;
    return evidence;
}

Node *build_avatar(const Variant &p_name) {
    Node3D *avatar = memnew(Node3D);
    avatar->set_name(String(p_name));
    avatar->set_position(Vector3(0.0, 1.0, 3.0));
    avatar->set_rotation(Vector3(0.0, real_t(Math_PI * 0.5), 0.0));
    Node3D *hand = memnew(Node3D);
    hand->set_name("Hand");
    hand->set_position(Vector3(0.5, 0.2, -0.4));
    hand->set_rotation(Vector3(real_t(Math_PI * 0.25), 0.0, 0.0));
    avatar->add_child(hand);
    return avatar;
}

CollisionShape3D *box_shape() {
    CollisionShape3D *shape = memnew(CollisionShape3D);
    Ref<BoxShape3D> box;
    box.instantiate();
    box->set_size(Vector3(0.4, 0.4, 0.4));
    shape->set_shape(box);
    return shape;
}

Node *build_held(const Variant &p_kind) {
    CollisionObject3D *body = nullptr;
    if (String(p_kind) == KIND_LABELS[KIND_CHARACTER_3D]) {
        body = memnew(CharacterBody3D);
    } else {
        RigidBody3D *rigid = memnew(RigidBody3D);
        rigid->set_gravity_scale(0.0);
        body = rigid;
    }
    body->set_name("Cube");
    body->set_collision_layer(1);
    body->set_collision_mask(0);
    body->set_position(Vector3(0.3, 0.0, 0.0));
    body->add_child(box_shape());
    NetwEntity::ensure(body)->set_on_parent_despawn(
        NetwEntity::PARENT_DESPAWN_DETACH
    );
    return body;
}

Array named(const char *p_name) {
    Array out;
    out.push_back(String(p_name));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

double basis_distance(const Basis &p_a, const Basis &p_b) {
    double worst = 0.0;
    for (int axis = 0; axis < 3; ++axis) {
        worst = MAX(
            worst,
            double((p_a.get_column(axis) - p_b.get_column(axis)).length())
        );
    }
    return worst;
}

Ref<netw::NetwMultiplayer> pair_session(
    const Ref<netw::LocalMultiplayerPeer> &p_peer
) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(NodePath("/"));
    Ref<netw::NetwMultiplayer> api
        = netw::NetwMultiplayer::make(inner, Ref<Script>());
    api->session_set_authority_peer(1);
    api->set("multiplayer_peer", p_peer);
    return api;
}

class Pair {
    Ref<netw::LocalLoopbackSession> link;
    Ref<netw::NetwMultiplayer> sides[2];
    Node *branches[2] = {nullptr, nullptr};

public:
    Node *arenas[2] = {nullptr, nullptr};

    Pair() {
        link.instantiate();
        sides[0] = pair_session(link->get_server_peer());
        sides[1] = pair_session(link->create_client_peer());
        pump();
        const char *names[2] = {"PairSession", "PairReceiver"};
        for (int side = 0; side < 2; ++side) {
            branches[side] = memnew(Node);
            branches[side]->set_name(names[side]);
            netw::gd::scene_root()->add_child(branches[side]);
            sides[side]->session_set_root(
                Callable(branches[side], "get_node").bind(NodePath("."))
            );
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(
                    Object::cast_to<MultiplayerAPI>(sides[side].ptr())
                ),
                branches[side]->get_path()
            );
            pump();
        }
        for (int side = 0; side < 2; ++side) {
            arenas[side] = memnew(Node);
            arenas[side]->set_name("Arena");
            branches[side]->add_child(arenas[side]);
        }
        pump();
    }

    ~Pair() {
        for (int side = 0; side < 2; ++side) {
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(),
                branches[side]->get_path()
            );
            netw::gd::scene_root()->remove_child(branches[side]);
            memdelete(branches[side]);
        }
        link->reset();
    }

    void pump(int p_times = 1) {
        for (int round = 0; round < p_times; ++round) {
            link->poll();
            sides[0]->poll();
            sides[1]->poll();
        }
    }

    netw::NetwMultiplayer *server() const {
        return sides[0].ptr();
    }

    netw::NetwMultiplayer *client(int) const {
        return sides[1].ptr();
    }

    Node *route_node(int p_route, int p_side = -1) const {
        netw::NetwMultiplayer *api = sides[p_side + 1].ptr();
        return api->entity_get_node(api->entity_from_route(p_route));
    }

    netw::spawn::Pipeline *spawn_plane(int p_side = -1) const {
        return sides[p_side + 1]->get_replication_plane()->get_spawn_pipeline();
    }

    int spawn_registered(
        const StringName &p_id,
        const Callable &p_build,
        const Array &p_args,
        const Array &p_arg_types,
        Node *p_parent
    ) {
        Array quantizers;
        quantizers.resize(p_arg_types.size());
        for (int side = 0; side < 2; ++side) {
            sides[side]->spawn_register_constructor(
                p_id,
                p_build,
                p_arg_types,
                quantizers
            );
        }
        const RID entity = server()->spawn_registered(p_id, p_args, nullptr);
        p_parent->add_child(server()->entity_get_node(entity));
        pump(6);
        return int(server()->entity_get_route(entity));
    }
};

class DetachBodyScenario final : public netw_test::FrameScenario {
    int kind = 0;
    int step = 0;
    Pair *rig = nullptr;
    Node *arenas[2] = {nullptr, nullptr};
    int avatar = 0;
    int cube = 0;
    ObjectID receiver_cube;
    int64_t counted[2] = {0, 0};

    void open() {
        rig = new Pair();
        arenas[0] = rig->arenas[0];
        arenas[1] = rig->arenas[1];
        avatar = rig->spawn_registered(
            StringName("avatar"),
            callable_mp_static(&build_avatar),
            named("Avatar"),
            one_type(),
            arenas[0]
        );
        cube = rig->spawn_registered(
            StringName("held"),
            callable_mp_static(&build_held),
            named(KIND_LABELS[kind]),
            one_type(),
            rig->route_node(avatar)->get_node_or_null(NodePath("Hand"))
        );
    }

    void despawn() {
        DetachEvidence &seen = detach_body_evidence().kinds[kind];
        Node *held = rig->route_node(cube, 0);
        Node *hand
            = rig->route_node(avatar, 0)->get_node_or_null(NodePath("Hand"));
        seen.held_by_hand = held != nullptr && held->get_parent() == hand;
        receiver_cube = netw::gd::instance_id(held);
        const Dictionary before = Dictionary(rig->spawn_plane(0)->counters());
        for (int at = 0; at < 2; ++at) {
            counted[at] = int64_t(before[StringName(DROPS[at])]);
        }
        NetwEntity::of(rig->route_node(avatar))
            ->despawn(Ref<netw::NetwDespawnOpts>());
        rig->pump(8);
    }

    void close() {
        DetachEvidence &seen = detach_body_evidence().kinds[kind];
        seen.driven = true;
        Node3D *session_cube = Object::cast_to<Node3D>(rig->route_node(cube));
        seen.session_detached = session_cube != nullptr
            && session_cube->get_parent() == arenas[0];
        Node3D *landed
            = Object::cast_to<Node3D>(netw::gd::object_of(receiver_cube));
        seen.receiver_alive = landed != nullptr;
        if (landed != nullptr) {
            seen.receiver_detached = landed->get_parent() == arenas[1];
            netw::NetwMultiplayer *api = rig->client(0);
            seen.receiver_live
                = int64_t(api->entity_get_state(api->entity_from_route(cube)))
                == int64_t(netw::NetwLivenessCore::STATE_LIVE);
            seen.receiver_anchor = int64_t(api->liveness_route_anchor(cube));
        }
        if (landed != nullptr && session_cube != nullptr) {
            const Transform3D there = session_cube->get_global_transform();
            const Transform3D here = landed->get_global_transform();
            seen.origin_error = double(here.origin.distance_to(there.origin));
            seen.basis_error = basis_distance(here.basis, there.basis);
        }
        seen.session_anchor
            = int64_t(rig->server()->liveness_route_anchor(cube));
        const Dictionary after = Dictionary(rig->spawn_plane(0)->counters());
        for (int at = 0; at < 2; ++at) {
            seen.drops[at]
                = int64_t(after[StringName(DROPS[at])]) - counted[at];
        }
        delete rig;
        rig = nullptr;
        arenas[0] = nullptr;
        arenas[1] = nullptr;
        receiver_cube = ObjectID();
        step = 0;
        ++kind;
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr || kind >= KIND_COUNT) {
            return false;
        }
        if (step == 0) {
            open();
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES) {
            rig->pump();
            ++step;
            return true;
        }
        if (step == SETTLE_FRAMES + 1) {
            despawn();
            ++step;
            return true;
        }
        if (step <= SETTLE_FRAMES + 1 + DESPAWN_FRAMES) {
            rig->pump();
            ++step;
            return true;
        }
        close();
        return kind < KIND_COUNT;
    }
};

NETW_FRAME_SCENARIO(DetachBodyScenario, detach_body_scenario);

} // namespace

TEST_CASE(
    "[Networked][Spawn][Frame] HC6 a detaching body held under an avatar's "
    "hand outlives the avatar's despawn on every peer, under the avatar's "
    "parent at the session's world pose, with no unresolved drop"
) {
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        const DetachEvidence &seen = detach_body_evidence().kinds[kind];
        NETW_FORMAT_TEXT(netw_kind_text, KIND_LABELS[kind]);
        CAPTURE(netw_kind_text);
        REQUIRE(seen.driven);
        CHECK(seen.held_by_hand);
        CHECK(seen.session_detached);
        CHECK(seen.receiver_alive);
        CHECK(seen.receiver_detached);
        CHECK(seen.receiver_live);
        NETW_CHECK_GE(seen.origin_error, 0.0);
        NETW_CHECK_LT(seen.origin_error, POSE_TOLERANCE);
        NETW_CHECK_LT(seen.basis_error, POSE_TOLERANCE);
        NETW_CHECK_EQ(seen.receiver_anchor, seen.session_anchor);
        NETW_CHECK_EQ(seen.drops[0], int64_t(0));
        NETW_CHECK_EQ(seen.drops[1], int64_t(0));
    }
}

} // namespace NetwTests

#endif
