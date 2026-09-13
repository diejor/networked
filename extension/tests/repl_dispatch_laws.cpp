#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/comp_table.hpp"

namespace TestNetwReplDispatch {

using namespace godot;
using netw::NetwCompTable;
using netw::NetwEntity;
using netw::NetwMultiplayer;

struct Stand {
    Ref<NetwMultiplayer> core;
    Ref<NetwEntity> entity;
    Node *root = nullptr;
    Node *child = nullptr;
    int64_t route = 0;
};

Stand a_stand() {
    Stand out;
    out.core.instantiate();
    out.root = memnew(Node);
    out.root->set_name("Player");
    out.child = memnew(Node);
    out.child->set_name("Gun");
    out.root->add_child(out.child);
    out.entity.instantiate();
    out.entity->attach_to(out.root);
    out.route = out.core->liveness_reserve_route();
    out.core->liveness_bind_route(out.route, out.entity.ptr());
    out.entity->register_component(out.child);
    out.entity->hydrate_components();
    return out;
}

void retire(Stand &r_stand) {
    memdelete(r_stand.root);
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD1 a frame addressed at the root or a "
    "mapped component resolves to the node the table names"
) {
    Stand stand = a_stand();

    NETW_CHECK_EQ(stand.core->repl_comp_node(stand.route, 0, ""), stand.root);
    const int64_t gun = stand.entity->comp_table().id_for_path(String("Gun"));
    NETW_CHECK_EQ(
        stand.core->repl_comp_node(stand.route, gun, ""),
        stand.child
    );
    NETW_CHECK_EQ(
        stand.core
            ->repl_comp_node(stand.route, NetwCompTable::FALLBACK_COMP, "Gun"),
        stand.child
    );

    const Dictionary drops = stand.core->repl_drop_stats();
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_no_node")]), int64_t(0));
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_traversal")]), int64_t(0));

    retire(stand);
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD2 a parent-relative path is refused "
    "by the address clamp before anything is resolved through it"
) {
    Stand stand = a_stand();
    Node *outside = memnew(Node);
    outside->set_name("Outside");
    outside->add_child(stand.root);

    ERR_PRINT_OFF;
    NETW_CHECK_EQ(
        stand.core->repl_comp_node(
            stand.route,
            NetwCompTable::FALLBACK_COMP,
            "../Outside"
        ),
        nullptr
    );
    ERR_PRINT_ON;
    NETW_CHECK_GT(
        int64_t(stand.core->repl_drop_stats()[StringName("drops_traversal")]),
        int64_t(0)
    );

    outside->remove_child(stand.root);
    memdelete(outside);
    retire(stand);
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD3 a relative path that is shape-safe "
    "but names nothing yet is counted as unresolved, not as hostile"
) {
    Stand stand = a_stand();

    NETW_CHECK_EQ(
        stand.core->repl_comp_node(
            stand.route,
            NetwCompTable::FALLBACK_COMP,
            "Later"
        ),
        nullptr
    );
    const Dictionary drops = stand.core->repl_drop_stats();
    NETW_CHECK_EQ(
        int64_t(drops[StringName("drops_comp_unresolved")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_traversal")]), int64_t(0));

    retire(stand);
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD4 a route nothing is bound to answers "
    "nothing and is counted as a missing node, never as a live one"
) {
    Stand stand = a_stand();

    NETW_CHECK_EQ(stand.core->repl_comp_node(999, 0, ""), nullptr);
    NETW_CHECK_EQ(
        int64_t(stand.core->repl_drop_stats()[StringName("drops_no_node")]),
        int64_t(1)
    );

    retire(stand);
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD5 a gate verdict lands on the drop "
    "class it names, and a verdict naming no class moves nothing"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();

    core->repl_note_gate_verdict(int64_t(ERR_DOES_NOT_EXIST));
    core->repl_note_gate_verdict(int64_t(ERR_SKIP));
    core->repl_note_gate_verdict(int64_t(ERR_UNAVAILABLE));
    core->repl_note_gate_verdict(int64_t(OK));
    core->repl_note_gate_verdict(int64_t(ERR_INVALID_DATA));
    core->repl_note_unknown_route();

    const Dictionary drops = core->repl_drop_stats();
    NETW_CHECK_EQ(
        int64_t(drops[StringName("drops_unknown_route")]),
        int64_t(2)
    );
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_not_live")]), int64_t(1));
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_no_node")]), int64_t(1));
    NETW_CHECK_EQ(int64_t(drops[StringName("drops_traversal")]), int64_t(0));
}

TEST_CASE(
    "[Networked][Replication][Hosted] RD6 a frame for a lingering route and "
    "a frame for a dead one are counted apart, because stragglers after a "
    "despawn are a race that ends and frames after that are a sender nobody "
    "told"
) {
    Stand lingering = a_stand();
    REQUIRE(
        lingering.core->liveness_linger(lingering.entity->get_rid_handle())
    );
    NETW_CHECK_EQ(
        int64_t(lingering.core->liveness_route_state(lingering.route)),
        int64_t(NetwMultiplayer::ENTITY_STATE_LINGERING)
    );
    NETW_CHECK_EQ(
        lingering.core->repl_comp_node(lingering.route, 0, ""),
        nullptr
    );
    const Dictionary after_linger = lingering.core->repl_drop_stats();
    NETW_CHECK_EQ(
        int64_t(after_linger[StringName("drops_lingering_route")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        int64_t(after_linger[StringName("drops_dead_route")]),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        int64_t(after_linger[StringName("drops_not_live")]),
        int64_t(1)
    );
    retire(lingering);

    Stand dead = a_stand();
    REQUIRE(dead.core->liveness_retire(dead.route));
    NETW_CHECK_EQ(dead.core->repl_comp_node(dead.route, 0, ""), nullptr);
    const Dictionary after_death = dead.core->repl_drop_stats();
    NETW_CHECK_EQ(
        int64_t(after_death[StringName("drops_dead_route")]),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        int64_t(after_death[StringName("drops_lingering_route")]),
        int64_t(0)
    );
    retire(dead);
}

} // namespace TestNetwReplDispatch
