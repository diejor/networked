#include "support/netw_test.h"

#include "support/stream_seat.h"

#include <cstdint>

#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/property_set.hpp"
#include "netw/replication_send.hpp"

namespace TestNetwSyncFlush {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SchemaCore;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw_test::ReplicationSend;
using netw_test::RowOffer;

const int64_t SYNC_ROW = 39;
const int64_t SYNC_ROW_DELTA = 40;
const int64_t SYNC_ROW_WINDOW = 41;

PropertySet *a_set(PropertySet &r_set) {
    PropertySet *set = &r_set;
    PropertyColumn column = PropertyColumn::create(
        StringName("hp"),
        Ref<netw::NetwQuantize>(),
        false,
        NetwMultiplayer::ColumnType(SchemaCore::I32)
    );
    column.lane = PropertySet::VOLATILE;
    set->bind_column(column);
    return set;
}

RowOffer an_offer(
    PropertySet *p_set,
    int64_t p_route,
    int64_t p_channel,
    bool p_windowed
) {
    RowOffer offer;
    offer.route = p_route;
    offer.comp = 0;
    offer.channel = uint8_t(p_channel);
    offer.schema = &p_set->get_volatile_schema();
    netw_test::offer_value(offer, 7);
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

void flush(
    const Ref<NetwMultiplayer> &p_core,
    ReplicationSend *p_send,
    const LocalVector<RowOffer> &p_offers
) {
    if (p_send != nullptr) {
        netw_test::seat_streams(*p_send, p_offers);
        netw_test::stage_offers(p_send->world, p_offers, 1);
    }
    p_core->sync_flush_rows(p_send, SYNC_ROW, SYNC_ROW_WINDOW, SYNC_ROW_DELTA);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SY1 a volatile row flush counts one row frame "
    "per send the pass answered, and a whole row is counted as whole too"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    PropertySet declared;
    PropertySet *const set = a_set(declared);
    ReplicationSend send;

    LocalVector<RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW, false));
    flush(core, &send, offers);

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
    PropertySet declared;
    PropertySet *const set = a_set(declared);
    set->window = 3;
    ReplicationSend send;

    LocalVector<RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW_WINDOW, true));
    flush(core, &send, offers);

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
    PropertySet declared;
    PropertySet *const set = a_set(declared);
    ReplicationSend send;

    LocalVector<RowOffer> offers;
    offers.push_back(an_offer(set, 1, SYNC_ROW, false));
    flush(core, &send, offers);
    const int64_t after_one
        = int64_t(core->sync_flush_stats()[StringName("row_frames_out")]);

    flush(core, &send, LocalVector<RowOffer>());
    flush(core, nullptr, offers);

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
    PropertySet declared;
    PropertySet *const set = a_set(declared);
    ReplicationSend send;

    for (int at = 0; at < 3; at++) {
        LocalVector<RowOffer> offers;
        offers.push_back(an_offer(set, 1, SYNC_ROW, false));
        netw_test::offer_value(offers[0], 7 + at);
        flush(core, &send, offers);
    }

    NETW_CHECK_EQ(
        int64_t(core->sync_flush_stats()[StringName("row_frames_out")]),
        int64_t(3)
    );
}

} // namespace TestNetwSyncFlush
