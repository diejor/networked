#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/resource_loader.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/transport.hpp"
#include "support/declared_seams.h"
#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwConnectRegistration {

using namespace godot;
using netw::NetwConnectHandle;
using netw::NetwMultiplayer;

namespace {

const char *HELD_SCRIPT = netw_test::gdsrc::TRANSPORT_THAT_HOLDS;
const char *RIVAL_SCRIPT = netw_test::gdsrc::TRANSPORT_CLAIMING_THE_SAME_CLASS;
const char *HELD_PEER_CLASS = "HeldMultiplayerPeer";
const char *STOCK_PEER_CLASS = "LocalMultiplayerPeer";

Ref<NetwMultiplayer> lone_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

Ref<Script> script_at(const char *p_path) {
    return netw_test::script_from(p_path);
}

void pump(const Ref<NetwMultiplayer> &p_core, int p_rounds) {
    for (int at = 0; at < p_rounds; at++) {
        p_core->poll();
    }
}

} // namespace

TEST_CASE(
    "[Networked][Connect][Registration] registering one script twice answers "
    "the standing handle rather than a second one, so a scene re-entering "
    "the tree cannot leave a session holding two providers it would then "
    "have to choose between"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);
    REQUIRE(held.is_valid());

    const RID first = core->transport_register(held);
    const RID again = core->transport_register(held);

    CHECK(first.is_valid());
    NETW_CHECK_EQ(int(again == first), 1);
    NETW_CHECK_EQ(
        int(core->transport_find(StringName(HELD_PEER_CLASS)) == first),
        1
    );

    int listed = 0;
    for (const Variant &entry : core->transport_list()) {
        listed += int(RID(entry) == first);
    }
    NETW_CHECK_EQ(listed, 1);

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] a second script claiming a peer "
    "class this session already registers is refused, which is what makes a "
    "peer class name exactly one provider and lets a snapshot carrying only "
    "the class name identify it"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);
    const Ref<Script> rival = script_at(RIVAL_SCRIPT);
    REQUIRE(held.is_valid());
    REQUIRE(rival.is_valid());

    const RID standing = core->transport_register(held);
    REQUIRE(standing.is_valid());
    const RID refused = core->transport_register(rival);

    CHECK_FALSE(refused.is_valid());
    NETW_CHECK_EQ(
        int(core->transport_find(StringName(HELD_PEER_CLASS)) == standing),
        1
    );
    CHECK(
        core->transport_get_param(
            standing,
            NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
        == String("Held")
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] the rival takes the class once the "
    "standing registration is withdrawn, so replacing a provider is an act a "
    "caller performs rather than one that happens underneath it"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);
    const Ref<Script> rival = script_at(RIVAL_SCRIPT);

    const RID standing = core->transport_register(held);
    NETW_CHECK_EQ(int(core->transport_unregister(standing)), int(OK));
    const RID taken = core->transport_register(rival);

    CHECK(taken.is_valid());
    NETW_CHECK_EQ(int(taken == standing), 0);
    CHECK(
        core->transport_get_param(
            taken,
            NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
        == String("Rival")
    );
    CHECK(
        core->transport_get_param(
            standing,
            NetwMultiplayer::TRANSPORT_PARAM_DISPLAY_NAME
        )
        == Variant()
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] a registration is the session's and "
    "not the process's, so a second session neither lists it nor resolves "
    "its handle, and a game that registers in one scene registers again in "
    "the next"
) {
    const Ref<NetwMultiplayer> first = lone_session();
    const Ref<NetwMultiplayer> second = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);

    const RID mine = first->transport_register(held);
    REQUIRE(mine.is_valid());

    CHECK_FALSE(second->transport_find(StringName(HELD_PEER_CLASS)).is_valid());
    CHECK(
        second->transport_get_param(
            mine,
            NetwMultiplayer::TRANSPORT_PARAM_PEER_CLASS
        )
        == Variant()
    );
    CHECK_FALSE(second->transport_list().has(mine));

    first->embed_dispose();
    second->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] disposing the session releases what "
    "it registered, because a registration outliving its owner is exactly "
    "the process-wide leak this ownership exists to close"
) {
    const Ref<Script> held = script_at(HELD_SCRIPT);
    RID mine;
    {
        const Ref<NetwMultiplayer> core = lone_session();
        mine = core->transport_register(held);
        REQUIRE(mine.is_valid());
        REQUIRE(netw::connect::transport_slot(mine) != nullptr);
        core->embed_dispose();
    }

    CHECK(netw::connect::transport_slot(mine) == nullptr);
}

