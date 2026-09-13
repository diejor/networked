#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionRootLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Session][Hosted] SR1 the session root is read through its "
    "installed reader on every ask, so a re-mount is followed rather than "
    "cached behind a node that was pushed once"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *first = memnew(Node);
    Node *second = memnew(Node);
    const CallLog read;
    core->session_set_root(read.answering("root", first));

    CHECK(core->session_root() == first);
    CHECK(core->session_root() == first);
    NETW_CHECK_EQ(read.count("root"), 2);

    core->session_set_root(read.answering("root", second));

    CHECK(core->session_root() == second);
    NETW_CHECK_EQ(read.count("root"), 3);

    memdelete(second);
    memdelete(first);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] SR2 a session with no root "
    "reader installed resolves its root from the transport's own root path, "
    "so a session nobody handed a reader still answers the branch it "
    "replicates against, and one whose path names nothing answers no root"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<SceneMultiplayer> transport = core->session_get_inner();
    REQUIRE(transport.is_valid());

    CHECK(core->session_root() == netw::gd::scene_root());

    transport->set_root_path(NodePath("/root/NothingIsMountedHere"));
    CHECK(core->session_root() == nullptr);

    const CallLog read;
    core->session_set_root(read.answering("root", Variant()));

    CHECK(core->session_root() == nullptr);
    NETW_CHECK_EQ(read.count("root"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] SR3 a reader that answers something that is "
    "not a node answers no root, so nothing is ever handed a parent it cannot "
    "add a child to"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<RefCounted> stranger;
    stranger.instantiate();
    const CallLog read;
    core->session_set_root(read.answering("root", stranger));

    CHECK(core->session_root() == nullptr);
    NETW_CHECK_EQ(read.count("root"), 1);
}

} // namespace TestNetwSessionRootLaws
