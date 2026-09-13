#include "support/netw_test.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/replication_send.hpp"
#include "support/netw_call_log.h"

namespace TestNetwPropertySetBinding {

using namespace godot;
using netw::NetwPropertySet;
using netw::NetwPropertySetBinding;
using netw::NetwPropertySetColumn;
using netw::ReplicationSend;
using netw::SchemaCore;

const int64_t SYNC_ROW = 39;
const int64_t SYNC_ROW_DELTA = 40;
const int64_t SYNC_ROW_WINDOW = 41;

struct ColumnDecl {
    StringName key;
    int64_t lane = NetwPropertySet::VOLATILE;
    int64_t type = SchemaCore::VARIANT;
};

Ref<NetwPropertySet> make_set(const Vector<ColumnDecl> &p_decls) {
    Ref<NetwPropertySet> set;
    set.instantiate();
    for (int at = 0; at < p_decls.size(); at++) {
        Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
            p_decls[at].key,
            Ref<netw::NetwQuantize>(),
            false,
            p_decls[at].type
        );
        column->lane = p_decls[at].lane;
        set->bind_column(column);
    }
    return set;
}

Ref<NetwPropertySet> one_column_set(int64_t p_type, const StringName &p_key) {
    Vector<ColumnDecl> decls;
    decls.push_back({p_key, NetwPropertySet::VOLATILE, p_type});
    return make_set(decls);
}

ReplicationSend send_on(int64_t, bool) {
    return ReplicationSend();
}

netw::repl::RowArrival arrival(int64_t p_tick) {
    netw::repl::RowArrival out;
    out.base_tick = p_tick;
    return out;
}

PackedByteArray one_frame(
    ReplicationSend &p_send,
    const Ref<NetwPropertySetBinding> &p_binding,
    int64_t p_channel,
    int64_t p_tick,
    int64_t p_ack
) {
    netw::repl::RowOffer offer;
    offer.route = 1;
    offer.comp = 0;
    offer.channel = uint8_t(p_channel);
    offer.schema = &p_binding->set->get_volatile_schema();
    offer.values = p_binding->volatile_row();
    offer.recipients.push_back(2);
    offer.tick = p_tick;
    offer.ack = p_ack;
    offer.priority = 1.0f;
    if (p_channel == SYNC_ROW_WINDOW) {
        offer.windowed = true;
        offer.window = uint32_t(p_binding->set->window);
    }
    LocalVector<netw::repl::RowOffer> offers;
    offers.push_back(offer);
    const netw::repl::SessionResult result
        = p_send.run(offers, 1 << 20, p_tick);
    if (result.sends.is_empty()) {
        return PackedByteArray();
    }
    return result.sends[0].bytes;
}

PackedByteArray row_frame(
    ReplicationSend &p_send,
    const Ref<NetwPropertySetBinding> &p_binding,
    int64_t p_tick,
    int64_t p_ack
) {
    return one_frame(p_send, p_binding, SYNC_ROW, p_tick, p_ack);
}

