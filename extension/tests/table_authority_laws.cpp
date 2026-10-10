#include "support/mesh_stand.h"

#if defined(NETW_TIER_HOSTED)

#include "netw/api/netw_multiplayer.hpp"
#include "netw/schema_model.hpp"
#include "netw/wire/registry.hpp"

namespace TestTableAuthority {

using namespace godot;
using namespace netw_test;
using netw::NetwMultiplayer;
using netw::NetwSchema;
namespace schema_model = netw::schema_model;

constexpr int SERVER = 1;
constexpr int MEMBER = 9;
constexpr int LATE = 11;
constexpr int OTHER = 7;

struct Declared {
    Declared() {
        schema_model::clear();
        const Ref<netw::NetwQuantize> none;
        const Ref<NetwSchema> schema = NetwSchema::declare("AuthMob");
        schema->vector3("pos", none, 1);
        schema->i32("hp", 1);
        schema->reliable(true);
    }
    ~Declared() {
        schema_model::clear();
    }
};

PackedInt64Array int64s(const std::initializer_list<int64_t> &values) {
    PackedInt64Array out;
    for (const int64_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedInt32Array int32s(const std::initializer_list<int32_t> &values) {
    PackedInt32Array out;
    for (const int32_t value : values) {
        out.push_back(value);
    }
    return out;
}

PackedVector3Array points(int p_count) {
    PackedVector3Array out;
    for (int at = 0; at < p_count; ++at) {
        out.push_back(Vector3(float(at), 0.0f, 0.0f));
    }
    return out;
}

Error publish(
    NetwMultiplayer *p_core,
    const RID &p_table,
    const PackedInt64Array &p_routes,
    const PackedInt32Array &p_hits
) {
    p_core->table_write_routes(p_table, p_routes);
    p_core->table_write_column(p_table, 0, points(p_routes.size()));
    p_core->table_write_column(p_table, 1, p_hits);
    return p_core->table_commit(p_table);
}

int64_t table_channel() {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *decl
        = registry.find_channel_by_name(StringName("TABLE"));
    REQUIRE(decl != nullptr);
    return int64_t(decl->id);
}

PackedByteArray body(int p_size) {
    PackedByteArray out;
    out.resize(p_size);
    return out;
}

TEST_CASE(
    "[Networked][Table][Authority] TA1 a wave committed at the server "
    "reaches a member, and the member follows the next wave and the release "
    "that retires the rows"
) {
    Declared declared;
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(SERVER);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);

    const RID table = host->table_find_or_adopt("AuthMob");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    const PackedInt64Array routes = host->liveness_claim_routes(2);
    NETW_CHECK_EQ(publish(host, table, routes, int32s({100, 90})), OK);
    stand.step_ticks(4);

    const RID mirror = guest->table_find("AuthMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid() || routes.size() != 2) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 2);
    const PackedInt32Array seen = guest->table_read_column(mirror, 1);
    NETW_CHECK_EQ(seen.size(), 2);
    if (seen.size() == 2) {
        NETW_CHECK_EQ(seen[1], 90);
    }

    NETW_CHECK_EQ(publish(host, table, routes, int32s({41, 42})), OK);
    stand.step_ticks(4);

    const PackedInt32Array moved = guest->table_read_column(mirror, 1);
    NETW_CHECK_EQ(moved.size(), 2);
    if (moved.size() == 2) {
        NETW_CHECK_EQ(moved[0], 41);
        NETW_CHECK_EQ(moved[1], 42);
    }

    host->liveness_release_routes(routes);
    stand.step_ticks(4);

    NETW_CHECK_EQ(guest->table_get_row(mirror, routes[0]), -1);
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 0);
}

TEST_CASE(
    "[Networked][Table][Authority] TA2 a peer that reaches the server after "
    "the wave is healed by the server's snapshot rather than by the next "
    "change"
) {
    Declared declared;
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(SERVER);
    REQUIRE(host != nullptr);
    const RID table = host->table_find_or_adopt("AuthMob");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    const PackedInt64Array routes = host->liveness_claim_routes(2);
    NETW_CHECK_EQ(publish(host, table, routes, int32s({11, 12})), OK);
    stand.step_ticks(4);

    stand.seat_member(LATE);
    stand.wire(SERVER, LATE);
    stand.step_ticks(8);

    NetwMultiplayer *joined = stand.session_of(LATE);
    REQUIRE(joined != nullptr);
    const RID mirror = joined->table_find("AuthMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(joined->table_read_routes(mirror).size(), 2);
    const PackedInt32Array seen = joined->table_read_column(mirror, 1);
    NETW_CHECK_EQ(seen.size(), 2);
    if (seen.size() == 2) {
        NETW_CHECK_EQ(seen[0], 11);
    }
}

TEST_CASE(
    "[Networked][Table][Authority] TA3 a member publishes no table, so "
    "adopting one is not permission to author rows"
) {
    Declared declared;
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(OTHER);
    stand.seat_member(MEMBER);
    stand.wire(SERVER, OTHER);
    stand.wire(SERVER, MEMBER);
    stand.wire(OTHER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *other = stand.session_of(OTHER);
    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(other != nullptr);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(other->table_publishes()), 0);

    const RID table = other->table_find_or_adopt("AuthMob");
    NETW_CHECK_EQ(int(table.is_valid()), 1);
    if (!table.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(
        publish(other, table, int64s({901, 902}), int32s({7, 8})),
        OK
    );
    stand.step_ticks(6);

    const RID mirror = guest->table_find("AuthMob");
    NETW_CHECK_EQ(int(mirror.is_valid()), 1);
    if (!mirror.is_valid()) {
        return;
    }
    NETW_CHECK_EQ(guest->table_read_routes(mirror).size(), 0);
}

TEST_CASE(
    "[Networked][Table][Authority] TA4 a member admits a table frame from the "
    "server and refuses the same frame from another member"
) {
    Declared declared;
    MeshStand stand;
    stand.seat_server();
    stand.seat_member(MEMBER);
    stand.wire(SERVER, MEMBER);
    stand.pump(4);

    NetwMultiplayer *guest = stand.session_of(MEMBER);
    REQUIRE(guest != nullptr);

    const int64_t channel = table_channel();
    const int refused
        = int(guest->table_admit_frame_default(OTHER, channel, body(4)));
    const int admitted
        = int(guest->table_admit_frame_default(SERVER, channel, body(4)));
    NETW_CHECK_EQ(refused, int(ERR_UNAUTHORIZED));
    NETW_CHECK_EQ(int(admitted == int(ERR_UNAUTHORIZED)), 0);
}

} // namespace TestTableAuthority

#endif
