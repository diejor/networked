#include "support/netw_test.h"

#include "netw/session/frames.hpp"
#include "netw/session_core.hpp"
#include "netw/wire/registry.hpp"

namespace TestControlFrameWire {

using namespace godot;
using netw::session::ControlApply;
using netw::session::ControlRequest;

TEST_CASE(
    "[Networked][Control][Hosted] KW1 the apply transcribed from WIRE.md 14 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    ControlApply applied;
    applied.controller = 2;
    applied.revision = 3;
    applied.tenure_changed = true;
    applied.hold = 2;
    const PackedByteArray bytes = netw::session::frame_write(applied);

    NETW_REQUIRE_EQ(int64_t(bytes.size()), int64_t(5));
    NETW_CHECK_EQ(bytes[0], 0x02);
    NETW_CHECK_EQ(bytes[1], 0x03);
    NETW_CHECK_EQ(bytes[2], 0x00);
    NETW_CHECK_EQ(bytes[3], 0x05);
    NETW_CHECK_EQ(bytes[4], 0x00);
}

TEST_CASE(
    "[Networked][Control][Hosted] KW3 the request transcribed from WIRE.md 14 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    ControlRequest request;
    request.op = 1;
    request.observed_revision = 3;
    request.issued_tick = 300;
    request.hold = 1;
    const PackedByteArray bytes = netw::session::frame_write(request);

    NETW_REQUIRE_EQ(int64_t(bytes.size()), int64_t(8));
    NETW_CHECK_EQ(bytes[0], 0x01);
    NETW_CHECK_EQ(bytes[1], 0x03);
    NETW_CHECK_EQ(bytes[2], 0xAC);
    NETW_CHECK_EQ(bytes[3], 0x02);
    NETW_CHECK_EQ(bytes[4], 0x00);
    NETW_CHECK_EQ(bytes[5], 0x00);
    NETW_CHECK_EQ(bytes[6], 0x02);
    NETW_CHECK_EQ(bytes[7], 0x00);
}

TEST_CASE(
    "[Networked][Control][Hosted] KW4 the control, spawn, reparent, row "
    "control and lifecycle decision channels declare the payload revision "
    "their layouts are on, so a build that still speaks the controller-only "
    "apply, an OPEN without a tenure, a move without an anchor revision, an "
    "anchor revision without its author, an ADOPT without its origin or a "
    "refusal without its denial answers a different wire identity and is "
    "refused at the join gate"
) {
    const netw::wire::WireRegistry table
        = netw::wire::WireRegistry::create_default();
    struct Revised {
        const char *name;
        int revision;
    };
    const Revised revised[] = {
        {"CONTROL_REQUEST", 1},
        {"CONTROL_APPLY", 1},
        {"SPAWN", 4},
        {"REPARENT", 2},
        {"ROW_CONTROL", 3},
        {"LIFECYCLE_DECISION", 2},
    };
    for (const Revised &row : revised) {
        const netw::wire::ChannelDecl *decl
            = table.find_channel_by_name(StringName(row.name));
        REQUIRE(decl != nullptr);
        NETW_CHECK_EQ(int(decl->payload_revision), row.revision);

        netw::wire::WireRegistry older
            = netw::wire::WireRegistry::create_default();
        netw::wire::ChannelDecl unrevised = *decl;
        unrevised.payload_revision = uint16_t(row.revision - 1);
        REQUIRE(older.register_channel(unrevised));
        const bool refused_at_join
            = older.identity_hash() != table.identity_hash();
        CHECK(refused_at_join);
    }
    NETW_CHECK_EQ(
        int64_t(table.identity_hash()),
        netw::SessionCore::compute_wire_identity()
    );
}

TEST_CASE(
    "[Networked][Control][Hosted] KW2 a control apply carrying no payload at "
    "all is refused rather than read as controller zero, because an empty "
    "frame and a frame declaring the route uncontrolled say different things"
) {
    ControlApply empty;
    empty.controller = 7;
    const bool refused = !netw::session::frame_read(PackedByteArray(), empty);
    CHECK(refused);

    ControlApply uncontrolled;
    uncontrolled.controller = 0;
    ControlApply read;
    read.controller = 7;
    const bool admitted = netw::session::frame_read(
        netw::session::frame_write(uncontrolled),
        read
    );
    CHECK(admitted);
    NETW_CHECK_EQ(int64_t(read.controller), int64_t(0));
}

} // namespace TestControlFrameWire
