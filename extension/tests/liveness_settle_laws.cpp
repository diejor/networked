#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwLivenessSettleLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> a_hosting_session() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    core->session_set_role(NetwMultiplayer::ROLE_LISTEN_SERVER);
    return core;
}

struct SeatedEntity {
    Node *owner = nullptr;
    RID handle;
    int64_t route = 0;
};

SeatedEntity seat(const Ref<NetwMultiplayer> &p_core, const char *p_name) {
    SeatedEntity made;
    made.owner = memnew(Node);
    made.owner->set_name(p_name);
    made.handle = p_core->entity_create();
    made.route = p_core->entity_admit(made.handle);
    REQUIRE(made.route > 0);
    REQUIRE(p_core->entity_bind_node(made.handle, made.owner) == OK);
    return made;
}

TEST_CASE(
    "[Networked][Liveness][Hosted] LS1 a session that ends gives back the "
    "route registry and not only the wrapper book, so the next session opens "
    "on route one with nothing standing"
) {
    Ref<NetwMultiplayer> core = a_hosting_session();
    const RID entity = core->entity_create();
    const int64_t route = core->entity_admit(entity);
    CHECK(route > 0);
    NETW_CHECK_EQ(
        core->liveness_route_state(route),
        int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
    );

    core->emit_signal(StringName("session_ended"));
    core->settle_drain();

    NETW_CHECK_EQ(
        core->liveness_route_state(route),
        int64_t(NetwMultiplayer::ENTITY_STATE_UNKNOWN)
    );
    NETW_CHECK_EQ(core->liveness_get_routes().size(), 0);
    NETW_CHECK_EQ(core->liveness_reserve_route(), 1);
    CHECK_FALSE(core->wrapper_for_route(route).is_valid());
}

TEST_CASE(
    "[Networked][Liveness][Hosted] LS3 a watcher reads off the route what no "
    "lifecycle row carries, and the read dies with its subject, which is why "
    "a death hands on a model rather than a route to ask about"
) {
    Ref<NetwMultiplayer> core = a_hosting_session();
    const SeatedEntity watched = seat(core, "Watched");

    const Dictionary described = core->entity_describe(watched.route);
    NETW_CHECK_EQ(int64_t(described.get("route", -1)), watched.route);
    NETW_CHECK_EQ(
        int64_t(described.get("peer_id", -1)),
        core->entity_get_peer(watched.handle)
    );
    NETW_CHECK_EQ(
        int64_t(described.get("liveness", -1)),
        int64_t(NetwMultiplayer::ENTITY_STATE_LIVE)
    );
    CHECK(described.has("entity_id"));
    CHECK(described.has("stage"));
    CHECK(described.has("controller"));
    CHECK(described.has("layers"));

    CHECK(core->entity_describe(watched.route + 1000).is_empty());

    memdelete(watched.owner);
}

} // namespace TestNetwLivenessSettleLaws
