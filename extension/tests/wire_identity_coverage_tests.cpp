// Which fields of a channel declaration protocol identity covers, field by
// field, so a new one cannot be added silently on either side of the line.
//
// The identity hash is what decides whether two builds may talk. A field that
// changes the BYTES must be inside it, or two peers that disagree about that
// field consider themselves compatible and then desynchronize on the first
// frame that uses it. A field that is local scheduling must stay outside it, or
// a peer that merely tunes its own send order is locked out of a session it
// could have joined.
//
// So neither direction is the safe default and the line has to be stated. This
// suite states it: every field is asserted IN or OUT by name, and adding a
// field to `ChannelDecl` without deciding which it is leaves this suite
// describing a declaration that no longer exists.
//
// `cap_bytes` is the one to watch. It is outside the identity and has no
// consumer at all today, which is the only reason that is safe: the moment it
// is wired to a `bytes_capped` cap it decides how many bits a length prefix
// occupies, and it becomes wire-affecting while this suite still says it is
// not. The law below is written to red at that moment rather than after it.

#include "support/netw_test.h"

#include <cstdint>

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

    // Priority orders this sender's own candidates and reaches no byte. A peer
    // that tunes it must not be locked out of a session it could have joined.
    ChannelDecl priority = probe();
    priority.priority = 9.0f;
    NETW_CHECK_EQ(identity_of(priority), base);

    // `cap_bytes` is outside identity and has NO CONSUMER, which is the only
    // thing making that safe. Wiring it to a `bytes_capped` cap would make it
    // decide the width of a length prefix, and two peers disagreeing about it
    // would then desynchronize while this hash called them compatible.
    //
    // If this assertion ever reds, the field has been brought into the hash
    // and this comment is the reason: check that it was brought in because it
    // became wire-affecting, and delete this paragraph.
    ChannelDecl cap = probe();
    cap.cap_bytes = 4096;
    NETW_CHECK_EQ(identity_of(cap), base);
}

TEST_CASE(
    "[Networked][Wire][Hosted] one declaration hashes the same twice"
) {
    // The hash walks a fixed-size array of slots rather than a container whose
    // order depends on insertion, so identity is a function of what is
    // declared and not of the order it was declared in.
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
