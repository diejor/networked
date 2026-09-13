#include "support/netw_test.h"

#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestNetwWireRegistry {

using netw::wire::ChannelDecl;
using netw::wire::ChannelKind;
using netw::wire::Delivery;
using netw::wire::Direction;
using netw::wire::Freshness;
using netw::wire::PayloadContract;
using netw::wire::Reliability;
using netw::wire::WireRegistry;

TEST_CASE(
    "[Networked][Wire][Hosted] default registry populates built-in channels"
) {
    const WireRegistry reg = WireRegistry::create_default();
    NETW_CHECK_EQ(reg.active_count(), 40);

    const ChannelDecl *call = reg.find_channel(3);
    REQUIRE(call != nullptr);
    CHECK(call->valid());
    CHECK(call->name == godot::StringName("CALL"));
    CHECK(call->kind == ChannelKind::ROUTED);
    CHECK(call->reliability == Reliability::RELIABLE);

    const ChannelDecl *sync = reg.find_channel(19);
    REQUIRE(sync != nullptr);
    CHECK(sync->valid());
    CHECK(sync->name == godot::StringName("SYNC"));
    CHECK(sync->kind == ChannelKind::KEYED);
    CHECK(sync->reliability == Reliability::UNRELIABLE_ACKED);
    CHECK(sync->freshness == Freshness::FRESHEST_WINS);
    CHECK(sync->payload == PayloadContract::DELTA);

    // The two row lanes differ in exactly the property that decides when a
    // baseline may advance, so the declaration is where that difference is
    // held rather than in either lane's code.
    const ChannelDecl *row = reg.find_channel(39);
    REQUIRE(row != nullptr);
    CHECK(row->reliability == Reliability::UNRELIABLE_ACKED);

    const ChannelDecl *row_delta = reg.find_channel(40);
    REQUIRE(row_delta != nullptr);
    CHECK(row_delta->valid());
    CHECK(row_delta->name == godot::StringName("SYNC_ROW_DELTA"));
    CHECK(row_delta->kind == ChannelKind::ROUTED);
    CHECK(row_delta->reliability == Reliability::RELIABLE);
    CHECK(row_delta->freshness == Freshness::NONE);
    CHECK(row_delta->payload == PayloadContract::DELTA);

    // The windowed lane repeats rather than diffs, so it declares whole rows
    // and takes no acknowledgement of its own.
    const ChannelDecl *row_window = reg.find_channel(41);
    REQUIRE(row_window != nullptr);
    CHECK(row_window->valid());
    CHECK(row_window->name == godot::StringName("SYNC_ROW_WINDOW"));
    CHECK(row_window->kind == ChannelKind::KEYED);
    CHECK(row_window->reliability == Reliability::UNRELIABLE);
    CHECK(row_window->payload == PayloadContract::PLANNED);
}

TEST_CASE("[Networked][Wire][Hosted] reserved channel slots report invalid") {
    const WireRegistry reg = WireRegistry::create_default();

    const ChannelDecl *ch0 = reg.find_channel(0);
    REQUIRE(ch0 != nullptr);
    CHECK_FALSE(ch0->valid());

    WireRegistry own = WireRegistry::create_default();
    ChannelDecl live;
    live.id = 0;
    live.name = godot::StringName("RECLAIMED");
    CHECK_FALSE(own.register_channel(live));
    CHECK(own.find_channel(0)->name != godot::StringName("RECLAIMED"));

    const ChannelDecl *ch1 = reg.find_channel(1);
    REQUIRE(ch1 != nullptr);
    CHECK_FALSE(ch1->valid());

    const ChannelDecl *ch7 = reg.find_channel(7);
    REQUIRE(ch7 != nullptr);
    CHECK_FALSE(ch7->valid());
}

TEST_CASE("[Networked][Wire][Hosted] registry finds channel by StringName") {
    const WireRegistry reg = WireRegistry::create_default();

    const ChannelDecl *predict
        = reg.find_channel_by_name(godot::StringName("PREDICT_COMMAND"));
    REQUIRE(predict != nullptr);
    NETW_CHECK_EQ(predict->id, 35);
    CHECK(predict->kind == ChannelKind::ROUTED);
    CHECK(predict->direction == Direction::OWNER_TO_SERVER);

    CHECK(
        reg.find_channel_by_name(godot::StringName("NON_EXISTENT")) == nullptr
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] identity hash is deterministic and sensitive"
) {
    const WireRegistry reg1 = WireRegistry::create_default();
    const WireRegistry reg2 = WireRegistry::create_default();

    const uint64_t hash1 = reg1.identity_hash();
    const uint64_t hash2 = reg2.identity_hash();
    CHECK(hash1 != 0);
    NETW_CHECK_EQ(hash1, hash2);

    WireRegistry reg3 = WireRegistry::create_default();
    ChannelDecl custom;
    custom.id = 100;
    custom.name = godot::StringName("CHAT");
    custom.kind = ChannelKind::SESSION;
    custom.reliability = Reliability::RELIABLE;
    reg3.register_channel(custom);

    NETW_CHECK_EQ(reg3.active_count(), 41);
    CHECK(reg3.identity_hash() != hash1);
}

TEST_CASE(
    "[Networked][Wire][Hosted] B1 a per-tick lane aggregates without being "
    "asked"
) {
    const WireRegistry reg = WireRegistry::create_default();

    // SYNC, SYNC_ROW, SYNC_ROW_DELTA, SYNC_ROW_WINDOW, SYNC_DELTA, TABLE,
    // SIGNAL and PROPERTY_SYNC are the lanes the tick pump flushes, and none
    // of their senders passes a batch flag.
    for (const uint8_t id : {5, 6, 17, 19, 20, 39, 40, 41}) {
        CHECK(reg.aggregates(id, false));
        CHECK(reg.aggregates(id, true));
    }
}

TEST_CASE("[Networked][Wire][Hosted] B2 an immediate channel refuses the ask") {
    const WireRegistry reg = WireRegistry::create_default();

    // CLOCK_PING and CLOCK_PONG time a round trip. Holding one for a flush
    // would fold the aggregation delay into the measurement, so the ask is
    // refused rather than honoured.
    CHECK_FALSE(reg.aggregates(11, true));
    CHECK_FALSE(reg.aggregates(12, true));

    // A channel that neither refuses nor aggregates by itself leaves the
    // decision with its sender.
    CHECK_FALSE(reg.aggregates(3, false));
    CHECK(reg.aggregates(3, true));
}

TEST_CASE(
    "[Networked][Wire][Hosted] B3 an undeclared channel is answered by the ask "
    "alone"
) {
    const WireRegistry reg = WireRegistry::create_default();

    // A custom channel runs 100..254 and has no declaration to consult, so
    // refusing here would silently drop its batch flag on the floor.
    CHECK(reg.aggregates(120, true));
    CHECK_FALSE(reg.aggregates(120, false));

    // A reserved id carries a slot rather than a contract, and is read the
    // same way for the same reason.
    CHECK(reg.aggregates(7, true));
    CHECK_FALSE(reg.aggregates(7, false));
}

} // namespace TestNetwWireRegistry