PackedByteArray window_frame(
    ReplicationSend &p_send,
    const Ref<NetwPropertySetBinding> &p_binding,
    int64_t p_tick
) {
    return one_frame(p_send, p_binding, SYNC_ROW_WINDOW, p_tick, -1);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB1 a volatile state row crosses to the "
    "receiving node"
) {
    const Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::VECTOR2, StringName("position"));
    set->stamp = NetwPropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(5.0, -6.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, NetwPropertySetBinding::create(set, src), 20, 17);
    const Dictionary header = NetwPropertySetBinding::create(set, dst)
                                  ->apply_row_frame(&send, bytes, arrival(20));

    NETW_CHECK_EQ(int64_t(header["tick"]), 20);
    NETW_CHECK_EQ(int64_t(header["ack"]), 17);
    CHECK(dst->get_position() == Vector2(5.0, -6.0));

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB2 a windowed frame displays its freshest "
    "sample"
) {
    const Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::VECTOR2, StringName("position"));
    set->stamp = NetwPropertySet::STAMP_TICK;
    set->window = 3;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    const Ref<NetwPropertySetBinding> src_binding
        = NetwPropertySetBinding::create(set, src);
    ReplicationSend send = send_on(SYNC_ROW_WINDOW, true);

    src->set_position(Vector2(1.0, 0.0));
    window_frame(send, src_binding, 10);
    src->set_position(Vector2(2.0, 0.0));
    const PackedByteArray bytes = window_frame(send, src_binding, 11);

    const Dictionary header
        = NetwPropertySetBinding::create(set, dst)
              ->apply_window_frame(&send, bytes, arrival(11));

    CHECK(dst->get_position() == Vector2(2.0, 0.0));
    NETW_CHECK_EQ(int64_t(header["tick"]), 11);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB3 a windowed apply answers every sample it "
    "carried"
) {
    const Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::F32, StringName("rotation"));
    set->stamp = NetwPropertySet::STAMP_TICK;
    set->window = 3;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    const Ref<NetwPropertySetBinding> src_binding
        = NetwPropertySetBinding::create(set, src);
    ReplicationSend send = send_on(SYNC_ROW_WINDOW, true);

    src->set_rotation(0.1);
    window_frame(send, src_binding, 10);
    src->set_rotation(0.2);
    window_frame(send, src_binding, 11);
    src->set_rotation(0.3);
    const PackedByteArray bytes = window_frame(send, src_binding, 12);

    const Dictionary header
        = NetwPropertySetBinding::create(set, dst)
              ->apply_window_frame(&send, bytes, arrival(12));
    const Array samples = header["samples"];

    NETW_CHECK_EQ(samples.size(), 3);
    const Dictionary oldest = samples[0];
    const Dictionary newest = samples[2];
    NETW_CHECK_EQ(int64_t(oldest["tick"]), 10);
    NETW_CHECK_EQ(int64_t(newest["tick"]), 12);
    const Dictionary oldest_payload = oldest["payload"];
    const Dictionary newest_payload = newest["payload"];
    NETW_CHECK_CLOSE(
        double(oldest_payload[StringName("rotation")]),
        0.1,
        0.0001
    );
    NETW_CHECK_CLOSE(
        double(newest_payload[StringName("rotation")]),
        0.3,
        0.0001
    );
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 0.3, 0.0001);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB4 a payload snapshot restores onto another "
    "node"
) {
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("position"), NetwPropertySet::VOLATILE});
    decls.push_back({StringName("rotation"), NetwPropertySet::VOLATILE});
    const Ref<NetwPropertySet> set = make_set(decls);

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(-6.0, 2.0));
    src->set_rotation(2.0);

    const Dictionary snapshot
        = NetwPropertySetBinding::create(set, src)->snapshot_payload();
    NetwPropertySetBinding::create(set, dst)->apply_payload(snapshot);

    CHECK(dst->get_position() == Vector2(-6.0, 2.0));
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 2.0, 0.0001);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB5 the apply hook carries the decoded payload "
    "and the node is snapped"
) {
    const Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::VECTOR2, StringName("position"));
    set->stamp = NetwPropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(4.0, 9.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, NetwPropertySetBinding::create(set, src), 12, 7);

    netw_test::CallLog log;
    const Ref<NetwPropertySetBinding> dst_binding
        = NetwPropertySetBinding::create(set, dst);
    dst_binding->on_applied = log.callable(StringName("applied"));
    dst_binding->apply_row_frame(&send, bytes, arrival(12));

    NETW_CHECK_EQ(log.count(StringName("applied")), 1);
    const Array args = log.args(StringName("applied"));
    NETW_CHECK_EQ(args.size(), 1);
    const Dictionary header = args[0];
    NETW_CHECK_EQ(int64_t(header["tick"]), 12);
    NETW_CHECK_EQ(int64_t(header["ack"]), 7);
    const Dictionary payload = header["payload"];
    CHECK(Vector2(payload[StringName("position")]) == Vector2(4.0, 9.0));
    CHECK(dst->get_position() == Vector2(4.0, 9.0));

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB6 a closed write gate decodes without "
    "snapping the node"
) {
    const Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::VECTOR2, StringName("position"));
    set->stamp = NetwPropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(5.0, 5.0));
    dst->set_position(Vector2(-1.0, -1.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, NetwPropertySetBinding::create(set, src), 3, -1);

    netw_test::CallLog log;
    const Ref<NetwPropertySetBinding> dst_binding
        = NetwPropertySetBinding::create(set, dst);
    dst_binding->write_gate = false;
    dst_binding->on_applied = log.callable(StringName("applied"));
    const Dictionary header
        = dst_binding->apply_row_frame(&send, bytes, arrival(3));

    CHECK(dst->get_position() == Vector2(-1.0, -1.0));
    const Dictionary payload = header["payload"];
    CHECK(Vector2(payload[StringName("position")]) == Vector2(5.0, 5.0));
    NETW_CHECK_EQ(log.count(StringName("applied")), 1);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF1 a volatile lane offers one row on "
    "SYNC_ROW, stamped with the pump's tick where nothing authored one"
) {
    Node2D *src = memnew(Node2D);
    const Ref<NetwPropertySetBinding> binding = NetwPropertySetBinding::create(
        one_column_set(SchemaCore::F32, StringName("rotation")),
        src
    );
    binding->route = 5;
    PackedInt32Array recipients;
    recipients.push_back(2);

    LocalVector<netw::repl::RowOffer> offers;
    binding->offer_rows(3, recipients, 77, 0, offers);

    REQUIRE(offers.size() == 1);
    const netw::repl::RowOffer &offer = offers[0];
    NETW_CHECK_EQ(int64_t(offer.route), 5);
    NETW_CHECK_EQ(int64_t(offer.comp), 3);
    NETW_CHECK_EQ(int64_t(offer.channel), SYNC_ROW);
    NETW_CHECK_EQ(int64_t(offer.tick), 77);
    NETW_CHECK_EQ(int64_t(offer.ack), -1);
    CHECK_FALSE(offer.windowed);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF2 an authored tick beats the pump's, "
    "because a prediction engine owns the frame it authored"
) {
    Node2D *src = memnew(Node2D);
    const Ref<NetwPropertySetBinding> binding = NetwPropertySetBinding::create(
        one_column_set(SchemaCore::F32, StringName("rotation")),
        src
    );
    binding->authored_tick = 9;
    binding->reconcile_ack = 4;

    LocalVector<netw::repl::RowOffer> offers;
    binding->offer_rows(0, PackedInt32Array(), 77, 0, offers);

    REQUIRE(offers.size() == 1);
    const netw::repl::RowOffer &offer = offers[0];
    NETW_CHECK_EQ(int64_t(offer.tick), 9);
    NETW_CHECK_EQ(int64_t(offer.ack), 4);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF3 a windowed set offers SYNC_ROW_WINDOW, "
    "and a masked one is never windowed"
) {
    Node2D *src = memnew(Node2D);
    Ref<NetwPropertySet> set
        = one_column_set(SchemaCore::F32, StringName("rotation"));
    set->stamp = NetwPropertySet::STAMP_TICK;
    set->window = 3;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, src);

    LocalVector<netw::repl::RowOffer> windowed;
    binding->offer_rows(0, PackedInt32Array(), 1, 0, windowed);
    NETW_CHECK_EQ(int64_t(windowed[0].channel), SYNC_ROW_WINDOW);
    CHECK(windowed[0].windowed);
    CHECK_FALSE(windowed[0].masked);

    set->masked = true;
    LocalVector<netw::repl::RowOffer> masked;
    binding->offer_rows(0, PackedInt32Array(), 1, 0, masked);
    NETW_CHECK_EQ(int64_t(masked[0].channel), SYNC_ROW);
    CHECK_FALSE(masked[0].windowed);
    CHECK(masked[0].masked);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF4 an externally driven volatile lane "
    "offers nothing, and the retained lane still does"
) {
    Node2D *src = memnew(Node2D);
    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("rotation"), NetwPropertySet::VOLATILE, SchemaCore::F32}
    );
    decls.push_back(
        {StringName("scale"), NetwPropertySet::RETAINED, SchemaCore::VARIANT}
    );
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(make_set(decls), src);

    LocalVector<netw::repl::RowOffer> both;
    binding->offer_rows(0, PackedInt32Array(), 1, 0, both);
    NETW_CHECK_EQ(int64_t(both.size()), 2);

    binding->volatile_external = true;
    LocalVector<netw::repl::RowOffer> offers;
    binding->offer_rows(0, PackedInt32Array(), 1, 0, offers);
    REQUIRE(offers.size() == 1);
    const netw::repl::RowOffer &offer = offers[0];
    NETW_CHECK_EQ(int64_t(offer.channel), SYNC_ROW_DELTA);
    NETW_CHECK_EQ(int64_t(offer.tick), -1);
    CHECK(offer.reliable);
    memdelete(src);
}

