#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "netw/api/context.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
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

    const PackedByteArray accept = host->session_accept_bytes(joined);
    REQUIRE(accept.size() > 0);
    CHECK(bytes_carry(accept, "ana"));
    CHECK_FALSE(bytes_carry(accept, SECRET));

    const PackedByteArray roster = host->session_roster_bytes();
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

    watcher->session_receive_roster(rig.server()->session_roster_bytes(), 1);
    CHECK(bool(watcher->peer_get_player(first) == seen));

    watcher->session_receive_accept(
        rig.server()->session_accept_bytes(first),
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

} // namespace TestNetwJoinPrivacyLaws

#endif
