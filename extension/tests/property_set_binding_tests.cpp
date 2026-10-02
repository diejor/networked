#include "support/netw_test.h"

#include "support/stream_seat.h"

#include "support/declared_nodes.h"

#include "support/minted_script.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/predict/arrival.hpp"
#include "netw/predict/compare.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/replication_send.hpp"
#include "support/books.h"
#include "support/netw_call_log.h"
#include "support/store_arrival.h"

namespace TestNetwPropertyBinding {

using namespace godot;
using netw::SchemaCore;
using netw::repl::PropertyBinding;
using netw::repl::PropertyBindingPtr;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw_test::Books;
using netw_test::DeclaredSet;
using netw_test::ReplicationSend;
using netw::enums::NetwMultiplayer;

const int64_t SYNC_ROW = 39;
const int64_t SYNC_ROW_DELTA = 40;
const int64_t SYNC_ROW_WINDOW = 41;

struct ColumnDecl {
    StringName key;
    int64_t lane = PropertySet::VOLATILE;
    int64_t type = SchemaCore::VARIANT;
};

void fill_set(PropertySet &r_set, const Vector<ColumnDecl> &p_decls) {
    for (int at = 0; at < p_decls.size(); at++) {
        PropertyColumn column = PropertyColumn::create(
            p_decls[at].key,
            Ref<netw::NetwQuantize>(),
            false,
            NetwMultiplayer::ColumnType(p_decls[at].type)
        );
        column.lane = p_decls[at].lane;
        r_set.bind_column(column);
    }
}

DeclaredSet make_set(Books &r_books, const Vector<ColumnDecl> &p_decls) {
    const DeclaredSet set = netw_test::declare(r_books);
    fill_set(*r_books.sets.get(set.id), p_decls);
    return set;
}

Vector<ColumnDecl> one_column(int64_t p_type, const StringName &p_key) {
    Vector<ColumnDecl> decls;
    decls.push_back({p_key, PropertySet::VOLATILE, p_type});
    return decls;
}

DeclaredSet one_column_set(
    Books &r_books,
    int64_t p_type,
    const StringName &p_key
) {
    return make_set(r_books, one_column(p_type, p_key));
}

ReplicationSend send_on(int64_t, bool) {
    return ReplicationSend();
}

PackedByteArray one_frame(
    ReplicationSend &p_send,
    const PropertyBindingPtr &p_binding,
    int64_t p_channel,
    int64_t p_tick,
    int64_t p_ack
) {
    netw_test::RowOffer offer;
    offer.route = 1;
    offer.comp = 0;
    offer.channel = uint8_t(p_channel);
    offer.schema = &p_binding->get_set()->get_volatile_schema();
    netw_test::offer_values(
        offer,
        netw_test::lane_values(p_binding, PropertySet::VOLATILE)
    );
    offer.recipients.push_back(2);
    offer.tick = p_tick;
    offer.ack = p_ack;
    offer.priority = 1.0f;
    if (p_channel == SYNC_ROW_WINDOW) {
        offer.windowed = true;
        offer.window = uint32_t(p_binding->get_set()->window);
    }
    LocalVector<netw_test::RowOffer> offers;
    offers.push_back(offer);
    netw_test::seat_streams(p_send, offers);
    const netw::repl::SessionResult result
        = p_send.run(offers, 1 << 20, p_tick);
    if (result.sends.is_empty()) {
        return PackedByteArray();
    }
    return result.sends[0].bytes;
}

PackedByteArray row_frame(
    ReplicationSend &p_send,
    const PropertyBindingPtr &p_binding,
    int64_t p_tick,
    int64_t p_ack
) {
    return one_frame(p_send, p_binding, SYNC_ROW, p_tick, p_ack);
}

PackedByteArray window_frame(
    ReplicationSend &p_send,
    const PropertyBindingPtr &p_binding,
    int64_t p_tick
) {
    return one_frame(p_send, p_binding, SYNC_ROW_WINDOW, p_tick, -1);
}

netw::predict::FieldCodec one_field_codec(
    const StringName &p_key,
    int p_variant,
    int64_t p_column
) {
    netw::predict::FieldCodec codec;
    codec.append(p_key, Ref<netw::NetwQuantize>(), p_variant, int(p_column));
    codec.seal();
    return codec;
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB1 a volatile state row crosses to the "
    "receiving binding's store row"
) {
    Books books;
    const DeclaredSet set
        = one_column_set(books, SchemaCore::VECTOR2, StringName("position"));
    set->stamp = PropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(5.0, -6.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, netw_test::bind(set, src), 20, 17);
    netw_test::StoreArrival landed(netw_test::bind(set, dst));
    REQUIRE(landed.settle(bytes, 20, false));

    NETW_CHECK_EQ(landed.arrival().tick, int64_t(20));
    NETW_CHECK_EQ(landed.arrival().ack, int64_t(17));
    CHECK(Vector2(landed.value(StringName("position"))) == Vector2(5.0, -6.0));

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB2 a windowed frame displays its freshest "
    "sample"
) {
    Books books;
    const DeclaredSet set
        = one_column_set(books, SchemaCore::VECTOR2, StringName("position"));
    set->stamp = PropertySet::STAMP_TICK;
    set->window = 3;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    const PropertyBindingPtr src_binding
        = netw_test::bind(set, src);
    ReplicationSend send = send_on(SYNC_ROW_WINDOW, true);

    src->set_position(Vector2(1.0, 0.0));
    window_frame(send, src_binding, 10);
    src->set_position(Vector2(2.0, 0.0));
    const PackedByteArray bytes = window_frame(send, src_binding, 11);

    netw_test::StoreArrival landed(netw_test::bind(set, dst));
    REQUIRE(landed.settle(bytes, 11, true));

    CHECK(Vector2(landed.value(StringName("position"))) == Vector2(2.0, 0.0));
    NETW_CHECK_EQ(landed.arrival().tick, int64_t(11));

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB3 a windowed apply answers every sample it "
    "carried"
) {
    Books books;
    const DeclaredSet set
        = one_column_set(books, SchemaCore::F32, StringName("rotation"));
    set->stamp = PropertySet::STAMP_TICK;
    set->window = 3;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    const PropertyBindingPtr src_binding
        = netw_test::bind(set, src);
    ReplicationSend send = send_on(SYNC_ROW_WINDOW, true);

    src->set_rotation(0.1);
    window_frame(send, src_binding, 10);
    src->set_rotation(0.2);
    window_frame(send, src_binding, 11);
    src->set_rotation(0.3);
    const PackedByteArray bytes = window_frame(send, src_binding, 12);

    netw_test::StoreArrival landed(netw_test::bind(set, dst));
    REQUIRE(landed.settle(bytes, 12, true));
    const netw::predict::Arrival samples = landed.arrival();
    const netw::predict::FieldCodec rotations = one_field_codec(
        StringName("rotation"),
        int(Variant::FLOAT),
        SchemaCore::F32
    );

    NETW_REQUIRE_EQ(int64_t(samples.sample_count()), int64_t(3));
    NETW_CHECK_EQ(samples.sample_tick(0), int64_t(10));
    NETW_CHECK_EQ(samples.sample_tick(2), int64_t(12));
    netw::predict::StateRow read;
    REQUIRE(samples.read_sample(rotations, 0, read));
    NETW_CHECK_CLOSE(double(read.values[0]), 0.1, 0.0001);
    REQUIRE(samples.read_sample(rotations, 2, read));
    NETW_CHECK_CLOSE(double(read.values[0]), 0.3, 0.0001);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] AR1 the arrival prediction reads carries the "
    "tick, ack, whole, row and every window sample the sender wrote"
) {
    Books books;
    const DeclaredSet rows
        = one_column_set(books, SchemaCore::VECTOR2, StringName("position"));
    rows->stamp = PropertySet::STAMP_TICK_ACK;
    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(5.0, -6.0));
    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray row_bytes
        = row_frame(send, netw_test::bind(rows, src), 20, 17);
    netw_test::StoreArrival landed(netw_test::bind(rows, dst));
    REQUIRE(landed.settle(row_bytes, 20, false));
    const netw::predict::Arrival arrival = landed.arrival();
    NETW_CHECK_EQ(arrival.tick, int64_t(20));
    NETW_CHECK_EQ(arrival.ack, int64_t(17));
    CHECK(arrival.whole);
    NETW_CHECK_EQ(int64_t(arrival.sample_count()), int64_t(0));
    const netw::predict::FieldCodec positions = one_field_codec(
        StringName("position"),
        int(Variant::VECTOR2),
        SchemaCore::VECTOR2
    );
    netw::predict::StateRow read;
    REQUIRE(arrival.read(positions, read));
    REQUIRE(read.has(0));
    const Vector2 held = read.values[0];
    NETW_CHECK_CLOSE(held.x, 5.0, 0.0);
    NETW_CHECK_CLOSE(held.y, -6.0, 0.0);

    const DeclaredSet window
        = one_column_set(books, SchemaCore::F32, StringName("rotation"));
    window->stamp = PropertySet::STAMP_TICK;
    window->window = 3;
    const PropertyBindingPtr spinning = netw_test::bind(window, src);
    ReplicationSend windowed = send_on(SYNC_ROW_WINDOW, true);
    src->set_rotation(0.1);
    window_frame(windowed, spinning, 10);
    src->set_rotation(0.2);
    window_frame(windowed, spinning, 11);
    src->set_rotation(0.3);
    const PackedByteArray window_bytes = window_frame(windowed, spinning, 12);
    netw_test::StoreArrival sampled(netw_test::bind(window, dst));
    REQUIRE(sampled.settle(window_bytes, 12, true));
    const netw::predict::Arrival samples = sampled.arrival();
    NETW_CHECK_EQ(samples.tick, int64_t(12));
    NETW_REQUIRE_EQ(int64_t(samples.sample_count()), int64_t(3));
    const netw::predict::FieldCodec rotations = one_field_codec(
        StringName("rotation"),
        int(Variant::FLOAT),
        SchemaCore::F32
    );
    const double sent[3] = {0.1, 0.2, 0.3};
    for (uint32_t at_sample = 0; at_sample < samples.sample_count();
         ++at_sample) {
        NETW_CHECK_EQ(samples.sample_tick(at_sample), int64_t(10 + at_sample));
        REQUIRE(samples.read_sample(rotations, at_sample, read));
        REQUIRE(read.has(0));
        NETW_CHECK_CLOSE(double(read.values[0]), sent[at_sample], 0.000001);
    }

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB4 a payload snapshot restores onto another "
    "node"
) {
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("position"), PropertySet::VOLATILE});
    decls.push_back({StringName("rotation"), PropertySet::VOLATILE});
    Books books;
    const DeclaredSet set = make_set(books, decls);

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(-6.0, 2.0));
    src->set_rotation(2.0);

    const Dictionary snapshot
        = netw_test::bind(set, src)->snapshot_payload();
    netw_test::bind(set, dst)->apply_payload(snapshot);

    CHECK(dst->get_position() == Vector2(-6.0, 2.0));
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 2.0, 0.0001);

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB5 the apply hook carries the decoded payload"
) {
    Books books;
    const DeclaredSet set
        = one_column_set(books, SchemaCore::VECTOR2, StringName("position"));
    set->stamp = PropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(4.0, 9.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, netw_test::bind(set, src), 12, 7);

    netw_test::LandedLog log;
    const PropertyBindingPtr dst_binding
        = netw_test::bind(set, dst);
    log.watch(dst_binding);
    netw_test::StoreArrival landed(dst_binding);
    REQUIRE(landed.settle(bytes, 12, false));

    NETW_CHECK_EQ(log.count, int64_t(1));
    NETW_CHECK_EQ(log.tick, int64_t(12));
    NETW_CHECK_EQ(log.ack, int64_t(7));
    CHECK(Vector2(log.value(StringName("position"))) == Vector2(4.0, 9.0));

    memdelete(dst);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB6 a closed write gate decodes and keeps the "
    "row off the node"
) {
    Books books;
    const DeclaredSet set
        = one_column_set(books, SchemaCore::VECTOR2, StringName("position"));
    set->stamp = PropertySet::STAMP_TICK_ACK;

    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(5.0, 5.0));
    dst->set_position(Vector2(-1.0, -1.0));

    ReplicationSend send = send_on(SYNC_ROW, false);
    const PackedByteArray bytes
        = row_frame(send, netw_test::bind(set, src), 3, -1);

    netw_test::LandedLog log;
    const PropertyBindingPtr dst_binding
        = netw_test::bind(set, dst);
    dst_binding->write_gate = false;
    log.watch(dst_binding);
    netw_test::StoreArrival landed(dst_binding);
    REQUIRE(landed.settle(bytes, 3, false));

    CHECK_FALSE(dst_binding->writes_node(false));
    CHECK(Vector2(landed.value(StringName("position"))) == Vector2(5.0, 5.0));
    NETW_CHECK_EQ(log.count, int64_t(1));

    memdelete(dst);
    memdelete(src);
}

