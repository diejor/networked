#include "support/netw_test.h"
#include "support/send_drive.h"

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/profile.hpp"
#include "netw/repl/session_send.hpp"

using namespace godot;

namespace TestNetwReplSendCost {

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

const uint8_t CHANNEL = 45;
const int ENTITIES = 50;
const int PEERS = 4;
const int TICKS = 240;

WireRegistry registry() {
    WireRegistry out;
    ChannelDecl decl;
    decl.id = CHANNEL;
    decl.name = godot::StringName("send_cost_probe");
    decl.delivery = Delivery::FITTED;
    out.register_channel(decl);
    return out;
}

SchemaRecord body() {
    SchemaRecord record;
    record.name = godot::StringName("Pose");
    SchemaCore::append_column(&record, "position", SchemaCore::VECTOR3, 1);
    SchemaCore::append_column(&record, "spin", SchemaCore::QUATERNION, 1);
    SchemaCore::append_column(&record, "hp", SchemaCore::I16, 1);
    SchemaCore::fix(&record);
    return record;
}

const SchemaRecord &declared() {
    static const SchemaRecord record = body();
    return record;
}

RowOffer offer(int64_t p_route, int64_t p_tick) {
    RowOffer out;
    out.route = p_route;
    out.comp = 0;
    out.channel = CHANNEL;
    out.schema = &declared();
    Array values;
    values.push_back(Vector3(float(p_tick), float(p_route), 0.0f));
    values.push_back(Quaternion());
    values.push_back(int64_t(p_tick % 100));
    out.values = values;
    for (int peer = 1; peer <= PEERS; ++peer) {
        out.recipients.push_back(peer);
    }
    out.priority = 1.0f;
    out.masked = true;
    out.tick = p_tick;
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] the send pass at fifty entities and four peers "
    "reaches every peer every tick it is not caught up"
) {
    NETW_ZONE_NC("Send cost probe", netw::colors::WIRE);
    const WireRegistry reg = registry();
    SessionSend session;

    int64_t sends = 0;
    for (int tick = 1; tick <= TICKS; ++tick) {
        LocalVector<RowOffer> offers;
        for (int entity = 0; entity < ENTITIES; ++entity) {
            offers.push_back(offer(entity + 1, tick));
        }
        const SessionResult result
            = drive_send(session, reg, offers, 1 << 20, uint16_t(tick), tick);
        sends += int64_t(result.sends.size());
        for (int peer = 1; peer <= PEERS; ++peer) {
            session.acknowledge(peer, uint16_t(tick), 0);
        }
    }

    NETW_CHECK_EQ(sends, int64_t(ENTITIES) * int64_t(PEERS) * int64_t(TICKS));
    NETW_CHECK_EQ(session.lane_count(), uint32_t(ENTITIES));
}

} // namespace TestNetwReplSendCost
