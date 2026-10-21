#include "support/fault_log.h"
#include "support/netw_test.h"

#include "netw/api/join_request.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwSessionUsername {

using namespace godot;
using netw::NetwMultiplayer;
using netw::session::AcceptFrame;

TEST_CASE(
    "[Networked][Session][Hosted] U1 two joins claiming one username are both "
    "admitted under it, each on its own membership, and the session records "
    "no refusal either of them would have to retry past"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();

    netw::JoinRequest first;
    first.username = StringName("ana");
    AcceptFrame seated;
    CHECK(session->session_resolve_inbound_join(first, 4, seated));
    session->session_admit(seated);

    netw::JoinRequest second;
    second.username = StringName("ana");
    AcceptFrame rival;
    CHECK(session->session_resolve_inbound_join(second, 5, rival));
    CHECK(bool(rival.username == StringName("ana")));
    CHECK(rival.player_id != seated.player_id);
    CHECK(session->session_refusal(5).is_empty());
    session->session_admit(rival);

    const Ref<netw::NetwPlayer> one = session->player_of(4);
    const Ref<netw::NetwPlayer> other = session->player_of(5);
    REQUIRE(one.is_valid());
    REQUIRE(other.is_valid());
    CHECK(one->get_is_active());
    CHECK(other->get_is_active());
    CHECK(bool(one->get_username() == StringName("ana")));
    CHECK(bool(other->get_username() == StringName("ana")));
}

TEST_CASE(
    "[Networked][Session][Hosted] U2 two joins carrying no username are both "
    "admitted, each with an empty username on its own membership"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();

    for (int64_t peer : {int64_t(4), int64_t(5)}) {
        netw::JoinRequest nameless;
        AcceptFrame seated;
        CHECK(session->session_resolve_inbound_join(nameless, peer, seated));
        session->session_admit(seated);
    }

    const Ref<netw::NetwPlayer> one = session->player_of(4);
    const Ref<netw::NetwPlayer> other = session->player_of(5);
    REQUIRE(one.is_valid());
    REQUIRE(other.is_valid());
    CHECK(one->player_id() != other->player_id());
    CHECK(String(one->get_username()).is_empty());
    CHECK(String(other->get_username()).is_empty());
}

} // namespace TestNetwSessionUsername
