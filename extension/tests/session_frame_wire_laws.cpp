#include "support/netw_test.h"

#include "netw/api/join_request.hpp"
#include "netw/session/frames.hpp"
#include "netw/session_core.hpp"
#include "netw/wire/registry.hpp"

namespace TestSessionFrameWire {

using namespace godot;
using netw::JoinFrame;
using netw::JoinRequest;
using netw::session::AcceptFrame;

AcceptFrame row_of(int64_t p_peer, const char *p_name) {
    AcceptFrame row;
    row.peer_id = p_peer;
    row.username = StringName(p_name);
    row.membership = uint64_t(p_peer);
    return row;
}

TEST_CASE(
    "[Networked][Session][Hosted] SW1 the join transcribed from WIRE.md 9.11 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    JoinFrame frame;
    frame.username = StringName("ana");
    frame.peer_id = 4;
    const PackedByteArray bytes = netw::session::frame_write(frame);

    REQUIRE(bytes.size() == 40);
    NETW_CHECK_EQ(bytes[0], 0x03);
    NETW_CHECK_EQ(bytes[2], uint8_t('a'));
    NETW_CHECK_EQ(bytes[15], 0x08);
}

TEST_CASE(
    "[Networked][Session][Hosted] SW2 the three identity halves survive the "
    "frame at their full width, because a hash read back narrowed pairs two "
    "builds that disagree about the wire they are about to speak"
) {
    JoinRequest sending;
    sending.username = StringName("ana");
    sending.peer_id = 4;
    sending.app_tag = int64_t(0x8877665544332211LL);
    sending.wire_identity = int64_t(0x1122334455667788LL);
    sending.schema_identity = -1;
    sending.schema_hash = int64_t(0x00FF00FF00FF00FFLL);

    JoinRequest read;
    const bool admitted = read.deserialize(sending.serialize());
    REQUIRE(admitted);
    NETW_CHECK_EQ(read.app_tag, sending.app_tag);
    NETW_CHECK_EQ(read.wire_identity, sending.wire_identity);
    NETW_CHECK_EQ(read.schema_identity, sending.schema_identity);
    NETW_CHECK_EQ(read.schema_hash, sending.schema_hash);
    NETW_CHECK_EQ(read.peer_id, int64_t(4));
}

TEST_CASE(
    "[Networked][Session][Hosted] SW3 a roster whose last row is cut short "
    "seats nobody, because a late joiner given part of a roster holds a world "
    "it believes complete"
) {
    LocalVector<AcceptFrame> rows;
    rows.push_back(row_of(4, "ana"));
    rows.push_back(row_of(7, "bo"));
    const PackedByteArray whole = netw::session::roster_write(rows);
    REQUIRE(whole.size() > 3);

    LocalVector<AcceptFrame> read;
    const bool refused
        = !netw::session::roster_read(whole.slice(0, whole.size() - 2), read);
    CHECK(refused);
    NETW_CHECK_EQ(int64_t(read.size()), int64_t(0));

    LocalVector<AcceptFrame> admitted;
    const bool whole_reads = netw::session::roster_read(whole, admitted);
    REQUIRE(whole_reads);
    NETW_CHECK_EQ(int64_t(admitted.size()), int64_t(2));
    NETW_CHECK_EQ(admitted[1].peer_id, int64_t(7));
}

TEST_CASE(
    "[Networked][Session][Hosted] SW4 the accept and roster channels declare "
    "the payload revision their frame layout is on, so a build whose accept "
    "frame differs answers a different wire identity and is refused at the "
    "join gate rather than decoding a roster it cannot read"
) {
    const netw::wire::WireRegistry table
        = netw::wire::WireRegistry::create_default();

    const netw::wire::ChannelDecl *accept
        = table.find_channel_by_name(StringName("SESSION_ACCEPT"));
    const netw::wire::ChannelDecl *roster
        = table.find_channel_by_name(StringName("SESSION_ROSTER"));
    REQUIRE(accept != nullptr);
    REQUIRE(roster != nullptr);
    NETW_CHECK_EQ(int(accept->payload_revision), 1);
    NETW_CHECK_EQ(int(roster->payload_revision), 1);

    netw::wire::WireRegistry older = netw::wire::WireRegistry::create_default();
    netw::wire::ChannelDecl unrevised_accept = *accept;
    netw::wire::ChannelDecl unrevised_roster = *roster;
    unrevised_accept.payload_revision = 0;
    unrevised_roster.payload_revision = 0;
    REQUIRE(older.register_channel(unrevised_accept));
    REQUIRE(older.register_channel(unrevised_roster));

    CHECK(older.identity_hash() != table.identity_hash());
    NETW_CHECK_EQ(
        int64_t(table.identity_hash()),
        netw::SessionCore::compute_wire_identity()
    );
}

} // namespace TestSessionFrameWire
