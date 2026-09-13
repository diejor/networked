#include "support/loopback_rig.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "support/netw_call_log.h"

namespace TestEntityReparentSettleLaws {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;
using netw::NetwReparentOpts;

struct Stand {
    LoopbackRig rig;
    Node *home = nullptr;
    Node *away = nullptr;
    Node *body = nullptr;
    Ref<NetwEntity> entity;
    CallLog seen;

    Stand() : rig(0) {
        Node *scene = netw::gd::scene_root();
        REQUIRE(scene != nullptr);
        home = memnew(Node);
        home->set_name("Home");
        scene->add_child(home);
        away = memnew(Node);
        away->set_name("Away");
        scene->add_child(away);

        body = memnew(Node);
        body->set_name("Traveller");
        body->connect(StringName("ready"), seen.callable("ready"));
        entity = NetwEntity::ensure(body);
        entity->set_entity_id(StringName("traveller"));
        entity->arm(
            godot::Object::cast_to<netw::NetwMultiplayer>(rig.server())
        );
        entity->connect(StringName("reparented"), seen.callable("reparented"));
        home->add_child(body);
    }

    ~Stand() {
        home->get_parent()->remove_child(home);
        away->get_parent()->remove_child(away);
        memdelete(home);
        memdelete(away);
    }

    void settle() {
        rig.server()->session_flush_deferred();
    }
};

TEST_CASE(
    "[Networked][Entity] a move reports at the settle and not where "
    "it happens, because the subtree is still rebuilding there, and it "
    "reports the completed fact rather than the options that asked"
) {
    Stand stand;
    Ref<NetwReparentOpts> opts;
    opts.instantiate();

    NETW_CHECK_EQ(stand.seen.count("reparented"), 0);

    stand.entity->reparent_to(stand.away, opts);

    const bool moved = stand.body->get_parent() == stand.away;
    CHECK(moved);
    NETW_CHECK_EQ(stand.seen.count("reparented"), 0);

    stand.settle();

    NETW_CHECK_EQ(stand.seen.count("reparented"), 1);
    NETW_CHECK_EQ(stand.seen.args("reparented").size(), 0);
}

TEST_CASE(
    "[Networked][Entity] several hops before one settle are one move, "
    "because the report answers where the owner ended up and a parent it "
    "never rested under is not a fact about it"
) {
    Stand stand;
    Ref<NetwReparentOpts> first;
    first.instantiate();
    Ref<NetwReparentOpts> second;
    second.instantiate();

    stand.entity->reparent_to(stand.away, first);
    stand.entity->reparent_to(stand.home, second);

    stand.settle();

    NETW_CHECK_EQ(stand.seen.count("reparented"), 1);
    const bool landed = stand.body->get_parent() == stand.home;
    CHECK(landed);
}

TEST_CASE(
    "[Networked][Entity] a moved owner is initialized once and reports no "
    "move for standing up, because _ready is where a node builds itself "
    "and neither arriving nor moving is a rebirth"
) {
    Stand stand;
    NETW_CHECK_EQ(stand.seen.count("ready"), 1);
    NETW_CHECK_EQ(stand.seen.count("reparented"), 0);

    Ref<NetwReparentOpts> opts;
    opts.instantiate();
    stand.entity->reparent_to(stand.away, opts);
    stand.settle();

    NETW_CHECK_EQ(stand.seen.count("ready"), 1);
    NETW_CHECK_EQ(stand.seen.count("reparented"), 1);
}

} // namespace TestEntityReparentSettleLaws

#endif
