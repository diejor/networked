#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/record.hpp"
#include "support/netw_call_log.h"

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
    owner->seat_at(p_core.ptr(), p_peer);
    return owner;
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

} // namespace TestNetwSpawnArm
