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

uint64_t fold(uint64_t h, uint64_t v) {
    return h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}

uint64_t identity_oracle(
    uint16_t p_format,
    const ChannelDecl *p_decls,
    int p_count
) {
    uint64_t hash = fold(14695981039346656037ULL, p_format);
    for (int at = 0; at < p_count; ++at) {
        const ChannelDecl &d = p_decls[at];
        hash = fold(hash, d.id);
        hash = fold(hash, uint64_t(d.kind));
        hash = fold(hash, uint64_t(d.reliability));
        hash = fold(hash, uint64_t(d.freshness));
        hash = fold(hash, uint64_t(d.delivery));
        hash = fold(hash, uint64_t(d.direction));
        hash = fold(hash, uint64_t(d.payload));
        if (d.payload_revision != 0) {
            hash = fold(hash, d.payload_revision);
        }
        hash = fold(hash, d.name.hash());
    }
    return hash;
}

ChannelDecl row_channel(
    uint8_t p_id,
    const char *p_name,
    ChannelKind p_kind,
    Reliability p_reliability,
    Freshness p_freshness,
    PayloadContract p_payload,
    uint16_t p_revision
) {
    ChannelDecl decl;
    decl.id = p_id;
    decl.name = godot::StringName(p_name);
    decl.kind = p_kind;
    decl.reliability = p_reliability;
    decl.freshness = p_freshness;
    decl.delivery = Delivery::FITTED;
    decl.direction = Direction::EITHER;
    decl.payload = p_payload;
    decl.payload_revision = p_revision;
    return decl;
}

int row_table(ChannelDecl *p_out, uint16_t p_revision, bool p_has_control) {
    int count = 0;
    p_out[count++] = row_channel(
        39,
        "SYNC_ROW",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE_ACKED,
        Freshness::FRESHEST_WINS,
        PayloadContract::DELTA,
        p_revision
    );
    p_out[count++] = row_channel(
        40,
        "SYNC_ROW_DELTA",
        ChannelKind::ROUTED,
        Reliability::RELIABLE,
        Freshness::NONE,
        PayloadContract::DELTA,
        p_revision
    );
    p_out[count++] = row_channel(
        41,
        "SYNC_ROW_WINDOW",
        ChannelKind::KEYED,
        Reliability::UNRELIABLE,
        Freshness::FRESHEST_WINS,
        PayloadContract::PLANNED,
        p_revision
    );
    if (p_has_control) {
        p_out[count++] = row_channel(
            43,
            "ROW_CONTROL",
            ChannelKind::SESSION,
            Reliability::RELIABLE,
            Freshness::NONE,
            PayloadContract::PLANNED,
            0
        );
    }
    return count;
}

uint64_t registered_identity(const ChannelDecl *p_decls, int p_count) {
    WireRegistry registry;
    for (int at = 0; at < p_count; ++at) {
        registry.register_channel(p_decls[at]);
    }
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

    const bool the_format_is_v11 = netw::wire::FORMAT_VERSION == 11;
    CHECK(the_format_is_v11);
}

TEST_CASE(
    "[Networked][Wire][Hosted] the format version is folded into protocol "
    "identity ahead of the channels, so a v10 row table cannot answer a v11 "
    "identity"
) {
    ChannelDecl v11_rows[4];
    const int v11_count = row_table(v11_rows, 1, true);
    ChannelDecl v10_rows[4];
    const int v10_count = row_table(v10_rows, 0, false);

    NETW_CHECK_EQ(
        registered_identity(v11_rows, v11_count),
        identity_oracle(netw::wire::FORMAT_VERSION, v11_rows, v11_count)
    );

    const bool the_format_alone_moves_identity
        = identity_oracle(10, v11_rows, v11_count)
        != identity_oracle(11, v11_rows, v11_count);
    CHECK(the_format_alone_moves_identity);

    const bool a_v10_peer_cannot_answer_this_identity
        = identity_oracle(10, v10_rows, v10_count)
        != registered_identity(v11_rows, v11_count);
    CHECK(a_v10_peer_cannot_answer_this_identity);

    const bool the_row_table_alone_moves_identity
        = identity_oracle(11, v10_rows, v10_count)
        != identity_oracle(11, v11_rows, v11_count);
    CHECK(the_row_table_alone_moves_identity);
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
