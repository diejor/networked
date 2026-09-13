#include "support/netw_test.h"
#include "support/send_drive.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/session_send.hpp"

using namespace godot;

namespace TestNetwReplSaturation {

using godot::Array;
using godot::LocalVector;
using netw::SchemaCore;
using netw::repl::RowOffer;
using netw::repl::SessionResult;
using netw::repl::SessionSend;
using netw::table::SchemaRecord;
using netw::wire::ChannelDecl;
using netw::wire::Delivery;
using netw::wire::WireRegistry;
using netw_test::drive_send;

const uint8_t CHANNEL = 44;
const int PEER = 3;
const int ROUTES = 8;
const int PASSES = 24;

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("saturation_probe");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

SchemaRecord body() {
    SchemaRecord record;
    record.name = godot::StringName("Saturated");
    SchemaCore::append_column(
        &record,
        godot::StringName("x"),
        SchemaCore::I32,
        1
    );
    SchemaCore::append_column(
        &record,
        godot::StringName("y"),
        SchemaCore::I32,
        1
    );
    SchemaCore::fix(&record);
    return record;
}

const SchemaRecord &declared() {
    static const SchemaRecord record = body();
    return record;
}

RowOffer offer(int64_t p_route, int64_t p_value, float p_priority) {
    RowOffer out;
    out.route = p_route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &declared();
    Array values;
    values.push_back(p_value);
    values.push_back(p_value * 2);
    out.values = values;
    out.recipients.push_back(PEER);
    out.priority = p_priority;
    out.masked = true;
    out.tick = p_value;
    return out;
}

struct Saturation {
    int sends[ROUTES] = {0};
    int longest_gap[ROUTES] = {0};
    int last_seen[ROUTES];

    Saturation() {
        for (int at = 0; at < ROUTES; ++at) {
            last_seen[at] = -1;
        }
    }
};

int64_t one_frame_bits() {
    const WireRegistry reg = registry();
    SessionSend probe;
    LocalVector<RowOffer> offers;
    offers.push_back(offer(1, 1, 1.0f));
    const SessionResult out = probe.run(reg, offers, 1 << 20, 0);
    return out.sends.is_empty() ? 0 : out.sends[0].bits;
}

Saturation saturate(bool p_ranked) {
    const WireRegistry reg = registry();
    const int64_t budget_bits = one_frame_bits() * ROUTES / 3;
    SessionSend session;
    Saturation out;

    for (int pass = 0; pass < PASSES; ++pass) {
        LocalVector<RowOffer> offers;
        for (int route = 0; route < ROUTES; ++route) {
            offers.push_back(
                offer(route + 1, pass + 1, p_ranked ? float(route + 1) : 1.0f)
            );
        }
        const int64_t p_budget_bits = budget_bits;
        const SessionResult result = drive_send(
            session,
            reg,
            offers,
            p_budget_bits,
            uint16_t(pass + 1)
        );

        for (uint32_t at = 0; at < result.sends.size(); ++at) {
            const int route = int(result.sends[at].route) - 1;
            out.sends[route] += 1;
            const int gap = pass - out.last_seen[route];
            if (gap > out.longest_gap[route]) {
                out.longest_gap[route] = gap;
            }
            out.last_seen[route] = pass;
        }
    }
    for (int route = 0; route < ROUTES; ++route) {
        const int gap = PASSES - out.last_seen[route];
        if (gap > out.longest_gap[route]) {
            out.longest_gap[route] = gap;
        }
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] under three times its budget every route still "
    "reaches the peer"
) {
    const Saturation out = saturate(true);

    for (int route = 0; route < ROUTES; ++route) {
        const bool the_route_was_sent = out.sends[route] > 0;
        CHECK(the_route_was_sent);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] equally entitled routes take turns rather than "
    "letting one starve"
) {
    const Saturation out = saturate(false);

    for (int route = 0; route < ROUTES; ++route) {
        const bool the_wait_is_bounded = out.longest_gap[route] <= ROUTES;
        CHECK(the_wait_is_bounded);
    }
}

TEST_CASE(
    "[Networked][Repl][Hosted] under three times its budget the higher "
    "priority route gets the larger share"
) {
    const Saturation out = saturate(true);

    const bool the_top_beats_the_bottom = out.sends[ROUTES - 1] > out.sends[0];
    CHECK(the_top_beats_the_bottom);
}

} // namespace TestNetwReplSaturation