TEST_CASE(
    "[Networked][Connect][Registration] the stock transports answer with no "
    "registration at all, and a session registering nothing still finds "
    "them, which is the zero-configuration path this band must not disturb"
) {
    const Ref<NetwMultiplayer> core = lone_session();

    const RID stock = core->transport_find(StringName(STOCK_PEER_CLASS));

    CHECK(stock.is_valid());
    NETW_CHECK_EQ(
        int(core->transport_unregister(stock)),
        int(ERR_DOES_NOT_EXIST)
    );
    NETW_CHECK_EQ(
        int(core->transport_find(StringName(STOCK_PEER_CLASS)) == stock),
        1
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] withdrawing a handle this session "
    "never minted, and withdrawing one twice, both answer "
    "ERR_DOES_NOT_EXIST rather than reaching another session's book"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwMultiplayer> other = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);

    const RID mine = core->transport_register(held);

    NETW_CHECK_EQ(
        int(other->transport_unregister(mine)),
        int(ERR_DOES_NOT_EXIST)
    );
    NETW_CHECK_EQ(int(core->transport_unregister(mine)), int(OK));
    NETW_CHECK_EQ(
        int(core->transport_unregister(mine)),
        int(ERR_DOES_NOT_EXIST)
    );
    NETW_CHECK_EQ(
        int(core->transport_unregister(RID())),
        int(ERR_DOES_NOT_EXIST)
    );

    core->embed_dispose();
    other->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] withdrawing a registration is not a "
    "disconnect, so a session serving a peer keeps serving it and only "
    "ordinary peer teardown ends a connection"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<Script> held = script_at(HELD_SCRIPT);

    Ref<netw::LocalMultiplayerPeer> server;
    server.instantiate();
    REQUIRE(server->create_server() == OK);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(server);
    REQUIRE(core->is_online());

    const RID mine = core->transport_register(held);
    NETW_CHECK_EQ(int(core->transport_unregister(mine)), int(OK));

    CHECK(core->is_online());
    CHECK(
        core->NETW_API_VIRTUAL(get_multiplayer_peer)()
        == Ref<MultiplayerPeer>(server)
    );

    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Registration] the view registers and withdraws by "
    "the script and the peer class, holding no handle of its own, and it "
    "reports what the session actually took"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    const Ref<NetwConnectHandle> view = core->get_connection();
    const Ref<Script> held = script_at(HELD_SCRIPT);
    const Ref<Script> rival = script_at(RIVAL_SCRIPT);
    REQUIRE(view.is_valid());

    CHECK(view->register_transport(held));
    CHECK(view->register_transport(held));
    CHECK_FALSE(view->register_transport(rival));

    const Dictionary snapshot = view->transport(held);
    CHECK(snapshot["peer_class"] == StringName(HELD_PEER_CLASS));
    CHECK(snapshot["display_name"] == String("Held"));

    CHECK(view->unregister_transport(StringName(HELD_PEER_CLASS)));
    CHECK(view->transport(held).is_empty());
    CHECK_FALSE(view->unregister_transport(StringName(HELD_PEER_CLASS)));
    CHECK_FALSE(view->unregister_transport(StringName(STOCK_PEER_CLASS)));

    core->embed_dispose();
}

} // namespace TestNetwConnectRegistration

#endif
