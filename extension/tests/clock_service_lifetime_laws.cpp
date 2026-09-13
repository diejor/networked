#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"

namespace TestClockServiceLifetimeLaws {

using namespace godot;
using netw::ClockEngine;
using netw::NetwMultiplayer;

struct Branch {
    netw::MultiplayerTree *tree = nullptr;
    NetwMultiplayer *core = nullptr;

    Branch() {
        tree = memnew(netw::MultiplayerTree);
        tree->set_name("ClockLifetimeTree");
        netw::gd::scene_root()->add_child(tree);
        core = tree->get_api().ptr();
        REQUIRE_MESSAGE(core != nullptr, "the tree has no native session");
    }

    netw::ClockEngine &clock() const {
        return core->clock_engine();
    }

    Node *mount(int p_tickrate) const {
        Node *scope = memnew(Node);
        scope->set_name("ClockScope");
        tree->add_child(scope);
        netw::Netw::configure_clock(scope)->tickrate(p_tickrate);
        return scope;
    }

    void unmount(Node *p_node) const {
        tree->remove_child(p_node);
        memdelete(p_node);
    }

    ~Branch() {
        netw::gd::scene_root()->remove_child(tree);
        memdelete(tree);
    }
};

TEST_CASE(
    "[Networked][Clock][SceneTree] CL1 a consumed clock configuration "
    "outlives the node that declared it, so a scene change never stops "
    "the clock"
) {
    Branch branch;
    CHECK_FALSE(branch.clock().get_configured());

    Node *clock = branch.mount(20);
    branch.core->config_settle();

    CHECK(branch.clock().get_configured());
    NETW_CHECK_EQ(branch.clock().get_tickrate(), 20);

    branch.unmount(clock);

    CHECK(branch.clock().get_configured());
    NETW_CHECK_EQ(branch.clock().get_tickrate(), 20);
}

TEST_CASE(
    "[Networked][Clock][SceneTree] CL2 a session initializes its clock "
    "once, so a second declaration is refused as late and the running "
    "tickrate stands"
) {
    Branch branch;
    Node *first = branch.mount(20);
    branch.core->config_settle();
    NETW_CHECK_EQ(branch.clock().get_tickrate(), 20);
    branch.unmount(first);

    Node *second = branch.mount(45);
    branch.core->config_settle();

    NETW_CHECK_EQ(branch.clock().get_tickrate(), 20);
    branch.unmount(second);
}

TEST_CASE(
    "[Networked][Clock][SceneTree] CL3 the session's own poll no longer "
    "advances the tick, because the physics frame is the one automatic "
    "pump"
) {
    Branch branch;
    Node *clock_node = branch.mount(30);
    branch.core->config_settle();

    netw::ClockEngine &clock = branch.clock();
    clock.mark_step(0.1);
    branch.core->poll();
    NETW_CHECK_EQ(clock.get_tick(), 0);

    branch.unmount(clock_node);
}

} // namespace TestClockServiceLifetimeLaws

#endif
