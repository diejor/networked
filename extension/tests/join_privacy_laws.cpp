#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/context.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/server_info.hpp"
#include "netw/api/session_config.hpp"
#include "support/loopback_rig.h"
#include "support/minted_script.h"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwJoinPrivacyLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::LoopbackRig;

constexpr const char *SECRET = "loadout-kestrel-93";

bool bytes_carry(const PackedByteArray &p_bytes, const char *p_text) {
    const CharString needle = String(p_text).utf8();
    const int wanted = int(needle.length());
    if (wanted == 0 || int(p_bytes.size()) < wanted) {
        return false;
    }
    for (int at = 0; at + wanted <= int(p_bytes.size()); ++at) {
        int matched = 0;
        while (matched < wanted
               && p_bytes[at + matched] == uint8_t(needle[matched])) {
            matched += 1;
        }
        if (matched == wanted) {
            return true;
        }
    }
    return false;
}

Node *scripted_handler(Node *p_branch) {
    Node *holder = netw_test::minted_node(
        "extends Node\n"
        "var carried: Array = []\n"
        "func seat(_who, secret) -> void:\n"
        "\tcarried.append(secret)\n"
    );
    REQUIRE(holder != nullptr);
    p_branch->add_child(holder);
    return holder;
}

Array one_arg(const char *p_text) {
    Array out;
    out.push_back(String(p_text));
    return out;
}

TEST_CASE(
    "[Networked][Session] JV1 a join argument reaches the server's handler and "
    "no other peer, because the accept every peer receives and the roster a "
    "late joiner receives both carry the membership and not the values"
) {
    LoopbackRig rig(3);
    rig.mount();
    rig.pump(4);

    Node *holder = scripted_handler(rig.branch(-1));
    REQUIRE(
        netw::Netw::configure_join(Callable(holder, StringName("seat")))
            .is_valid()
    );
    Node *joiner = scripted_handler(rig.branch(0));
    REQUIRE(
        netw::Netw::configure_join(Callable(joiner, StringName("seat")))
            .is_valid()
    );

    const Ref<netw::NetwPlayer> seated
        = rig.join(0, StringName("ana"), one_arg(SECRET));
    REQUIRE(seated.is_valid());

    const Array carried = holder->get(StringName("carried"));
    REQUIRE(carried.size() == 1);
    CHECK(bool(String(carried[0]) == String(SECRET)));

    NetwMultiplayer *host = rig.server();
    const int joined = rig.peer_id(0);

    const PackedByteArray accept = host->session_accept_bytes(joined, joined);
    REQUIRE(accept.size() > 0);
    CHECK(bytes_carry(accept, "ana"));
    CHECK_FALSE(bytes_carry(accept, SECRET));

    const PackedByteArray roster = host->session_roster_bytes(joined);
    REQUIRE(roster.size() > 0);
    CHECK(bytes_carry(roster, "ana"));
    CHECK_FALSE(bytes_carry(roster, SECRET));
}

TEST_CASE(
    "[Networked][Session] JV2 one membership answers one participant object on "
    "every route that mints one, so a client that learned a peer from the "
    "accept and then from the roster holds a single object for it"
) {
    LoopbackRig rig(2);
    rig.pump(4);

    REQUIRE(rig.join(0, StringName("ana")).is_valid());
    REQUIRE(rig.join(1, StringName("bo")).is_valid());
    rig.pump(4);

    const int first = rig.peer_id(0);
    NetwMultiplayer *watcher = rig.client(1);

    const Ref<netw::NetwPlayer> seen = watcher->peer_get_player(first);
    REQUIRE(seen.is_valid());
    CHECK(bool(watcher->peer_get_player(first) == seen));

    const int watching = rig.peer_id(1);
    watcher->session_receive_roster(
        rig.server()->session_roster_bytes(watching),
        1
    );
    CHECK(bool(watcher->peer_get_player(first) == seen));

    watcher->session_receive_accept(
        rig.server()->session_accept_bytes(first, watching),
        1
    );
    CHECK(bool(watcher->peer_get_player(first) == seen));
}

