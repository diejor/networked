#include "support/netw_recorder.h"
#include "support/netw_test.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwEmbedPhase {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> declaring_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

TEST_CASE(
    "[Networked][Session][Hosted] E1 a session starts DECLARING and settle "
    "advances it to LIVE, announcing every phase it passes through"
) {
    Ref<NetwMultiplayer> session = declaring_session();
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_DECLARING)
    );

    netw_test::Recorder heard(
        session.ptr(),
        {StringName("embed_phase_changed")}
    );
    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
    NETW_CHECK_EQ(heard.count(StringName("embed_phase_changed")), 2);
    NETW_CHECK_EQ(
        int(heard.args(StringName("embed_phase_changed"), 0)[0]),
        int(NetwMultiplayer::EMBED_PHASE_SETTLING)
    );
    NETW_CHECK_EQ(
        int(heard.args(StringName("embed_phase_changed"), 1)[0]),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] E2 settle past DECLARING resolves nothing "
    "again and announces nothing, so a second embedding cannot re-author"
) {
    Ref<NetwMultiplayer> session = declaring_session();
    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));

    netw_test::Recorder heard(
        session.ptr(),
        {StringName("embed_phase_changed")}
    );
    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
    NETW_CHECK_EQ(heard.count(StringName("embed_phase_changed")), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] E3 a bare level offered to a session whose "
    "root path names nothing is refused rather than adopted, and settle still "
    "reaches LIVE"
) {
    Ref<NetwMultiplayer> session = declaring_session();
    session->session_get_inner()->set_root_path(
        NodePath("/root/NothingIsMountedHere")
    );
    Node *level = memnew(Node);
    session->embed_offer_bare_level(level);
    CHECK(session->session_root() == nullptr);

    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
    CHECK(netw::gd::instance_from_id(netw::gd::instance_id(level)) != nullptr);
    memdelete(level);
}

TEST_CASE(
    "[Networked][Session][Hosted] E4 a level freed between the offer and the "
    "settle is resolved as absent rather than reached through, so an "
    "embedding that tears its own candidate down cannot fault the settle"
) {
    Ref<NetwMultiplayer> session = declaring_session();
    Node *level = memnew(Node);
    const ObjectID offered = netw::gd::instance_id(level);
    session->embed_offer_bare_level(level);
    memdelete(level);

    NETW_CHECK_EQ(int(session->embed_settle()), int(OK));
    NETW_CHECK_EQ(
        int(session->embed_phase()),
        int(NetwMultiplayer::EMBED_PHASE_LIVE)
    );
    CHECK(netw::gd::instance_from_id(offered) == nullptr);
}

} // namespace TestNetwEmbedPhase
