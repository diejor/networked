#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_tree.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_decl.hpp"

namespace TestNetwConfigureSession {

using namespace godot;
using netw::MultiplayerTree;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;

namespace {

struct Mounted {
    MultiplayerTree *tree = nullptr;

    explicit Mounted(const char *p_name) {
        tree = memnew(MultiplayerTree);
        tree->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(tree);
    }

    ~Mounted() {
        tree->get_parent()->remove_child(tree);
        memdelete(tree);
    }

    NetwMultiplayer *api() const {
        return tree->get_api().ptr();
    }
};

Node *child_named(Node *p_parent, const char *p_name) {
    Node *node = memnew(Node);
    node->set_name(StringName(p_name));
    p_parent->add_child(node);
    return node;
}

} // namespace

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS1 an explicit declaration "
    "replaces the WHOLE tree export, so a field the declaration left at its "
    "default is the default rather than the tree's value"
) {
    Mounted branch("CS1Tree");
    branch.tree->set_app_id(StringName("from-the-tree"));
    branch.tree->set_desired_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);

    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app_id(StringName("from-the-declaration"));

    branch.api()->config_settle();

    CHECK(
        branch.api()->session_get_app_id() == StringName("from-the-declaration")
    );
    NETW_CHECK_EQ(
        int(branch.api()->session_get_authored_role()),
        int(NetwMultiplayer::ROLE_LISTEN_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS2 no explicit declaration "
    "leaves the tree export as the one configuration, consumed whole"
) {
    Mounted branch("CS2Tree");
    branch.tree->set_app_id(StringName("only-the-tree"));
    branch.tree->set_desired_role(NetwMultiplayer::ROLE_DEDICATED_SERVER);

    branch.api()->config_settle();

    CHECK(branch.api()->session_get_app_id() == StringName("only-the-tree"));
    NETW_CHECK_EQ(
        int(branch.api()->session_get_authored_role()),
        int(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS3 the fluent tail reaches the "
    "auth app tag and not only the stored app id, because both readers are "
    "handed the same owned value at consumption"
) {
    Mounted branch("CS3Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app_id(StringName("tagged-arena"));

    branch.api()->config_settle();

    NETW_CHECK_EQ(
        branch.api()->auth_app_tag_of(),
        NetwMultiplayer::session_app_tag(StringName("tagged-arena"))
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS4 a construction role "
    "constrains the effective role after consumption, so an embedded server "
    "hosts even when the branch it was copied from declared a client"
) {
    Mounted branch("CS4Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->desired_role(NetwMultiplayer::ROLE_CLIENT);
    branch.api()->session_constrain_role(
        NetwMultiplayer::ROLE_DEDICATED_SERVER
    );

    branch.api()->config_settle();

    NETW_CHECK_EQ(
        int(branch.api()->session_get_authored_role()),
        int(NetwMultiplayer::ROLE_DEDICATED_SERVER)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS5 a session whose declaration "
    "has not been consumed refuses a direct peer assignment and keeps the "
    "peer it already had, because a void setter cannot secretly become an "
    "asynchronous one"
) {
    Mounted branch("CS5Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app_id(StringName("still-authoring"));

    Ref<netw::LocalMultiplayerPeer> offered;
    offered.instantiate();
    NETW_CHECK_EQ(int(offered->create_server()), int(OK));

    const Ref<MultiplayerPeer> standing
        = branch.api()->NETW_API_VIRTUAL(get_multiplayer_peer)();

    branch.api()->NETW_API_VIRTUAL(set_multiplayer_peer)(offered);

    CHECK(branch.api()->NETW_API_VIRTUAL(get_multiplayer_peer)() == standing);

    branch.api()->config_settle();
    branch.api()->NETW_API_VIRTUAL(set_multiplayer_peer)(offered);

    CHECK(
        branch.api()->NETW_API_VIRTUAL(get_multiplayer_peer)()
        == Ref<MultiplayerPeer>(offered)
    );
    branch.api()->NETW_API_VIRTUAL(set_multiplayer_peer)(
        Ref<MultiplayerPeer>()
    );
}

} // namespace TestNetwConfigureSession