#if defined(NETW_TIER_HOSTED)

const char *HOLDS_VALUE = netw_test::gdsrc::PROPERTY_PRESENT;
const char *DROPS_VALUE = netw_test::gdsrc::PROPERTY_ABSENT;

bool gather_reads_field(const Ref<NetwPropertySetBinding> &p_binding) {
    const Array row = p_binding->volatile_row();
    return row.size() == 1 && int64_t(row[0]) == 7;
}

bool gather_offers_nothing(const Ref<NetwPropertySetBinding> &p_binding) {
    return p_binding->volatile_row().is_empty();
}

TEST_CASE(
    "[Networked][Sync][PropertySet] a gather re-reads the node's property "
    "list after its script changes, because a field list cached from an "
    "earlier script names properties the node no longer has"
) {
    const Ref<Script> holds = netw_test::script_from(HOLDS_VALUE);
    const Ref<Script> drops = netw_test::script_from(DROPS_VALUE);
    REQUIRE(holds.is_valid());
    REQUIRE(drops.is_valid());

    Node *probe = memnew(Node);
    probe->set_script(holds);
    const Ref<NetwPropertySetBinding> binding = NetwPropertySetBinding::create(
        one_column_set(SchemaCore::VARIANT, StringName("replicated_value")),
        probe
    );

    CHECK(gather_reads_field(binding));

    probe->set_script(drops);

    CHECK(gather_offers_nothing(binding));

    memdelete(probe);
}

#endif

} // namespace TestNetwPropertySetBinding