TEST_CASE(
    "[Networked][Session] JV3 a membership is not the transport peer, so a "
    "reconnect that reuses a peer id reads as a new player and the handle "
    "the game still holds from the previous one reports itself inactive"
) {
    LoopbackRig rig(1);
    rig.pump(4);

    NetwMultiplayer *host = rig.server();
    const int reused = rig.peer_id(0);

    REQUIRE(rig.join(0, StringName("ana")).is_valid());
    const Ref<netw::NetwPlayer> before = host->peer_get_player(reused);
    REQUIRE(before.is_valid());
    const int64_t first_membership = host->player_incarnation(reused);
    CHECK(host->player_is_active(reused, first_membership));

    host->session_forget_peer(reused);
    rig.pump(4);

    netw::JoinRequest again;
    again.username = StringName("bo");
    again.app_tag = host->auth_app_tag_of();
    again.wire_identity = netw::SessionCore::compute_wire_identity();
    again.schema_identity = host->session_schema_identity();
    host->session_receive_join(again.serialize(), reused);
    rig.pump(4);

    const Ref<netw::NetwPlayer> after = host->peer_get_player(reused);
    REQUIRE(after.is_valid());
    CHECK(bool(after != before));

    const int64_t second_membership = host->player_incarnation(reused);
    CHECK(second_membership != first_membership);
    CHECK_FALSE(host->player_is_active(reused, first_membership));
    CHECK(host->player_is_active(reused, second_membership));
    CHECK(bool(String(after->get_username()) == String("bo")));
}

Node *account_gate(Node *p_branch) {
    Node *gate = netw_test::minted_node(
        "extends Node\n"
        "func admit(request: NetwJoinRequest) -> Error:\n"
        "\trequest.account = \"test:\" + request.username\n"
        "\trequest.account_name = String(request.username).to_upper()\n"
        "\treturn OK\n"
    );
    REQUIRE(gate != nullptr);
    p_branch->add_child(gate);
    REQUIRE(
        netw::Netw::configure_admission(Callable(gate, StringName("admit")))
            .is_valid()
    );
    return gate;
}

TEST_CASE(
    "[Networked][Session] JV4 an account reaches the server and its own "
    "player and no other peer, which reads only the account name, whether "
    "it learned the player from the accept or from the roster"
) {
    LoopbackRig rig(2);
    rig.mount();
    rig.pump(4);
    account_gate(rig.branch(-1));

    REQUIRE(rig.join(0, StringName("ana")).is_valid());
    REQUIRE(rig.join(1, StringName("bo")).is_valid());
    rig.pump(4);

    const int ana = rig.peer_id(0);
    const int bo = rig.peer_id(1);
    NetwMultiplayer *host = rig.server();
    NetwMultiplayer *first = rig.client(0);
    NetwMultiplayer *second = rig.client(1);

    CHECK(bool(host->peer_get_player(ana)->get_account() == "test:ana"));
    CHECK(bool(host->peer_get_player(bo)->get_account() == "test:bo"));
    CHECK(bool(first->peer_get_player(ana)->get_account() == "test:ana"));
    CHECK(bool(second->peer_get_player(bo)->get_account() == "test:bo"));

    const Ref<netw::NetwPlayer> accepted = first->peer_get_player(bo);
    REQUIRE(accepted.is_valid());
    CHECK(accepted->get_account().is_empty());
    CHECK(bool(accepted->get_account_name() == "BO"));
    const Ref<netw::NetwPlayer> rostered = second->peer_get_player(ana);
    REQUIRE(rostered.is_valid());
    CHECK(rostered->get_account().is_empty());
    CHECK(bool(rostered->get_account_name() == "ANA"));
}

TEST_CASE(
    "[Networked][Session] JV5 a client reads the join address and the player "
    "limit the server advertises once admitted, and follows the server when "
    "it advertises another address without joining a second time"
) {
    LoopbackRig rig(1);
    rig.mount();
    rig.pump(4);
    Ref<netw::NetwServerInfo> info;
    info.instantiate();
    info->set_max_players(8);
    netw::Netw::configure_session(rig.branch(-1))->server(info);
    rig.server()->config_settle();

    const Ref<netw::NetwConnection> served = rig.server()->get_connection();
    const Ref<netw::NetwConnection> first = rig.client(0)->get_connection();
    served->set_join_address(String("203.0.113.4:21253"));
    NETW_CHECK_EQ(served->get_max_players(), int64_t(8));

    REQUIRE(rig.join(0, StringName("ana")).is_valid());
    CHECK(bool(first->get_join_address() == "203.0.113.4:21253"));
    NETW_CHECK_EQ(first->get_max_players(), int64_t(8));

    const Ref<netw::NetwPlayer> seated = rig.client(0)->player_local();
    served->set_join_address(String("203.0.113.9:21253"));
    rig.pump(4);
    CHECK(bool(first->get_join_address() == "203.0.113.9:21253"));
    CHECK(bool(rig.client(0)->player_local() == seated));
}

} // namespace TestNetwJoinPrivacyLaws

#endif
