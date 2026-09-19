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
    "[Networked][Session][Hosted] U2 a join carrying no username at all is "
    "not resolved, so a forged payload seats nobody"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();

    netw::JoinRequest nameless;
    AcceptFrame seated;
    CHECK_FALSE(session->session_resolve_inbound_join(nameless, 4, seated));
    CHECK(session->player_of(4).is_null());
}

} // namespace TestNetwSessionUsername