struct OfferedRow {
    int64_t route = 0;
    int64_t comp = 0;
    int64_t channel = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    bool windowed = false;
    bool reliable = false;
};

struct Offered {
    Ref<netw::NetwMultiplayer> session;
    LocalVector<OfferedRow> offers;
};

struct OfferShape {
    int64_t route = 0;
    int64_t stamp = PropertySet::STAMP_NONE;
    int64_t window = 0;
    bool masked = false;
    int64_t authored_tick = -1;
    int64_t reconcile_ack = -1;
    bool volatile_external = false;
};

Offered offered(
    const Vector<ColumnDecl> &p_decls,
    const OfferShape &p_shape,
    Node *p_node,
    int64_t p_ordinal,
    int64_t p_tick
) {
    Offered out;
    out.session.instantiate();
    netw::repl::SetBook &sets = out.session->property_set_book();
    const uint64_t declared = sets.create();
    PropertySet *set = sets.get(declared);
    fill_set(*set, p_decls);
    set->stamp = p_shape.stamp;
    set->window = p_shape.window;
    set->masked = p_shape.masked;
    PropertyBinding *p_binding
        = out.session->property_binding_book().create(declared, p_node);
    p_binding->route = p_shape.route;
    p_binding->authored_tick = p_shape.authored_tick;
    p_binding->reconcile_ack = p_shape.reconcile_ack;
    p_binding->volatile_external = p_shape.volatile_external;
    out.session->store_admit(p_binding);
    netw::store::World &world = out.session->store();
    for (uint32_t at = 0; at < world.table_count(); ++at) {
        netw::store::Table &table = world.table(at);
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            world.name_lane({at, row}, uint8_t(p_ordinal));
            table.sources[row] = uint8_t(
                out.session->store_source(table, row, p_binding, true)
            );
        }
        table.stamp_tick = p_tick;
        world.pull(at, p_tick, false);
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            world.stamp(at, row);
        }
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            if (table.sources[row] != uint8_t(netw::store::Source::LOCAL)
                || (table.flags[row] & netw::store::Table::UNGATHERED) != 0) {
                continue;
            }
            OfferedRow made;
            made.route = table.routes[row];
            made.comp = table.ordinals[row];
            made.channel = table.channel;
            made.tick = table.frame_ticks[row];
            made.ack = table.reconcile_acks[row];
            made.windowed = table.family == netw::store::Family::WINDOW;
            made.reliable = table.family == netw::store::Family::RETAINED;
            out.offers.push_back(made);
        }
    }
    return out;
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF1 a volatile lane offers one row on "
    "SYNC_ROW, stamped with the pump's tick where nothing authored one"
) {
    Node2D *src = memnew(Node2D);
    OfferShape shape;
    shape.route = 5;

    const Offered made = offered(
        one_column(SchemaCore::F32, StringName("rotation")),
        shape,
        src,
        3,
        77
    );
    const LocalVector<OfferedRow> &offers = made.offers;

    REQUIRE(offers.size() == 1);
    const OfferedRow &offer = offers[0];
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
    OfferShape shape;
    shape.authored_tick = 9;
    shape.reconcile_ack = 4;

    const Offered made = offered(
        one_column(SchemaCore::F32, StringName("rotation")),
        shape,
        src,
        0,
        77
    );
    const LocalVector<OfferedRow> &offers = made.offers;

    REQUIRE(offers.size() == 1);
    const OfferedRow &offer = offers[0];
    NETW_CHECK_EQ(int64_t(offer.tick), 9);
    NETW_CHECK_EQ(int64_t(offer.ack), 4);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF3 a windowed set offers SYNC_ROW_WINDOW, "
    "and a masked one is never windowed"
) {
    Node2D *src = memnew(Node2D);
    const Vector<ColumnDecl> decls
        = one_column(SchemaCore::F32, StringName("rotation"));
    OfferShape shape;
    shape.stamp = PropertySet::STAMP_TICK;
    shape.window = 3;

    const Offered first = offered(decls, shape, src, 0, 1);
    const LocalVector<OfferedRow> &windowed = first.offers;
    REQUIRE(windowed.size() == 1);
    NETW_CHECK_EQ(int64_t(windowed[0].channel), SYNC_ROW_WINDOW);
    CHECK(windowed[0].windowed);

    shape.masked = true;
    const Offered second = offered(decls, shape, src, 0, 1);
    const LocalVector<OfferedRow> &masked = second.offers;
    REQUIRE(masked.size() == 1);
    NETW_CHECK_EQ(int64_t(masked[0].channel), SYNC_ROW);
    CHECK_FALSE(masked[0].windowed);
    memdelete(src);
}

