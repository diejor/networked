#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_decl.hpp"

namespace TestNetwConfigureLagcomp {

using namespace godot;
using netw::Netw;
using netw::NetwLagCompensationConfig;
using netw::NetwMultiplayer;

namespace {

struct Branch {
    Node *node = nullptr;
    Ref<NetwMultiplayer> api;

    explicit Branch(const char *p_name) {
        node = memnew(Node);
        node->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(node);
        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(node->get_path());
        api.instantiate();
        api->session_set_inner(inner);
        netw::gd::scene_tree()->set_multiplayer(api, node->get_path());
    }

    ~Branch() {
        netw::gd::scene_tree()->set_multiplayer(
            Ref<MultiplayerAPI>(),
            node->get_path()
        );
        node->get_parent()->remove_child(node);
        memdelete(node);
    }
};

Node *child_named(Node *p_parent, const char *p_name) {
    Node *node = memnew(Node);
    node->set_name(StringName(p_name));
    p_parent->add_child(node);
    return node;
}

Ref<NetwLagCompensationConfig> declared(
    Node *p_scope,
    int64_t p_future,
    int64_t p_gate
) {
    const Ref<NetwLagCompensationConfig> draft
        = Netw::configure_lagcomp(p_scope);
    if (draft.is_valid()) {
        draft->max_future_action(p_future)->input_gate_deadline(p_gate);
    }
    return draft;
}

} // namespace

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CG2 a second declaration on one "
    "node is an authoring error that answers nothing and leaves the first "
    "draft standing, because a node declares each configuration once"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] Condition \"standing.is_valid()\" is true. "
        "Returning: Ref<T>(). Netw.configure_lagcomp was called "
        "twice on '*'. Keep the Resource the first call returned "
        "and set its values there.",
        1
    );

    Branch branch("CG2");
    Node *scope = child_named(branch.node, "Session");

    const Ref<NetwLagCompensationConfig> draft = declared(scope, 4, 6);
    REQUIRE(draft.is_valid());

    const Ref<NetwLagCompensationConfig> again = Netw::configure_lagcomp(scope);

    CHECK(again.is_null());
    NETW_CHECK_EQ(int(draft->get_max_future_action_ticks()), 4);
    NETW_CHECK_EQ(int(draft->get_input_gate_deadline_ticks()), 6);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CG3 the fluent tail an author "
    "writes after the declaring call is the tail the session runs, because "
    "the payload is read at consumption rather than at declaration"
) {
    Branch branch("CG3");
    Node *scope = child_named(branch.node, "Session");

    Netw::configure_lagcomp(scope)->max_future_action(21)->input_gate_deadline(
        33
    );

    branch.api->config_settle();

    CHECK(
        branch.api->config_is_consumed(netw::session_decl::KIND_LAGCOMP_CONFIG)
    );
    NETW_CHECK_EQ(int(branch.api->get_max_future_action_ticks()), 21);
    NETW_CHECK_EQ(int(branch.api->get_input_gate_deadline_ticks()), 33);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CG4 a draft the session has "
    "consumed is sealed, so a later setter reports and changes nothing while "
    "the running values stand"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][session] Netw.configure_lagcomp set '*' on '*' "
        "after the session started, so the running value is "
        "unchanged. Set it before startup and finish the chain "
        "without awaiting.",
        1
    );

    Branch branch("CG4");
    Node *scope = child_named(branch.node, "Session");

    const Ref<NetwLagCompensationConfig> draft = declared(scope, 13, 19);
    REQUIRE(draft.is_valid());
    branch.api->config_settle();

    draft->set_max_future_action_ticks(2);
    draft->input_gate_deadline(2);

    NETW_CHECK_EQ(int(draft->get_max_future_action_ticks()), 13);
    NETW_CHECK_EQ(int(draft->get_input_gate_deadline_ticks()), 19);
    NETW_CHECK_EQ(int(branch.api->get_max_future_action_ticks()), 13);
    NETW_CHECK_EQ(int(branch.api->get_input_gate_deadline_ticks()), 19);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CG5 a second live declaration "
    "on the same branch is ambiguous, so the session consumes neither and "
    "keeps its own values rather than picking the later entrant"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] Netw.configure_lagcomp was called on more"
        " than one node (/root/CG5/One, /root/CG5/Two), so "
        "configuration was refused. Call it on one node per "
        "session.",
        1
    );

    Branch branch("CG5");
    declared(child_named(branch.node, "One"), 41, 43);
    declared(child_named(branch.node, "Two"), 41, 43);

    const int before = int(branch.api->get_max_future_action_ticks());

    branch.api->config_settle();

    CHECK_FALSE(
        branch.api->config_is_consumed(netw::session_decl::KIND_LAGCOMP_CONFIG)
    );
    NETW_CHECK_EQ(int(branch.api->get_max_future_action_ticks()), before);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CG6 a declaration whose scope "
    "left the branch before consumption blocks the built-in defaults instead "
    "of reading as no declaration at all"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_warning(
        "[warn][session] Netw.configure_lagcomp was called by "
        "Session, which left the tree before the session applied "
        "its settings, so configuration was refused. Keep that node"
        " in the tree until the session starts.",
        1
    );

    Branch branch("CG6");
    Node *scope = child_named(branch.node, "Session");
    declared(scope, 23, 29);

    branch.node->remove_child(scope);

    branch.api->config_settle();

    CHECK_FALSE(
        branch.api->config_is_consumed(netw::session_decl::KIND_LAGCOMP_CONFIG)
    );
    CHECK_FALSE(branch.api->lagcomp_is_configured());
    memdelete(scope);
}

} // namespace TestNetwConfigureLagcomp
