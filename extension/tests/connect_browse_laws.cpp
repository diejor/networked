#include "support/netw_test.h"

#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/browse_list.hpp"
#include "netw/connect/transport.hpp"

namespace TestNetwConnectBrowse {

using namespace godot;
using netw::NetwServerInfo;
using netw::connect::BrowseList;
using netw::connect::TargetRow;

struct TransportScope {
    RID slot;

    explicit TransportScope(const char *p_peer_class)
        : slot(netw::connect::mint_built_in_slot(StringName(p_peer_class))) {
    }
    ~TransportScope() {
        netw::connect::free_transport_slot(slot);
    }
};

Ref<NetwServerInfo> info_for(const char *p_app_id) {
    Ref<NetwServerInfo> info;
    info.instantiate();
    info->set_app_id(StringName(p_app_id));
    return info;
}

PackedStringArray strings_of(
    const char *p_first,
    const char *p_second = nullptr
) {
    PackedStringArray out;
    out.push_back(String(p_first));
    if (p_second != nullptr) {
        out.push_back(String(p_second));
    }
    return out;
}

struct Publication {
    LocalVector<RID> dropped;
    LocalVector<RID> added;
    LocalVector<RID> updated;
};

Publication publish(
    BrowseList &p_list,
    const RID &p_transport,
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names = PackedStringArray(),
    const Array &p_infos = Array()
) {
    Publication out;
    p_list.publish_directory(
        StringName("steam"),
        p_transport,
        p_addresses,
        p_names,
        p_infos,
        out.dropped,
        out.added,
        out.updated
    );
    return out;
}

TEST_CASE(
    "[Networked][Connect][Hosted] the list answers caller rows before "
    "directory rows, because a row a person chose to keep outranks one a "
    "provider happened to advertise this second"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    list.add(enet.slot, String("first.example:21253"), String("First"));
    list.add(enet.slot, String("second.example:21253"), String("Second"));

    publish(list, enet.slot, strings_of("lobby-a", "lobby-b"));

    const Array shown = list.list();

    NETW_CHECK_EQ(int(shown.size()), 4);
    CHECK(list.row(RID(shown[0]))->address == String("first.example:21253"));
    CHECK(list.row(RID(shown[1]))->address == String("second.example:21253"));
    CHECK(list.row(RID(shown[2]))->address == String("lobby-a"));
    CHECK(list.row(RID(shown[3]))->address == String("lobby-b"));
    NETW_CHECK_EQ(int(list.caller_targets().size()), 2);
    CHECK(list.is_caller_row(RID(shown[0])));
    CHECK_FALSE(list.is_caller_row(RID(shown[2])));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a republished listing lands on the same "
    "handles rather than minting new ones, so a browser drawing a row every "
    "second redraws it instead of rebuilding the whole list"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    const Publication first
        = publish(list, enet.slot, strings_of("lobby-a", "lobby-b"));

    NETW_CHECK_EQ(int(first.added.size()), 2);
    NETW_CHECK_EQ(int(first.dropped.size()), 0);
    const RID handle_a = first.added[0];
    const RID handle_b = first.added[1];
    CHECK(handle_a != handle_b);

    const Publication again = publish(
        list,
        enet.slot,
        strings_of("lobby-b", "lobby-a"),
        strings_of("Lobby B renamed", "Lobby A")
    );

    NETW_CHECK_EQ(int(again.added.size()), 0);
    NETW_CHECK_EQ(int(again.updated.size()), 2);
    CHECK(again.updated[0] == handle_b);
    CHECK(again.updated[1] == handle_a);
    CHECK(list.row(handle_b)->display_name == String("Lobby B renamed"));

    SUBCASE("and a lobby that closed frees exactly its own handle") {
        const Publication fewer
            = publish(list, enet.slot, strings_of("lobby-b"));

        NETW_CHECK_EQ(int(fewer.dropped.size()), 1);
        CHECK(fewer.dropped[0] == handle_a);
        CHECK(list.row(handle_a) == nullptr);
        CHECK(list.row(handle_b) != nullptr);
        NETW_CHECK_EQ(int(list.list().size()), 1);
    }

    SUBCASE(
        "and a lobby that returns takes a NEW handle, because the one "
        "it held was freed and a stale handle must never resurrect"
    ) {
        publish(list, enet.slot, strings_of("lobby-b"));
        const Publication back
            = publish(list, enet.slot, strings_of("lobby-b", "lobby-a"));

        NETW_CHECK_EQ(int(back.added.size()), 1);
        CHECK(back.added[0] != handle_a);
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a directory that goes away takes only its "
    "own rows, because a caller's row belongs to the caller and no provider "
    "may drop it"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    publish(list, enet.slot, strings_of("lobby-a"));
    const RID kept
        = list.add(enet.slot, String("kept.example:21253"), String("Kept"));

    LocalVector<RID> forgotten;
    list.forget_directory(StringName("steam"), forgotten);

    NETW_CHECK_EQ(int(forgotten.size()), 1);
    const Array left = list.list();
    NETW_CHECK_EQ(int(left.size()), 1);
    CHECK(RID(left[0]) == kept);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row is found by the pair it is, so a "
    "caller that already knows its transport and address reaches the same "
    "handle without holding one"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    const TransportScope other("WebSocketMultiplayerPeer");
    const RID made
        = list.add(enet.slot, String("first.example:21253"), String("First"));

    CHECK(list.find(enet.slot, String("first.example:21253")) == made);
    CHECK_FALSE(
        list.find(other.slot, String("first.example:21253")).is_valid()
    );
    CHECK_FALSE(list.find(enet.slot, String("nothing.example")).is_valid());

    SUBCASE(
        "and adding the same pair twice answers the standing handle "
        "rather than a second row for one endpoint"
    ) {
        CHECK(
            list.add(enet.slot, String("first.example:21253"), String("Again"))
            == made
        );
        NETW_CHECK_EQ(int(list.list().size()), 1);
    }

    SUBCASE("and a removed handle is refused by every reader") {
        CHECK(list.remove(made));
        CHECK(list.row(made) == nullptr);
        CHECK_FALSE(list.remove(made));
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row advertising a foreign app id is "
    "refused before the join, because the handshake would refuse it anyway "
    "and a person reading the list deserves the reason first"
) {
    BrowseList list;
    list.set_local_app_id(StringName("arena-v3"));

    NETW_CHECK_EQ(list.classify(info_for("arena-v3"), OK), int64_t(OK));
    NETW_CHECK_EQ(
        list.classify(info_for("arena-v2"), OK),
        int64_t(ERR_UNAUTHORIZED)
    );

    SUBCASE(
        "and an error already reported is passed through, not reclassified"
    ) {
        NETW_CHECK_EQ(
            list.classify(info_for("arena-v2"), ERR_TIMEOUT),
            int64_t(ERR_TIMEOUT)
        );
    }

    SUBCASE("and a list with no app id of its own gates nothing") {
        BrowseList open;
        NETW_CHECK_EQ(open.classify(info_for(""), OK), int64_t(OK));
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a directory row arrives already judged, "
    "because it carries what the host advertised and needs no round trip to "
    "say whether this build could join it"
) {
    BrowseList list;
    list.set_local_app_id(StringName("arena-v3"));
    const TransportScope enet("ENetMultiplayerPeer");

    Array carried;
    carried.push_back(info_for("arena-v2"));
    const Publication published = publish(
        list,
        enet.slot,
        strings_of("lobby-a"),
        PackedStringArray(),
        carried
    );

    REQUIRE(published.added.size() == 1);
    const TargetRow *judged = list.row(published.added[0]);
    REQUIRE(judged != nullptr);
    CHECK(judged->observed);
    NETW_CHECK_EQ(judged->status, int64_t(ERR_UNAUTHORIZED));
}

TEST_CASE(
    "[Networked][Connect][Hosted] the probe queue never runs more than six "
    "at once, so a list of forty rows costs six sockets rather than forty"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    for (int at = 0; at < 40; at++) {
        list.add(enet.slot, vformat("host-%d", at), String());
    }
    list.enqueue_caller_rows();

    RID taken;
    int started = 0;
    while (list.next_to_probe(taken)) {
        started++;
    }

    NETW_CHECK_EQ(started, BrowseList::MAX_IN_FLIGHT_PROBES);
    NETW_CHECK_EQ(list.in_flight(), BrowseList::MAX_IN_FLIGHT_PROBES);

    SUBCASE("and one settling lets exactly one more start") {
        list.probe_settled(taken, OK, info_for(""));

        NETW_CHECK_EQ(list.in_flight(), BrowseList::MAX_IN_FLIGHT_PROBES - 1);
        RID next;
        CHECK(list.next_to_probe(next));
        NETW_CHECK_EQ(list.in_flight(), BrowseList::MAX_IN_FLIGHT_PROBES);
    }
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row observed carries its verdict and an "
    "unobserved one says so, which is what tells a list a blank cell means "
    "not yet rather than unreachable"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    const RID made
        = list.add(enet.slot, String("first.example:21253"), String());

    CHECK_FALSE(list.row(made)->observed);

    list.enqueue(made);
    RID taken;
    REQUIRE(list.next_to_probe(taken));
    list.probe_settled(taken, ERR_TIMEOUT, Ref<NetwServerInfo>());

    const TargetRow *settled = list.row(made);
    REQUIRE(settled != nullptr);
    CHECK(settled->observed);
    NETW_CHECK_EQ(settled->status, int64_t(ERR_TIMEOUT));
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row removed while its probe is in "
    "flight reports nothing, because the waiter holds the handle rather "
    "than a copy of the row and the handle is gone"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    const RID made
        = list.add(enet.slot, String("first.example:21253"), String());

    list.enqueue(made);
    RID taken;
    REQUIRE(list.next_to_probe(taken));
    CHECK(list.remove(made));

    list.probe_settled(taken, OK, info_for(""));

    NETW_CHECK_EQ(list.in_flight(), 0);
    CHECK(list.row(made) == nullptr);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row queued and then removed is skipped "
    "rather than probed, so a browser closing a row cannot open a socket "
    "for it a frame later"
) {
    BrowseList list;
    const TransportScope enet("ENetMultiplayerPeer");
    const RID first
        = list.add(enet.slot, String("first.example:21253"), String());
    const RID second
        = list.add(enet.slot, String("second.example:21253"), String());

    list.enqueue_caller_rows();
    CHECK(list.remove(first));

    RID taken;
    REQUIRE(list.next_to_probe(taken));
    CHECK(taken == second);
    CHECK_FALSE(list.next_to_probe(taken));
}

} // namespace TestNetwConnectBrowse