TEST_CASE(
    "[Networked][Repl][Hosted] PB-OF4 an externally driven volatile lane "
    "offers nothing, and the retained lane still does"
) {
    Node2D *src = memnew(Node2D);
    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("rotation"), PropertySet::VOLATILE, SchemaCore::F32}
    );
    decls.push_back(
        {StringName("scale"), PropertySet::RETAINED, SchemaCore::VARIANT}
    );
    OfferShape shape;
    const Offered first = offered(decls, shape, src, 0, 1);
    NETW_CHECK_EQ(int64_t(first.offers.size()), 2);

    shape.volatile_external = true;
    const Offered second = offered(decls, shape, src, 0, 1);
    const LocalVector<OfferedRow> &offers = second.offers;
    REQUIRE(offers.size() == 1);
    const OfferedRow &offer = offers[0];
    NETW_CHECK_EQ(int64_t(offer.channel), SYNC_ROW_DELTA);
    NETW_CHECK_EQ(int64_t(offer.tick), -1);
    CHECK(offer.reliable);
    memdelete(src);
}

#if defined(NETW_TIER_HOSTED)

const char *HOLDS_VALUE = netw_test::gdsrc::PROPERTY_PRESENT;
const char *DROPS_VALUE = netw_test::gdsrc::PROPERTY_ABSENT;

