#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/server_info.hpp"
#include "netw/session_decl.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionDecl {

using namespace godot;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwServerInfo;
using netw_test::CallLog;

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

Callable provider(const CallLog &p_log, const StringName &p_tag) {
    return p_log.minting<NetwServerInfo>(p_tag);
}

bool probe_answers(NetwMultiplayer *p_api) {
    bool answered = false;
    p_api->probe_reply_payload(answered);
    return answered;
}

bool reports_ambiguity(const Ref<NetwMultiplayer> &p_api) {
    return p_api->declaration_book().report_unresolved(
        p_api.ptr(),
        netw::session_decl::KIND_SERVER_INFO,
        netw::session_decl::AMBIGUOUS,
        "a probe"
    );
}

} // namespace

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD1 a declaration made off-tree "
    "runs nothing and governs nothing until its scope node enters a branch, "
    "at which point the probe that session answers is the declared one"
) {
    Branch branch("HD1");
    const CallLog log;
    Node *scope = memnew(Node);
    scope->set_name(StringName("Session"));

    NETW_CHECK_EQ(
        Netw::configure_server_info(scope, provider(log, "info")),
        OK
    );
    NETW_CHECK_EQ(log.count("info"), 0);

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 0);

    branch.node->add_child(scope);

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD2 a provider is handed a "
    "record already built from the session it answers for, so it edits live "
    "state rather than reconstructing it"
) {
    Branch branch("HD2");
    const CallLog log;
    Node *scope = child_named(branch.node, "Session");
    Netw::configure_session(scope)->app_id(StringName("HD2App"));
    branch.api->config_settle();

    Netw::configure_server_info(scope, provider(log, "info"));

    CHECK(probe_answers(branch.api.ptr()));

    const Ref<NetwServerInfo> handed = log.args("info")[0];
    REQUIRE(handed.is_valid());
    CHECK(handed->get_app_id() == StringName("HD2App"));
    CHECK(handed->get_is_local_listener());
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD3 two branches each answer "
    "with the declaration made in their own branch, and neither reaches the "
    "other's"
) {
    Branch first("HD3First");
    Branch second("HD3Second");
    const CallLog log;

    Netw::configure_server_info(
        child_named(first.node, "Session"),
        provider(log, "first")
    );
    Netw::configure_server_info(
        child_named(second.node, "Session"),
        provider(log, "second")
    );

    CHECK(probe_answers(first.api.ptr()));
    CHECK(probe_answers(second.api.ptr()));
    NETW_CHECK_EQ(log.count("first"), 1);
    NETW_CHECK_EQ(log.count("second"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD4 an api installed on a "
    "branch after the declaring node entered it is discovered by the "
    "reconciliation a probe performs, with no re-declaration"
) {
    Node *branch = memnew(Node);
    branch->set_name(StringName("HD4"));
    netw::gd::scene_root()->add_child(branch);
    Node *scope = child_named(branch, "Session");
    const CallLog log;

    Netw::configure_server_info(scope, provider(log, "info"));

    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(branch->get_path());
    Ref<NetwMultiplayer> api;
    api.instantiate();
    api->session_set_inner(inner);
    netw::gd::scene_tree()->set_multiplayer(api, branch->get_path());

    CHECK(probe_answers(api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 1);

    netw::gd::scene_tree()->set_multiplayer(
        Ref<MultiplayerAPI>(),
        branch->get_path()
    );
    netw::gd::scene_root()->remove_child(branch);
    memdelete(branch);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD5 two live declarations on one "
    "session refuse the probe rather than picking the later entrant, report "
    "the conflict once, and answer again once one is cleared"
) {
    Branch branch("HD5");
    const CallLog log;
    Node *first = child_named(branch.node, "First");
    Node *second = child_named(branch.node, "Second");

    Netw::configure_server_info(first, provider(log, "first"));
    Netw::configure_server_info(second, provider(log, "second"));

    CHECK_FALSE(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("first"), 0);
    NETW_CHECK_EQ(log.count("second"), 0);
    CHECK_FALSE(reports_ambiguity(branch.api));

    Netw::configure_server_info(second, Callable());

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("first"), 1);
    NETW_CHECK_EQ(log.count("second"), 0);
    CHECK(reports_ambiguity(branch.api));
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD6 a scope node that leaves the "
    "branch refuses the probe rather than falling back to the built-in reply, "
    "and re-entering the same session restores it"
) {
    Branch branch("HD6");
    const CallLog log;
    Node *scope = child_named(branch.node, "Session");
    Netw::configure_server_info(scope, provider(log, "info"));

    CHECK(probe_answers(branch.api.ptr()));

    branch.node->remove_child(scope);

    CHECK_FALSE(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 1);

    branch.node->add_child(scope);

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 2);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD7 a scope node that moves to "
    "another session activates there and can never be invoked through the "
    "branch it left"
) {
    Branch first("HD7First");
    Branch second("HD7Second");
    const CallLog log;
    Node *scope = child_named(first.node, "Session");
    Netw::configure_server_info(scope, provider(log, "info"));

    CHECK(probe_answers(first.api.ptr()));

    first.node->remove_child(scope);
    second.node->add_child(scope);

    CHECK_FALSE(probe_answers(first.api.ptr()));
    CHECK(probe_answers(second.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 2);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD8 the same scope node replaces "
    "its declaration atomically, and clearing it selects the built-in reply "
    "rather than leaving the old provider standing"
) {
    Branch branch("HD8");
    const CallLog log;
    Node *scope = child_named(branch.node, "Session");

    Netw::configure_server_info(scope, provider(log, "old"));
    CHECK(probe_answers(branch.api.ptr()));

    Netw::configure_server_info(scope, provider(log, "new"));
    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("old"), 1);
    NETW_CHECK_EQ(log.count("new"), 1);

    Netw::configure_server_info(scope, Callable());
    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("old"), 1);
    NETW_CHECK_EQ(log.count("new"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD9 a provider that answers "
    "something that is not a NetwServerInfo refuses the probe, because an "
    "empty advertised reply reads as a working host"
) {
    Branch branch("HD9");
    const CallLog log;
    Node *scope = child_named(branch.node, "Session");
    Netw::configure_server_info(scope, log.callable("info"));

    bool answered = true;
    const PackedByteArray payload = branch.api->probe_reply_payload(answered);
    CHECK_FALSE(answered);
    CHECK(payload.is_empty());
    NETW_CHECK_EQ(log.count("info"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD10 a declaration its node "
    "never activated is freed with that node and leaves the session answering "
    "its built-in reply, with no report owed to anyone"
) {
    Branch branch("HD10");
    const CallLog log;
    Node *scope = memnew(Node);
    Netw::configure_server_info(scope, provider(log, "info"));
    memdelete(scope);

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("info"), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] HD11 a declaration with no scope "
    "node and one whose handler is already dead are both refused at the door, "
    "and the declaration the node already carries survives the rejected edit"
) {
    Branch branch("HD11");
    const CallLog log;
    Node *scope = child_named(branch.node, "Session");
    Netw::configure_server_info(scope, provider(log, "kept"));

    NETW_CHECK_EQ(
        Netw::configure_server_info(nullptr, log.callable("kept")),
        ERR_INVALID_PARAMETER
    );

    Node *doomed = memnew(Node);
    const Callable dead(doomed, StringName("get_name"));
    memdelete(doomed);
    NETW_CHECK_EQ(
        Netw::configure_server_info(scope, dead),
        ERR_INVALID_PARAMETER
    );

    CHECK(probe_answers(branch.api.ptr()));
    NETW_CHECK_EQ(log.count("kept"), 1);
}

} // namespace TestNetwSessionDecl
