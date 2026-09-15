#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

#if defined(NETW_TIER_HOSTED)
#include "support/loopback_rig.h"
#endif

namespace TestNetwSpawnArm {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;
using netw::NetwParticipant;
using netw::spawn::Record;
using netw_test::CallLog;

Ref<NetwParticipant> seated(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_peer
) {
    Ref<NetwParticipant> owner;
    owner.instantiate();
    p_core->participant_adopt(p_peer, owner);
    p_core->participant_admit(p_peer);
    return owner;
}

Ref<NetwParticipant> a_stranger(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_peer
) {
    Ref<NetwParticipant> stranger;
    stranger.instantiate();
    stranger->bind_to(p_core.ptr(), p_peer);
    return stranger;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM1 an arm reserves a route, names the entity "
    "after its recipe, and copies the identity onto the record"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record record;
    record.set_recipe(netw::spawn::Book::RECIPE_FN);
    record.set_fn_method(StringName("make_crate"));

    const Ref<NetwEntity> entity = core->spawn_arm_identity(
        &record,
        node,
        Ref<NetwParticipant>(),
        log.answering("declare", int64_t(OK))
    );

    REQUIRE(entity.is_valid());
    NETW_CHECK_GT(entity->get_route(), int64_t(0));
    NETW_CHECK_EQ(record.get_route(), entity->get_route());
    NETW_CHECK_EQ(record.node(), node);
    CHECK(
        bool(
            String(entity->get_entity_id())
            == String("make_crate@") + String::num_int64(entity->get_route())
        )
    );
    CHECK(
        bool(String(record.get_entity_id()) == String(entity->get_entity_id()))
    );
    NETW_CHECK_EQ(log.count("declare"), 1);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM2 an owner stamps both the owning peer and "
    "the controller, and an entity that already has an id keeps it"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    const Ref<NetwEntity> pre = NetwEntity::ensure(node);
    pre->set_entity_id(StringName("chosen"));
    Record record;

    const Ref<NetwEntity> entity = core->spawn_arm_identity(
        &record,
        node,
        seated(core, 4),
        log.answering("declare", int64_t(OK))
    );

    REQUIRE(entity.is_valid());
    CHECK(bool(String(entity->get_entity_id()) == String("chosen")));
    NETW_CHECK_EQ(entity->get_peer_id(), int64_t(4));
    NETW_CHECK_EQ(entity->get_controller(), int64_t(4));
    NETW_CHECK_EQ(record.get_peer_id(), int64_t(4));
    NETW_CHECK_EQ(record.get_controller(), int64_t(4));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM3 a refused declaration puts every identity "
    "field back the way it found it, and arms nothing"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    const Ref<NetwEntity> pre = NetwEntity::ensure(node);
    pre->set_entity_id(StringName("held"));
    pre->set_peer_id(2);
    pre->set_controller(3);
    const int64_t held_route = pre->get_route();

    Record scratch;
    const Ref<NetwEntity> refused = core->spawn_arm_identity(
        &scratch,
        node,
        seated(core, 8),
        log.answering("declare", int64_t(ERR_UNAUTHORIZED))
    );

    CHECK(refused.is_null());
    NETW_CHECK_EQ(log.count("declare"), 1);
    CHECK(bool(String(pre->get_entity_id()) == String("held")));
    NETW_CHECK_EQ(pre->get_peer_id(), int64_t(2));
    NETW_CHECK_EQ(pre->get_controller(), int64_t(3));
    NETW_CHECK_EQ(pre->get_route(), held_route);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM4 an arm with nothing to arm answers "
    "nothing, and a caller that declares nothing is not refused"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *node = memnew(Node);
    node->set_name("Crate");

    Record scratch;
    CHECK(core->spawn_arm_identity(
                  nullptr,
                  node,
                  Ref<NetwParticipant>(),
                  Callable()
    )
              .is_null());
    CHECK(core->spawn_arm_identity(
                  &scratch,
                  nullptr,
                  Ref<NetwParticipant>(),
                  Callable()
    )
              .is_null());
    CHECK(core->spawn_arm_identity(
                  &scratch,
                  node,
                  Ref<NetwParticipant>(),
                  Callable()
    )
              .is_valid());

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] SM5 an arm naming a participant this session "
    "does not hold is refused outright, so a stale handle cannot stamp a peer "
    "that has gone"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    CallLog log;
    Node *node = memnew(Node);
    node->set_name("Crate");
    Record record;

    const Ref<NetwParticipant> stranger = a_stranger(core, 9);
    CHECK_FALSE(core->participant_holds(stranger));
    const Ref<NetwEntity> refused = core->spawn_arm_identity(
        &record,
        node,
        stranger,
        log.answering("declare", int64_t(OK))
    );

    CHECK(refused.is_null());
    NETW_CHECK_EQ(log.count("declare"), 0);
    NETW_CHECK_EQ(record.get_route(), int64_t(0));
    const Ref<NetwEntity> held = NetwEntity::of(node);
    const int64_t stamped = held.is_valid() ? held->get_peer_id() : 0;
    NETW_CHECK_EQ(stamped, int64_t(0));

    const Ref<NetwParticipant> admitted = seated(core, 9);
    CHECK(core->participant_holds(admitted));
    const Ref<NetwEntity> armed = core->spawn_arm_identity(
        &record,
        node,
        admitted,
        log.answering("declare", int64_t(OK))
    );
    REQUIRE(armed.is_valid());
    NETW_CHECK_EQ(armed->get_peer_id(), int64_t(9));

    memdelete(node);
}

#if defined(NETW_TIER_HOSTED)

Node *build_orphan_avatar() {
    Node *body = memnew(Node);
    body->set_name("Avatar");
    return body;
}

TEST_CASE(
    "[Networked][Spawn][SceneTree] SM6 an orphan armed for a participant is "
    "discarded rather than replicated when that participant leaves before the "
    "body is mounted"
) {
    netw_test::LoopbackRig rig(0);
    rig.mount();
    NetwMultiplayer *server = rig.server();

    Ref<NetwParticipant> leaving;
    leaving.instantiate();
    server->participant_adopt(77, leaving);
    server->participant_admit(77);
    REQUIRE(server->participant_holds(leaving));

    rig.register_constructor(
        server,
        StringName("orphan_avatar"),
        callable_mp_static(&build_orphan_avatar),
        Array()
    );
    const RID armed = server->spawn_registered(
        StringName("orphan_avatar"),
        Array(),
        leaving.ptr()
    );
    REQUIRE(armed.is_valid());
    Node *body = server->entity_get_node(armed);
    REQUIRE(body != nullptr);
    const int route = int(server->entity_get_route(armed));
    netw::spawn::Pipeline *pipeline = rig.spawn_plane();
    REQUIRE(pipeline != nullptr);
    const bool orphaned = !body->is_inside_tree();
    CHECK(orphaned);

    server->participant_release_membership(leaving);
    const bool still_held = server->participant_holds(leaving);
    CHECK_FALSE(still_held);

    rig.branch(-1)->add_child(body);
    rig.pump(6);

    const bool published = pipeline->owns_spawned_route(route);
    CHECK_FALSE(published);
}

#endif

} // namespace TestNetwSpawnArm
