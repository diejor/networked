#include "support/netw_test.h"

#include <cstdint>

#include "netw/replication_send.hpp"
#include "netw/wire/registry.hpp"

namespace TestNetwWireIdentityCoverage {

using netw::wire::ChannelDecl;
using netw::wire::ChannelKind;
using netw::wire::Delivery;
using netw::wire::Direction;
using netw::wire::Freshness;
using netw::wire::PayloadContract;
using netw::wire::Reliability;
using netw::wire::WireRegistry;

const uint8_t PROBE = 41;

ChannelDecl probe() {
    ChannelDecl decl;
    decl.id = PROBE;
    decl.name = godot::StringName("identity_probe");
    decl.kind = ChannelKind::ROUTED;
    decl.reliability = Reliability::UNRELIABLE;
    decl.freshness = Freshness::FRESHEST_WINS;
    decl.delivery = Delivery::FITTED;
    decl.direction = Direction::SERVER_TO_CLIENT;
    decl.payload = PayloadContract::PLANNED;
    decl.cap_bytes = 64;
    decl.priority = 1.0f;
    return decl;
}

uint64_t identity_of(const ChannelDecl &decl) {
    WireRegistry registry;
    registry.register_channel(decl);
    return registry.identity_hash();
}

TEST_CASE(
    "[Networked][Wire][Hosted] every field that decides the bytes is inside "
    "protocol identity"
) {
    const uint64_t base = identity_of(probe());

    ChannelDecl id = probe();
    id.id = PROBE + 1;
    CHECK(identity_of(id) != base);

    ChannelDecl name = probe();
    name.name = godot::StringName("identity_probe_2");
    CHECK(identity_of(name) != base);

    ChannelDecl kind = probe();
    kind.kind = ChannelKind::SESSION;
    CHECK(identity_of(kind) != base);

    ChannelDecl reliability = probe();
    reliability.reliability = Reliability::RELIABLE;
    CHECK(identity_of(reliability) != base);

    ChannelDecl freshness = probe();
    freshness.freshness = Freshness::NONE;
    CHECK(identity_of(freshness) != base);

    ChannelDecl delivery = probe();
    delivery.delivery = Delivery::IMMEDIATE;
    CHECK(identity_of(delivery) != base);

    ChannelDecl direction = probe();
    direction.direction = Direction::CLIENT_TO_SERVER;
    CHECK(identity_of(direction) != base);

    ChannelDecl payload = probe();
    payload.payload = PayloadContract::RAW;
    CHECK(identity_of(payload) != base);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a field that is only local scheduling stays "
    "outside protocol identity"
) {
    const uint64_t base = identity_of(probe());

    ChannelDecl priority = probe();
    priority.priority = 9.0f;
    NETW_CHECK_EQ(identity_of(priority), base);

    ChannelDecl cap = probe();
    cap.cap_bytes = 4096;
    NETW_CHECK_EQ(identity_of(cap), base);
}

TEST_CASE(
    "[Networked][Wire][Hosted] the format version and a payload revision are "
    "inside protocol identity, and an unrevised table hashes as it always did"
) {
    ChannelDecl revised = probe();
    revised.payload_revision = 1;
    CHECK(identity_of(revised) != identity_of(probe()));

    ChannelDecl unrevised = probe();
    unrevised.payload_revision = 0;
    NETW_CHECK_EQ(identity_of(unrevised), identity_of(probe()));

    CHECK(bool(netw::wire::FORMAT_VERSION == 10));
}

TEST_CASE(
    "[Networked][Wire][Hosted] a built-in id is declared once, by the built-in "
    "table, and a caller cannot redeclare it"
) {
    CHECK(WireRegistry::id_is_builtin(PROBE));
    CHECK(WireRegistry::id_is_builtin(0));
    CHECK_FALSE(WireRegistry::id_is_builtin(200));

    netw::ReplicationSend send;
    CHECK_FALSE(send.declare_channel(PROBE, godot::StringName("stolen"), true));
    CHECK(send.declare_channel(200, godot::StringName("mine"), true));
}

TEST_CASE("[Networked][Wire][Hosted] one declaration hashes the same twice") {
    WireRegistry first;
    WireRegistry second;
    ChannelDecl a = probe();
    ChannelDecl b = probe();
    b.id = PROBE + 5;
    b.name = godot::StringName("identity_probe_b");

    first.register_channel(a);
    first.register_channel(b);
    second.register_channel(b);
    second.register_channel(a);

    NETW_CHECK_EQ(first.identity_hash(), second.identity_hash());
}

} // namespace TestNetwWireIdentityCoverage
