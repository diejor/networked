#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/call_args.hpp"
#include "netw/comp_table.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwRpcAdmissionLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;

constexpr int64_t RELAYED_SENDER = 7;

struct Seated {
    Node *owner = nullptr;
    RID handle;
    int64_t route = 0;
};

Ref<NetwMultiplayer> a_hosting_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

Seated seat(const Ref<NetwMultiplayer> &p_core, const char *p_name) {
    Seated made;
    made.owner = memnew(Node);
    made.owner->set_name(p_name);
    made.handle = p_core->entity_create();
    made.route = p_core->entity_admit(made.handle);
    REQUIRE(made.route > 0);
    REQUIRE(p_core->entity_bind_node(made.handle, made.owner) == OK);
    return made;
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RA1 a node argument crosses as the route that "
    "names it, because the receiving peer holds a different instance and can "
    "only rebind an address it can resolve for itself"
) {
    Ref<NetwMultiplayer> core = a_hosting_session();
    const Seated held = seat(core, "Held");

    const netw::call_args::Slot crossed
        = core->rpc_encoded_arg(Variant(held.owner));
    CHECK(crossed.addresses_node);
    if (crossed.addresses_node) {
        NETW_CHECK_EQ(crossed.node.route, held.route);
        NETW_CHECK_EQ(crossed.node.comp, 0);
    }

    SUBCASE("a value that is no entity crosses as itself") {
        const netw::call_args::Slot plain = core->rpc_encoded_arg(Variant(42));
        CHECK_FALSE(plain.addresses_node);
        NETW_CHECK_EQ(int64_t(plain.value), 42);
    }

    SUBCASE("a node in no entity crosses as itself") {
        Node *loose = memnew(Node);
        loose->set_name("Loose");
        const netw::call_args::Slot answered
            = core->rpc_encoded_arg(Variant(loose));
        CHECK_FALSE(answered.addresses_node);
        memdelete(loose);
    }

    memdelete(held.owner);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RA1b a component the table maps crosses as its "
    "one byte id and carries no path, which is the whole return on the "
    "component table"
) {
    Ref<NetwMultiplayer> core = a_hosting_session();
    const Seated held = seat(core, "Held");
    Node *gun = memnew(Node);
    gun->set_name("Gun");
    held.owner->add_child(gun);
    const Ref<NetwEntity> entity = NetwEntity::of(held.owner);
    REQUIRE(entity.is_valid());
    entity->register_component(gun);
    entity->hydrate_components();

    const netw::call_args::Slot crossed = core->rpc_encoded_arg(Variant(gun));

    REQUIRE(crossed.addresses_node);
    NETW_CHECK_EQ(crossed.node.comp, 1);
    CHECK(crossed.node.path.is_empty());

    memdelete(held.owner);
}

TEST_CASE(
    "[Networked][Rpc][Hosted] RA2 a relayed dispatch puts the sender back "
    "when it returns, so the stamp a handler reads never outlives the frame "
    "it belongs to"
) {
    Ref<NetwMultiplayer> core = a_hosting_session();
    const Seated target = seat(core, "Target");
    NETW_CHECK_EQ(core->rpc_get_relay_sender(), 0);

    PackedByteArray payload;
    payload.push_back(1);
    core->receive_carrier(
        netw::NetwMultiplayer::frame_pack(
            target.route,
            0,
            netw::wire::builtin_channel("CALL"),
            payload,
            String()
        ),
        RELAYED_SENDER,
        true,
        -1,
        -1
    );

    NETW_CHECK_EQ(core->rpc_get_relay_sender(), 0);

    core->clear_session_state();
    memdelete(target.owner);
}

} // namespace TestNetwRpcAdmissionLaws
