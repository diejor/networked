#include "support/netw_test.h"

#include "netw/interest/relay.hpp"

namespace TestAwarenessFrameWire {

using namespace godot;
using netw::interest::Awareness;
using netw::interest::AwarenessEdge;

TEST_CASE(
    "[Networked][Interest][Hosted] AW1 the edge transcribed from WIRE.md 9.10 "
    "packs to the bytes tools/wire_decode.py holds it to"
) {
    netw::wire::WriteStream stream;
    AwarenessEdge edge;
    edge.observer_scoped = true;
    edge.route = 7;
    edge.layer_id = StringName("zone");
    edge.observer = 3;
    edge.entered = true;
    REQUIRE(AwarenessEdge::wire.run(stream, edge));
    REQUIRE(stream.align_verify());
    const PackedByteArray bytes = stream.to_bytes();

    REQUIRE(bytes.size() == 10);
    NETW_CHECK_EQ(bytes[0], 0x01);
    NETW_CHECK_EQ(bytes[1], 0x07);
    NETW_CHECK_EQ(bytes[2], 0x04);
    NETW_CHECK_EQ(bytes[4], uint8_t('z'));
    NETW_CHECK_EQ(bytes[8], 0x03);
    NETW_CHECK_EQ(bytes[9], 0x01);
}

TEST_CASE(
    "[Networked][Interest][Hosted] AW2 a batch round trips every edge it was "
    "given, layer scoped and observer scoped together"
) {
    Array rows;
    rows.push_back(
        Awareness::layer_edge(4, StringName("zone"), Awareness::ENTER)
            .to_array()
    );
    rows.push_back(
        Awareness::observer_edge(9, StringName("zone"), 3, Awareness::EXIT)
            .to_array()
    );

    LocalVector<Awareness> read;
    const bool admitted = netw::interest::awareness_decode(
        netw::interest::awareness_encode(rows),
        read
    );
    REQUIRE(admitted);
    REQUIRE(read.size() == 2);
    NETW_CHECK_EQ(read[0].type, int32_t(Awareness::LAYER));
    NETW_CHECK_EQ(read[0].route, int64_t(4));
    NETW_CHECK_EQ(read[0].observer_peer, int64_t(0));
    NETW_CHECK_EQ(read[1].type, int32_t(Awareness::OBSERVER));
    NETW_CHECK_EQ(read[1].observer_peer, int64_t(3));
    NETW_CHECK_EQ(read[1].kind, int32_t(Awareness::EXIT));
}

TEST_CASE(
    "[Networked][Interest][Hosted] AW3 a batch whose last edge is cut short "
    "yields none of its edges, because the sweep that authored them authored "
    "them together and one that fails to decode discredits the rest"
) {
    Array rows;
    rows.push_back(
        Awareness::layer_edge(4, StringName("zone"), Awareness::ENTER)
            .to_array()
    );
    rows.push_back(
        Awareness::layer_edge(5, StringName("zone"), Awareness::ENTER)
            .to_array()
    );
    const PackedByteArray whole = netw::interest::awareness_encode(rows);
    REQUIRE(whole.size() > 2);

    LocalVector<Awareness> read;
    const bool refused = !netw::interest::awareness_decode(
        whole.slice(0, whole.size() - 2),
        read
    );
    CHECK(refused);
    NETW_CHECK_EQ(int64_t(read.size()), int64_t(0));
}

TEST_CASE(
    "[Networked][Interest][Hosted] AW4 a layer edge naming an observer and an "
    "observer edge naming peer zero are both refused, because an edge that "
    "does not say which scope it is in reaches the wrong sweep"
) {
    AwarenessEdge edge;
    edge.observer_scoped = false;
    edge.route = 4;
    edge.layer_id = StringName("zone");
    edge.observer = 3;
    edge.entered = true;

    netw::wire::WriteStream framed;
    uint64_t count = 1;
    REQUIRE(framed.varuint(count, 2));
    REQUIRE(AwarenessEdge::wire.run(framed, edge));
    REQUIRE(framed.align_verify());

    LocalVector<Awareness> read;
    const bool refused
        = !netw::interest::awareness_decode(framed.to_bytes(), read);
    CHECK(refused);
}

} // namespace TestAwarenessFrameWire
