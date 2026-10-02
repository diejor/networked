#include "support/netw_test.h"

#include "support/send_drive.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/carrier_buffers.hpp"
#include "netw/repl/session_send.hpp"
#include "netw/wire/frame.hpp"
#include "netw/wire/registry.hpp"

using namespace godot;

namespace TestStoreAccount {

using netw::SchemaCore;
using netw::repl::SessionResult;
using netw::table::SchemaRecord;
using netw::wire::WireRegistry;
using netw_test::RowOffer;
using netw_test::SessionSend;

constexpr uint8_t CHANNEL = 50;
constexpr int PEER = 7;
constexpr int ROWS = 40;

const WireRegistry &registry() {
    static const WireRegistry made = [] {
        WireRegistry out;
        netw::wire::ChannelDecl decl;
        decl.id = CHANNEL;
        decl.name = StringName("account_probe");
        decl.delivery = netw::wire::Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

const SchemaRecord &narrow_row() {
    static const SchemaRecord record = [] {
        SchemaRecord made;
        made.name = StringName("AccountRow");
        SchemaCore::append_column(&made, StringName("x"), SchemaCore::I32, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

const SchemaRecord &held_row() {
    static const SchemaRecord record = [] {
        SchemaRecord made;
        made.name = StringName("AccountHeld");
        for (int at = 0; at < 8; ++at) {
            SchemaCore::append_column(
                &made,
                StringName(String::num_int64(at)),
                SchemaCore::I32,
                1
            );
        }
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

LocalVector<RowOffer> moving(int64_t p_tick) {
    LocalVector<RowOffer> out;
    for (int row = 0; row < ROWS; ++row) {
        RowOffer made;
        made.route = 100 + row;
        made.channel = CHANNEL;
        made.schema = &narrow_row();
        netw_test::offer_value(made, p_tick * 1000 + row);
        made.recipients.push_back(PEER);
        out.push_back(made);
    }
    return out;
}

LocalVector<RowOffer> burst(int p_count) {
    LocalVector<RowOffer> out;
    for (int row = 0; row < p_count; ++row) {
        RowOffer made;
        made.route = 500 + row;
        made.channel = CHANNEL;
        made.schema = &held_row();
        made.reliable = true;
        Array values;
        for (int at = 0; at < 8; ++at) {
            values.push_back(int64_t(row) * 7777 + at * 131071);
        }
        netw_test::offer_values(made, values);
        made.recipients.push_back(PEER);
        out.push_back(made);
    }
    return out;
}

int64_t framed_of(const SessionResult &p_result, bool p_reliable) {
    int64_t total = 0;
    for (uint32_t at = 0; at < p_result.sends.size(); ++at) {
        if (p_result.sends[at].reliable == p_reliable) {
            total += p_result.sends[at].framed;
        }
    }
    return total;
}

SessionResult drive_tick(
    SessionSend &r_session,
    const LocalVector<RowOffer> &p_offers,
    int64_t p_fill_bytes,
    int64_t p_tick
) {
    netw_test::seat_streams(r_session, p_offers);
    return r_session.run(registry(), p_offers, p_fill_bytes * 8, p_tick);
}

TEST_CASE(
    "[Networked][Store][Hosted] AC1 a saturated peer whose account refills "
    "one datagram a tick is only sent a second datagram in a tick when the "
    "account still holds a whole datagram after the first"
) {
    constexpr int64_t DATAGRAM = 100;
    SessionSend session;
    int64_t single_ticks = 0;
    int64_t unfunded_splits = 0;
    for (int64_t tick = 1; tick <= 60; ++tick) {
        const netw::repl::PeerAccount *before = session.account(PEER);
        const double banked = before != nullptr ? before->balance : 0.0;
        const double opened = banked + double(DATAGRAM) < double(2 * DATAGRAM)
            ? banked + double(DATAGRAM)
            : double(2 * DATAGRAM);
        const SessionResult result
            = drive_tick(session, moving(tick), DATAGRAM, tick);
        netw::NetwCarrierBuffers carrier;
        PackedByteArray frame;
        bool split = false;
        for (uint32_t at = 0; at < result.sends.size(); ++at) {
            frame.resize(netw::wire::frame_size(
                result.sends[at].route,
                result.sends[at].bytes.size()
            ));
            const netw::CarrierBatch early
                = carrier.append(PEER, frame, false, DATAGRAM);
            if (early.is_empty()) {
                continue;
            }
            split = true;
            if (opened - double(early.size()) < double(DATAGRAM)) {
                unfunded_splits += 1;
            }
        }
        single_ticks += split ? 0 : 1;
    }
    NETW_CHECK_GT(single_ticks, int64_t(50));
    NETW_CHECK_EQ(unfunded_splits, int64_t(0));
}

TEST_CASE(
    "[Networked][Store][Hosted] AC2 a reliable burst is charged in full and "
    "the debt it leaves holds back exactly its size of fitted rows"
) {
    constexpr int64_t FILL = 120;
    SessionSend quiet;
    SessionSend loaded;
    int64_t burst_bytes = 0;
    int64_t fitted_quiet = 0;
    int64_t fitted_loaded = 0;
    for (int64_t tick = 1; tick <= 40; ++tick) {
        const LocalVector<RowOffer> plain = moving(tick);
        LocalVector<RowOffer> with = moving(tick);
        if (tick >= 10) {
            const LocalVector<RowOffer> held = burst(12);
            for (uint32_t at = 0; at < held.size(); ++at) {
                with.push_back(held[at]);
            }
        }
        const SessionResult one = drive_tick(quiet, plain, FILL, tick);
        const SessionResult two = drive_tick(loaded, with, FILL, tick);
        burst_bytes += framed_of(two, true);
        if (tick >= 10) {
            fitted_quiet += framed_of(one, false);
            fitted_loaded += framed_of(two, false);
        }
    }
    NETW_CHECK_GT(burst_bytes, FILL * 3);
    const int64_t held_back = fitted_quiet - fitted_loaded;
    const bool the_debt_is_the_burst = held_back >= burst_bytes - 16
        && held_back <= burst_bytes + 16;
    CHECK(the_debt_is_the_burst);
}

TEST_CASE(
    "[Networked][Store][Hosted] AC3 a peer the pass skipped for five ticks "
    "is sent at most two ticks of its budget when the pass resumes"
) {
    constexpr int64_t FILL = 50;
    SessionSend session;
    drive_tick(session, moving(1), FILL, 10);
    const SessionResult resumed = drive_tick(session, moving(2), FILL, 16);
    NETW_CHECK_GT(framed_of(resumed, false), FILL);
    NETW_CHECK_LE(framed_of(resumed, false), FILL * 2);
}

TEST_CASE(
    "[Networked][Store][Hosted] AC4 a replication priority at or below zero "
    "is refused and leaves the weight in place"
) {
    Ref<netw::NetwEntity> entity;
    entity.instantiate();
    NETW_CHECK_EQ(entity->get_replication_priority(), 1.0);
    entity->set_replication_priority(2.5);
    NETW_CHECK_EQ(entity->get_replication_priority(), 2.5);
    ERR_PRINT_OFF;
    entity->set_replication_priority(0.0);
    entity->set_replication_priority(-3.0);
    ERR_PRINT_ON;
    NETW_CHECK_EQ(entity->get_replication_priority(), 2.5);
}

} // namespace TestStoreAccount
