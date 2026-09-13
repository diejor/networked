#include "support/netw_test.h"

#include "godot/json.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/connect/room_board.hpp"

namespace netw::connect {

struct RoomBoardProbe {
    static void arm(RoomBoard &p_board) {
        p_board.board_hash = board_hash_of(p_board.filter_uid);
        p_board.board_peer_id = "aaaaaaaaaaaaaaaaaaaa";
    }

    static void feed(RoomBoard &p_board, const godot::Dictionary &p_packet) {
        p_board.read_packet(p_packet);
    }

    static void collecting(RoomBoard &p_board, bool p_on) {
        p_board.collecting = p_on;
    }

    static void ready(RoomBoard &p_board) {
        p_board.collecting = false;
        p_board.listing_ready = true;
    }

    static godot::String hash_of(const RoomBoard &p_board) {
        return p_board.board_hash;
    }
};

} // namespace netw::connect

namespace TestNetwConnectRoomBoard {

using namespace godot;
using netw::connect::board_hash_of;
using netw::connect::RoomBoard;
using netw::connect::RoomBoardProbe;
using netw::connect::RoomCard;
using netw::connect::TargetRow;

RoomCard card_of(const char *p_hash, const char *p_uid, int64_t p_players) {
    RoomCard card;
    card.room_hash = String(p_hash);
    card.room_name = "Someone's game";
    card.filter_uid = String(p_uid);
    card.app_id = StringName("build-7");
    card.signaling_namespace = "my_game";
    card.players = p_players;
    card.max_players = 8;
    return card;
}

Dictionary packet_of(
    const String &p_board,
    const String &p_sender,
    const Dictionary &p_card
) {
    Dictionary slot;
    slot["type"] = "offer";
    slot["sdp"] = JSON::stringify(p_card, "", true, false);

    Dictionary packet;
    packet["info_hash"] = p_board;
    packet["peer_id"] = p_sender;
    packet["offer"] = slot;
    return packet;
}

TEST_CASE(
    "[Networked][Connect][Hosted] every host and browser of one game meet on "
    "a board hash derived from its filter uid, because a WebTorrent tracker "
    "pairs only peers announcing the same hash and has no list-all call"
) {
    CHECK(board_hash_of("networked") == board_hash_of("networked"));
    CHECK(board_hash_of("game-a") != board_hash_of("game-b"));
    NETW_CHECK_EQ(int(board_hash_of("networked").length()), 20);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a room card survives the round trip "
    "through the announce field it tunnels in, because the tracker relays "
    "that field verbatim and everything a browser shows rides in it"
) {
    const RoomCard sent = card_of("room00000000000000ab", "networked", 3);
    RoomCard read;
    REQUIRE(RoomCard::from_dictionary(sent.to_dictionary(), read));

    CHECK(read.room_hash == sent.room_hash);
    CHECK(read.room_name == sent.room_name);
    CHECK(read.filter_uid == sent.filter_uid);
    CHECK(read.app_id == sent.app_id);
    CHECK(read.signaling_namespace == sent.signaling_namespace);
    NETW_CHECK_EQ(int(read.players), 3);
    NETW_CHECK_EQ(int(read.max_players), 8);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a card that is not a room card is refused, "
    "because a browser's own query rides the same field and would otherwise "
    "be collected as a room with no address"
) {
    Dictionary query;
    query["t"] = "query";
    RoomCard read;
    CHECK(!RoomCard::from_dictionary(query, read));

    Dictionary hashless;
    hashless["t"] = "room";
    hashless["name"] = "nowhere";
    CHECK(!RoomCard::from_dictionary(hashless, read));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a card becomes a target addressed by its "
    "room hash and carrying what it advertised, so the browse list "
    "classifies a foreign build before a join the handshake would refuse"
) {
    const TargetRow row
        = card_of("room00000000000000ab", "networked", 3).to_row();

    CHECK(row.address == String("room00000000000000ab"));
    REQUIRE(row.advertised.is_valid());
    CHECK(row.advertised->get_app_id() == StringName("build-7"));
    NETW_CHECK_EQ(int(row.advertised->get_players()), 3);
    CHECK(String(row.metadata["signaling_namespace"]) == String("my_game"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a room announced for another game is not "
    "collected, because one board hash is shared by every build of every "
    "game that kept the stock filter uid"
) {
    RoomBoard board;
    board.filter_uid = "networked";
    RoomBoardProbe::arm(board);
    RoomBoardProbe::collecting(board, true);

    const String hash = RoomBoardProbe::hash_of(board);
    RoomBoardProbe::feed(
        board,
        packet_of(
            hash,
            "bbbbbbbbbbbbbbbbbbbb",
            card_of("room00000000000000ab", "networked", 1).to_dictionary()
        )
    );
    RoomBoardProbe::feed(
        board,
        packet_of(
            hash,
            "cccccccccccccccccccc",
            card_of("room00000000000000cd", "someone-else", 1).to_dictionary()
        )
    );

    RoomBoardProbe::ready(board);
    LocalVector<TargetRow> listed;
    REQUIRE(board.take_listing(listed));
    NETW_CHECK_EQ(int(listed.size()), 1);
    CHECK(listed[0].address == String("room00000000000000ab"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a room re-announcing replaces its own row "
    "rather than adding one, because a host announces on a loop and a browse "
    "window spans several of its announces"
) {
    RoomBoard board;
    board.filter_uid = "networked";
    RoomBoardProbe::arm(board);
    RoomBoardProbe::collecting(board, true);
    const String hash = RoomBoardProbe::hash_of(board);

    RoomBoardProbe::feed(
        board,
        packet_of(
            hash,
            "bbbbbbbbbbbbbbbbbbbb",
            card_of("room00000000000000ab", "networked", 1).to_dictionary()
        )
    );
    RoomBoardProbe::feed(
        board,
        packet_of(
            hash,
            "bbbbbbbbbbbbbbbbbbbb",
            card_of("room00000000000000ab", "networked", 4).to_dictionary()
        )
    );

    RoomBoardProbe::ready(board);
    LocalVector<TargetRow> listed;
    REQUIRE(board.take_listing(listed));
    NETW_CHECK_EQ(int(listed.size()), 1);
    NETW_CHECK_EQ(int(listed[0].advertised->get_players()), 4);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a packet for another board, or the board's "
    "own announce heard back, is dropped, because one tracker socket carries "
    "every swarm the process announces on"
) {
    RoomBoard board;
    board.filter_uid = "networked";
    RoomBoardProbe::arm(board);
    RoomBoardProbe::collecting(board, true);
    const String hash = RoomBoardProbe::hash_of(board);

    RoomBoardProbe::feed(
        board,
        packet_of(
            "ffffffffffffffffffff",
            "bbbbbbbbbbbbbbbbbbbb",
            card_of("room00000000000000ab", "networked", 1).to_dictionary()
        )
    );
    RoomBoardProbe::feed(
        board,
        packet_of(
            hash,
            "aaaaaaaaaaaaaaaaaaaa",
            card_of("room00000000000000cd", "networked", 1).to_dictionary()
        )
    );

    RoomBoardProbe::ready(board);
    LocalVector<TargetRow> listed;
    REQUIRE(board.take_listing(listed));
    NETW_CHECK_EQ(int(listed.size()), 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a listing is taken once, because a browse "
    "window closing is an edge and a reader that polls every frame must not "
    "see the same rooms answered on each of them"
) {
    RoomBoard board;
    RoomBoardProbe::arm(board);
    LocalVector<TargetRow> nothing;
    CHECK(!board.take_listing(nothing));

    RoomBoardProbe::ready(board);
    LocalVector<TargetRow> once;
    CHECK(board.take_listing(once));
    LocalVector<TargetRow> again;
    CHECK(!board.take_listing(again));
}

} // namespace TestNetwConnectRoomBoard