bool pull_reads_field(netw::store::World &p_world, int64_t p_tick) {
    netw::store::Table &table = p_world.table(0);
    table.sources[0] = uint8_t(netw::store::Source::LOCAL);
    p_world.pull(0, p_tick, false);
    return (table.flags[0] & netw::store::Table::UNGATHERED) == 0
        && int64_t(table.read_value(0, 0)) == 7;
}

bool pull_offers_nothing(netw::store::World &p_world, int64_t p_tick) {
    netw::store::Table &table = p_world.table(0);
    table.sources[0] = uint8_t(netw::store::Source::LOCAL);
    p_world.pull(0, p_tick, false);
    return (table.flags[0] & netw::store::Table::UNGATHERED) != 0;
}

TEST_CASE(
    "[Networked][Sync][PropertySet] a pull re-reads the node's property "
    "list after its script changes, because a field list cached from an "
    "earlier script names properties the node no longer has"
) {
    const Ref<Script> holds = netw_test::script_from(HOLDS_VALUE);
    const Ref<Script> drops = netw_test::script_from(DROPS_VALUE);
    REQUIRE(holds.is_valid());
    REQUIRE(drops.is_valid());

    Node *probe = memnew(Node);
    probe->set_script(holds);
    Ref<netw::NetwMultiplayer> session;
    session.instantiate();
    netw::repl::SetBook &sets = session->property_set_book();
    const uint64_t declared = sets.create();
    fill_set(
        *sets.get(declared),
        one_column(SchemaCore::VARIANT, StringName("replicated_value"))
    );
    session->store_admit(
        session->property_binding_book().create(declared, probe)
    );
    REQUIRE(session->store().table_count() == 1);

    CHECK(pull_reads_field(session->store(), 1));

    probe->set_script(drops);

    CHECK(pull_offers_nothing(session->store(), 2));

    memdelete(probe);
}

#endif

} // namespace TestNetwPropertyBinding
