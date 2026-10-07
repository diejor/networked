#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_decl.hpp"

namespace TestNetwConfigureSession {

using namespace godot;
using netw::MultiplayerViewport;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;

namespace {

struct Mounted {
    MultiplayerViewport *tree = nullptr;

    explicit Mounted(const char *p_name) {
        tree = memnew(MultiplayerViewport);
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
    "[Networked][Session][Hosted][SceneTree] CS3 the fluent tail reaches the "
    "auth app tag and not only the stored app id, because both readers are "
    "handed the same owned value at consumption"
) {
    Mounted branch("CS3Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app(StringName("tagged-arena"));

    branch.api()->config_settle();

    NETW_CHECK_EQ(
        branch.api()->auth_app_tag_of(),
        NetwMultiplayer::session_app_tag(StringName("tagged-arena"))
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
        ->app(StringName("still-authoring"));

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
