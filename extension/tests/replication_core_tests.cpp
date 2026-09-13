#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/call_park.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

namespace TestReplicationCore {

using namespace godot;
using netw::ReplicationCore;

struct Stand {
    Ref<netw::NetwMultiplayer> session;
    ReplicationCore *plane = nullptr;

    Stand() {
        session.instantiate();
        plane = session->get_replication_plane();
    }
};

TEST_CASE(
    "[Networked][Repl][Hosted] the plane owns its four halves from the moment "
    "it exists, because anything built after it can already dispatch"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;

    CHECK(plane->get_sync_model() != nullptr);
    CHECK(plane->get_sync_pipeline() != nullptr);
    CHECK(plane->get_spawn_pipeline() != nullptr);
    CHECK(plane->get_spawner_compat() != nullptr);
    CHECK(plane->get_sync_compat() != nullptr);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the two sync families share one declaration "
    "table, which is what makes their ordinals agree on one route"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;
    netw::NetwSyncModel *model = plane->get_sync_model();

    model->declare(
        7,
        netw::NetwSyncModel::KIND_CONSUMED,
        StringName("Sync"),
        0,
        RID(),
        0,
        0x1234,
        0,
        0
    );
    model->declare(
        7,
        netw::NetwSyncModel::KIND_DERIVED,
        StringName("."),
        0,
        RID(),
        0,
        0x5678,
        0,
        0
    );

    REQUIRE(model->route_rows(7) != nullptr);
    NETW_CHECK_EQ(int(model->route_rows(7)->size()), 2);
    const netw::repl::SetRow *first = model->row(7, 0);
    const netw::repl::SetRow *second = model->row(7, 1);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    NETW_CHECK_EQ(first->kind, int64_t(netw::NetwSyncModel::KIND_CONSUMED));
    NETW_CHECK_EQ(second->kind, int64_t(netw::NetwSyncModel::KIND_DERIVED));
}

TEST_CASE(
    "[Networked][Repl][Hosted] an empty datagram is nothing to dispatch, and "
    "a plane with no session refuses every frame one carries"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;

    NETW_CHECK_EQ(
        int(plane->receive_carrier(PackedByteArray(), 1, true, -1, -1)),
        int(OK)
    );

    PackedByteArray payload;
    payload.push_back(1);
    const PackedByteArray one_frame
        = netw::wire::frame_pack(2, 0, 17, payload, String());
    NETW_CHECK_EQ(
        int(plane->receive_carrier(one_frame, 1, true, -1, -1)),
        int(ERR_UNAVAILABLE)
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] a plane with no session refuses to dispatch "
    "rather than answering a frame it cannot resolve"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;

    PackedByteArray payload;
    payload.push_back(1);

    NETW_CHECK_EQ(
        int(plane->dispatch(0, 0, 17, payload, String(), 1, true, -1)),
        int(ERR_UNAVAILABLE)
    );
}

TEST_CASE(
    "[Networked][Repl][Hosted] the channel numbering this plane dispatches on "
    "is the wire registry's, so the shell cannot rename a channel alone"
) {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();

    const netw::wire::ChannelDecl *call
        = registry.find_channel_by_name(StringName("CALL"));
    const netw::wire::ChannelDecl *spawn
        = registry.find_channel_by_name(StringName("SPAWN"));
    const netw::wire::ChannelDecl *sync_row
        = registry.find_channel_by_name(StringName("SYNC_ROW"));
    const netw::wire::ChannelDecl *missing
        = registry.find_channel_by_name(StringName("NOT_A_CHANNEL"));

    REQUIRE(call != nullptr);
    REQUIRE(spawn != nullptr);
    REQUIRE(sync_row != nullptr);
    NETW_CHECK_EQ(int(call->id), 3);
    NETW_CHECK_EQ(int(spawn->id), 14);
    NETW_CHECK_EQ(int(sync_row->id), 39);
    CHECK(missing == nullptr);
}

TEST_CASE(
    "[Networked][Repl][Hosted] the counters name every drop reason the "
    "resolver keeps and every tally its four halves keep"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;
    const Dictionary rows = plane->counters();

    CHECK(rows.has(StringName("drops_unknown_route")));
    CHECK(rows.has(StringName("drops_not_live")));
    CHECK(rows.has(StringName("drops_no_node")));
    CHECK(rows.has(StringName("drops_traversal")));
    CHECK(rows.has(StringName("drops_comp_unresolved")));
    CHECK(rows.has(StringName("derived_sets_active")));
    CHECK(rows.has(StringName("spawn_book_armed")));
    CHECK(rows.has(StringName("drops_uncaptured_custom")));
    CHECK(rows.has(StringName("sync_sets_active")));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a protocol handler answers for its channel and "
    "an unsettled protocol channel is reported rather than assumed"
) {
    ReplicationCore held;
    ReplicationCore *plane = &held;

    NETW_CHECK_EQ(int(plane->settle_channels()), int(ERR_UNCONFIGURED));
}

TEST_CASE(
    "[Networked][Repl][Hosted] a deferrable frame for a route this peer has "
    "not seen yet is PARKED rather than counted as unknown, and a session "
    "with no shell parks it the same way, because the deferrer is the "
    "session itself"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> session = stand.session;
    ReplicationCore *plane = stand.plane;

    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *call
        = registry.find_channel_by_name(StringName("CALL"));
    REQUIRE(call != nullptr);

    NETW_CHECK_EQ(session->get_rpc_park()->size(), 0);

    plane->dispatch(
        9,
        0,
        int64_t(call->id),
        PackedByteArray(),
        String(),
        2,
        true,
        -1
    );

    NETW_CHECK_EQ(session->get_rpc_park()->size(), 1);

    plane->dispatch(
        9,
        0,
        int64_t(call->id),
        PackedByteArray(),
        String(),
        2,
        false,
        -1
    );

    NETW_CHECK_EQ(session->get_rpc_park()->size(), 1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] ending a session clears the plane the session "
    "was told to route through, because the plane's sets name the routes that "
    "session issued and no other"
) {
    Stand stand;
    const Ref<netw::NetwMultiplayer> session = stand.session;
    ReplicationCore *plane = stand.plane;

    plane->get_sync_model()->declare(
        7,
        int64_t(netw::NetwSyncModel::KIND_CONSUMED),
        StringName("pos"),
        0,
        RID(),
        3,
        0,
        0,
        0
    );
    REQUIRE(plane->get_sync_model()->route_rows(7) != nullptr);
    NETW_CHECK_EQ(int(plane->get_sync_model()->route_rows(7)->size()), 1);

    session->clear_session_state();

    const godot::LocalVector<netw::repl::SetRow> *after
        = plane->get_sync_model()->route_rows(7);
    NETW_CHECK_EQ(after == nullptr ? 0 : int(after->size()), 0);
}

TEST_CASE(
    "[Networked][Repl][Hosted] a session answers _native_core with ITSELF, so "
    "a caller holding either the session or the shell that wraps one reaches "
    "the same plane through one reading"
) {
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();

    const Variant held = session->get(StringName("_native_core"));
    NETW_CHECK_EQ(Object::cast_to<netw::NetwMultiplayer>(held), session.ptr());

    const Variant absent = session->get(StringName("_not_a_member"));
    CHECK(absent.get_type() == Variant::NIL);
}

} // namespace TestReplicationCore
