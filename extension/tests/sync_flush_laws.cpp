#include "support/netw_test.h"

#include <cstdint>

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/replication_send.hpp"

namespace TestNetwSyncFlush {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPropertySet;
using netw::NetwPropertySetColumn;
using netw::ReplicationSend;
using netw::SchemaCore;

const int64_t SYNC_ROW = 39;
const int64_t SYNC_ROW_DELTA = 40;
const int64_t SYNC_ROW_WINDOW = 41;

Ref<NetwPropertySet> a_set() {
    Ref<NetwPropertySet> set;
    set.instantiate();
    Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
        StringName("hp"),
        Ref<netw::NetwQuantize>(),
        false,
        SchemaCore::I32
    );
    column->lane = NetwPropertySet::VOLATILE;
    set->bind_column(column);
    return set;
}

ReplicationSend a_send() {
    return ReplicationSend();
}

netw::repl::RowOffer an_offer(
    const Ref<NetwPropertySet> &p_set,
    int64_t p_route,
    int64_t p_channel,
    bool p_windowed
) {
    netw::repl::RowOffer offer;
    offer.route = p_route;
    offer.comp = 0;
    offer.channel = uint8_t(p_channel);
    offer.schema = &p_set->get_volatile_schema();
    offer.values.push_back(7);
    offer.recipients.push_back(2);
    offer.tick = 1;
    offer.ack = -1;
    offer.priority = 1.0f;
    if (p_windowed) {
        offer.windowed = true;
        offer.window = uint32_t(p_set->window);
    }
    return offer;
}

TEST_CASE(
    "[Networked][Sync][Hosted] SY1 a volatile row flush counts one row frame "
    "per send the pass answered, and a whole row is counted as whole too"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPropertySet> set = a_set();
    ReplicationSend send = a_send();

    LocalVector<netw::repl::RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW, false));

    core->sync_flush_offers(
        &send,
        offers,
        SYNC_ROW,
        SYNC_ROW_WINDOW,
        SYNC_ROW_DELTA
    );

    const Dictionary stats = core->sync_flush_stats();
    NETW_CHECK_EQ(int64_t(stats[StringName("row_frames_out")]), int64_t(1));
    NETW_CHECK_EQ(int64_t(stats[StringName("row_frames_full")]), int64_t(1));
    NETW_CHECK_EQ(
        int64_t(stats[StringName("retained_frames_out")]),
        int64_t(0)
    );
    NETW_CHECK_EQ(int64_t(stats[StringName("window_frames_out")]), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] SY2 a windowed offer is counted as a window "
    "frame and carries its sample count, never as a plain row"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPropertySet> set = a_set();
    set->window = 3;
    ReplicationSend send;

    LocalVector<netw::repl::RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW_WINDOW, true));

    core->sync_flush_offers(
        &send,
        offers,
        SYNC_ROW,
        SYNC_ROW_WINDOW,
        SYNC_ROW_DELTA
    );

    const Dictionary stats = core->sync_flush_stats();
    NETW_CHECK_EQ(int64_t(stats[StringName("window_frames_out")]), int64_t(1));
    NETW_CHECK_GT(int64_t(stats[StringName("window_samples_out")]), int64_t(0));
    NETW_CHECK_EQ(int64_t(stats[StringName("row_frames_out")]), int64_t(0));
}

TEST_CASE(
    "[Networked][Sync][Hosted] SY3 a flush with nothing offered, and one with "
    "no sender to flush through, both leave every counter where it was"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPropertySet> set = a_set();
    ReplicationSend send = a_send();

    LocalVector<netw::repl::RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW, false));
    core->sync_flush_offers(
        &send,
        offers,
        SYNC_ROW,
        SYNC_ROW_WINDOW,
        SYNC_ROW_DELTA
    );
    const int64_t after_one
        = int64_t(core->sync_flush_stats()[StringName("row_frames_out")]);

    core->sync_flush_offers(
        &send,
        LocalVector<netw::repl::RowOffer>(),
        SYNC_ROW,
        SYNC_ROW_WINDOW,
        SYNC_ROW_DELTA
    );
    core->sync_flush_offers(
        nullptr,
        offers,
        SYNC_ROW,
        SYNC_ROW_WINDOW,
        SYNC_ROW_DELTA
    );

    NETW_CHECK_EQ(
        int64_t(core->sync_flush_stats()[StringName("row_frames_out")]),
        after_one
    );
}

TEST_CASE(
    "[Networked][Sync][Hosted] SY4 the counters accumulate across flushes, "
    "because they are the session's tally rather than one pass's"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    const Ref<NetwPropertySet> set = a_set();
    ReplicationSend send = a_send();

    for (int at = 0; at < 3; at++) {
        LocalVector<netw::repl::RowOffer> offers;
        offers.push_back(an_offer(set, 1, SYNC_ROW, false));
        core->sync_flush_offers(
            &send,
            offers,
            SYNC_ROW,
            SYNC_ROW_WINDOW,
            SYNC_ROW_DELTA
        );
    }

    NETW_CHECK_EQ(
        int64_t(core->sync_flush_stats()[StringName("row_frames_out")]),
        int64_t(3)
    );
}

} // namespace TestNetwSyncFlush
