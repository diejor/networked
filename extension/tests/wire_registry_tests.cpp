#include "support/netw_test.h"

#include "netw/wire/registry.hpp"

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
    NETW_CHECK_EQ(reg.active_count(), 35);

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
}

TEST_CASE("[Networked][Wire][Hosted] reserved channel slots report invalid") {
    const WireRegistry reg = WireRegistry::create_default();

    const ChannelDecl *ch0 = reg.find_channel(0);
    CHECK(ch0 == nullptr);

    const ChannelDecl *ch1 = reg.find_channel(1);
    REQUIRE(ch1 != nullptr);
    CHECK_FALSE(ch1->valid());

    const ChannelDecl *ch7 = reg.find_channel(7);
    REQUIRE(ch7 != nullptr);
    CHECK_FALSE(ch7->valid());
}

TEST_CASE("[Networked][Wire][Hosted] registry finds channel by StringName") {
    const WireRegistry reg = WireRegistry::create_default();

    const ChannelDecl *predict =
        reg.find_channel_by_name(godot::StringName("PREDICT_COMMAND"));
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

    NETW_CHECK_EQ(reg3.active_count(), 36);
    CHECK(reg3.identity_hash() != hash1);
}

} // namespace TestNetwWireRegistry
