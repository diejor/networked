#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/scene_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwSessionRootLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::NetwSceneCore;
using netw_test::CallLog;

TEST_CASE(
    "[Networked][Session][Hosted] SR1 the session root is read through its "
    "installed reader on every ask, so a re-mount is followed rather than "
    "cached behind a node that was pushed once"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *first = memnew(Node);
    Node *second = memnew(Node);
    const CallLog read;
    core->set_session_root(read.answering("root", first));

    CHECK(core->session_root() == first);
    CHECK(core->session_root() == first);
    NETW_CHECK_EQ(read.count("root"), 2);

    core->set_session_root(read.answering("root", second));

    CHECK(core->session_root() == second);
    NETW_CHECK_EQ(read.count("root"), 3);

    memdelete(second);
    memdelete(first);
}

TEST_CASE(
    "[Networked][Session][Hosted] SR2 a session with no root reader installed "
    "answers no root and asks nobody, because a session with no tree behind it "
    "is the ordinary case rather than a defect"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    CHECK(core->session_root() == nullptr);

    const CallLog read;
    core->set_session_root(read.answering("root", Variant()));

    CHECK(core->session_root() == nullptr);
    NETW_CHECK_EQ(read.count("root"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] SR3 a reader that answers something that is "
    "not a node answers no root, so nothing is ever handed a parent it cannot "
    "add a child to"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<RefCounted> stranger;
    stranger.instantiate();
    const CallLog read;
    core->set_session_root(read.answering("root", stranger));

    CHECK(core->session_root() == nullptr);
    NETW_CHECK_EQ(read.count("root"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] SR4 the session root is the fallback a "
    "spawn anchor takes, so a declaration that named an anchor still wins and "
    "one that named none lands under the session's own root"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    Node *anchor = memnew(Node);
    const CallLog read;
    core->set_session_root(read.answering("root", root));
    const Ref<NetwSceneCore> scenes = core->get_scene_core();

    CHECK(scenes->spawn_anchor(core->session_root()) == root);

    scenes->set_scene_anchor(anchor);

    CHECK(scenes->spawn_anchor(core->session_root()) == anchor);

    memdelete(anchor);
    memdelete(root);
}

} // namespace TestNetwSessionRootLaws
