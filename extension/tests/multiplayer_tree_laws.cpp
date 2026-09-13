#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_core.hpp"

namespace TestMultiplayerTreeLaws {

using namespace godot;
using netw::MultiplayerTree;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;

struct Mounted {
    MultiplayerTree *tree = memnew(MultiplayerTree);

    explicit Mounted(const char *p_name) {
        tree->set_name(p_name);
        netw::gd::scene_root()->add_child(tree);
    }

    ~Mounted() {
        netw::gd::scene_root()->remove_child(tree);
        memdelete(tree);
    }
};

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT1 a tree owns one session for "
    "its whole "
    "life, and mounting installs that session on its own branch"
) {
    Mounted branch("MT1Tree");
    const Ref<NetwMultiplayer> api = branch.tree->get_api();
    REQUIRE(api.is_valid());

    Node *child = memnew(Node);
    branch.tree->add_child(child);

    NETW_CHECK_EQ(int(Netw::of(child) == api.ptr()), 1);
    NETW_CHECK_EQ(int(Netw::of(branch.tree) == api.ptr()), 1);
    NETW_CHECK_EQ(int(api->session_root() == branch.tree), 1);
    NETW_CHECK_EQ(int(branch.tree->get_api() == api), 1);

    branch.tree->remove_child(child);
    memdelete(child);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT2 two trees in one SceneTree "
    "are two "
    "sessions, so a node answers the branch it sits under rather than the "
    "first session that happened to mount"
) {
    Mounted first("MT2TreeA");
    Mounted second("MT2TreeB");

    Node *under_first = memnew(Node);
    first.tree->add_child(under_first);
    Node *under_second = memnew(Node);
    second.tree->add_child(under_second);

    NETW_CHECK_EQ(int(first.tree->get_api() != second.tree->get_api()), 1);
    NETW_CHECK_EQ(int(Netw::of(under_first) == first.tree->get_api().ptr()), 1);
    NETW_CHECK_EQ(
        int(Netw::of(under_second) == second.tree->get_api().ptr()),
        1
    );

    first.tree->remove_child(under_first);
    memdelete(under_first);
    second.tree->remove_child(under_second);
    memdelete(under_second);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT3 the exports reach the session "
    "as ONE "
    "whole fallback consumed once, so every setter is carried into it and a "
    "setter arriving after consumption is refused rather than half-rewriting "
    "a running session"
) {
    Mounted branch("MT3Tree");

    branch.tree->set_app_id(StringName("bomber-v2"));
    branch.tree->set_desired_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);
    branch.tree->set_link_conditions(Ref<netw::NetwLinkConditions>());

    branch.tree->get_api()->config_settle();

    const Ref<NetwSessionConfig> effective
        = branch.tree->get_api()->session_get_config();
    REQUIRE(effective.is_valid());
    NETW_CHECK_EQ(int(effective->get_app_id() == StringName("bomber-v2")), 1);
    NETW_CHECK_EQ(
        int(effective->get_desired_role()),
        int(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );

    branch.tree->set_app_id(StringName("bomber-v3"));

    NETW_CHECK_EQ(int(branch.tree->get_app_id() == StringName("bomber-v2")), 1);
    NETW_CHECK_EQ(
        int(branch.tree->get_api()->session_get_app_id()
            == StringName("bomber-v2")),
        1
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT4 the node's tag is the "
    "session's "
    "one tag, so an unnamed build is still gated on its wire"
) {
    NETW_CHECK_EQ(
        MultiplayerTree::app_tag_of(StringName()),
        netw::SessionCore::compute_app_tag(StringName())
    );
    NETW_CHECK_EQ(int(MultiplayerTree::app_tag_of(StringName("")) != 0), 1);

    const int64_t tagged = MultiplayerTree::app_tag_of(StringName("bomber-v2"));
    NETW_CHECK_EQ(
        tagged,
        netw::SessionCore::compute_app_tag(StringName("bomber-v2"))
    );
    NETW_CHECK_EQ(int(tagged != 0), 1);
    NETW_CHECK_EQ(
        int(tagged == MultiplayerTree::app_tag_of(StringName("bomber-v2"))),
        1
    );
    NETW_CHECK_EQ(
        int(tagged != MultiplayerTree::app_tag_of(StringName("bomber-v3"))),
        1
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT5 a raised sibling is a "
    "dedicated server "
    "that does not host itself, and it carries its own session"
) {
    Mounted branch("MT5Tree");
    branch.tree->set_peer_class(StringName("ENetMultiplayerPeer"));

    MultiplayerTree *server = branch.tree->raise_embedded_server();
    REQUIRE(server != nullptr);

    NETW_CHECK_EQ(
        int(server->get_desired_role()),
        int(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );
    NETW_CHECK_EQ(int(server->get_auto_host_headless()), 0);
    NETW_CHECK_EQ(
        int(server->get_peer_class() == branch.tree->get_peer_class()),
        1
    );
    NETW_CHECK_EQ(int(server->get_api() != branch.tree->get_api()), 1);
    NETW_CHECK_EQ(int(String(server->get_name()) == String("Server")), 1);

    Node *parent = server->get_parent();
    REQUIRE(parent != nullptr);
    parent->remove_child(server);
    memdelete(server);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] MT6 a tree with no parent raises "
    "no sibling "
    "rather than leaking one nothing owns"
) {
    MultiplayerTree *orphan = memnew(MultiplayerTree);
    NETW_CHECK_EQ(int(orphan->raise_embedded_server() == nullptr), 1);
    memdelete(orphan);
}

} // namespace TestMultiplayerTreeLaws
