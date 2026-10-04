#include "support/netw_test.h"

#include "support/books.h"
#include "support/store_arrival.h"
#include "support/stream_seat.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/replication_send.hpp"

namespace TestNetwPropertySetWire {

using namespace godot;
using netw::NetwQuantizeScalar;
using netw::SchemaCore;
using netw::enums::NetwMultiplayer;
using netw::repl::PropertyBinding;
using netw::repl::PropertyBindingPtr;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw_test::Books;
using netw_test::DeclaredSet;
using netw_test::ReplicationSend;

const int64_t SYNC_ROW_DELTA = 40;

struct ColumnDecl {
    StringName key;
    int64_t lane = PropertySet::VOLATILE;
    int64_t type = SchemaCore::VARIANT;
    Ref<NetwQuantizeScalar> quantizer;
};

DeclaredSet make_set(Books &r_books, const Vector<ColumnDecl> &p_decls) {
    const DeclaredSet set = netw_test::declare(r_books);
    for (int at = 0; at < p_decls.size(); at++) {
        const ColumnDecl &decl = p_decls[at];
        PropertyColumn column = PropertyColumn::create(
            decl.key,
            decl.quantizer,
            false,
            NetwMultiplayer::ColumnType(decl.type)
        );
        column.lane = decl.lane;
        set->bind_column(column);
    }
    return set;
}

Ref<NetwQuantizeScalar> fixed_step(double p_step) {
    Ref<NetwQuantizeScalar> codec;
    codec.instantiate();
    codec->limits(-512.0, 512.0);
    codec->step(p_step);
    return codec;
}

ReplicationSend retained_send() {
    return ReplicationSend();
}

netw_test::RowOffer retained_offer(const PropertyBindingPtr &p_binding) {
    netw_test::RowOffer offer;
    offer.route = 1;
    offer.comp = 0;
    offer.channel = SYNC_ROW_DELTA;
    offer.schema = &p_binding->get_set()->get_retained_schema();
    netw_test::offer_values(
        offer,
        netw_test::lane_values(p_binding, PropertySet::RETAINED)
    );
    offer.recipients.push_back(2);
    offer.tick = -1;
    offer.ack = -1;
    offer.reliable = true;
    offer.priority = 1.0f;
    return offer;
}

void accept_pass(ReplicationSend &p_send, const PropertyBindingPtr &p_binding) {
    LocalVector<netw_test::RowOffer> owed;
    owed.push_back(retained_offer(p_binding));
    netw_test::accept_streams(p_send, owed, 2);
}

Array one_pass(ReplicationSend &p_send, const PropertyBindingPtr &p_binding) {
    LocalVector<netw_test::RowOffer> offers;
    offers.push_back(retained_offer(p_binding));
    netw_test::seat_streams(p_send, offers);
    const netw::repl::SessionResult result = p_send.run(offers, 1 << 20, 0);
    Array out;
    for (uint32_t at = 0; at < result.sends.size(); ++at) {
        out.push_back(result.sends[at].bytes);
    }
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW1 the wire hash is deterministic and 32 bit"
) {
    Books books;
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("position"), PropertySet::VOLATILE});
    decls.push_back({StringName("hp"), PropertySet::RETAINED});
    const DeclaredSet a = make_set(books, decls);
    const DeclaredSet b = make_set(books, decls);

    NETW_CHECK_EQ(a->wire_hash(), b->wire_hash());
    NETW_CHECK_GE(a->wire_hash(), 0);
    NETW_CHECK_LE(a->wire_hash(), int64_t(0xFFFFFFFF));
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW1b two schemas whose wire hashes agree in "
    "their low sixteen bits still disagree, because the hash is not masked"
) {
    Books books;
    LocalVector<int64_t> seen;
    int64_t first = 0;
    int64_t second = 0;
    for (int at = 0; at < 4096 && second == 0; at++) {
        Vector<ColumnDecl> decls;
        ColumnDecl decl;
        decl.key = StringName(String("a") + String::num_int64(at));
        decl.lane = PropertySet::VOLATILE;
        decls.push_back(decl);
        const int64_t hash = make_set(books, decls)->wire_hash();
        for (uint32_t back = 0; back < seen.size(); back++) {
            if ((seen[back] & 0xFFFF) != (hash & 0xFFFF)) {
                continue;
            }
            if (seen[back] != hash) {
                first = seen[back];
                second = hash;
            }
            break;
        }
        seen.push_back(hash);
    }

    const bool found_a_pair = second != 0;
    const bool low_halves_agree = (first & 0xFFFF) == (second & 0xFFFF);
    const bool full_hashes_differ = first != second;
    CHECK(found_a_pair);
    CHECK(low_halves_agree);
    CHECK(full_hashes_differ);
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW2 the wire hash answers order, membership "
    "and lane"
) {
    Books books;
    Vector<ColumnDecl> base_decls;
    base_decls.push_back({StringName("a"), PropertySet::VOLATILE});
    base_decls.push_back({StringName("b"), PropertySet::VOLATILE});

    Vector<ColumnDecl> reordered_decls;
    reordered_decls.push_back({StringName("b"), PropertySet::VOLATILE});
    reordered_decls.push_back({StringName("a"), PropertySet::VOLATILE});

    Vector<ColumnDecl> extra_decls = base_decls;
    extra_decls.push_back({StringName("c"), PropertySet::VOLATILE});

    Vector<ColumnDecl> relaned_decls;
    relaned_decls.push_back({StringName("a"), PropertySet::VOLATILE});
    relaned_decls.push_back({StringName("b"), PropertySet::RETAINED});

    const int64_t base = make_set(books, base_decls)->wire_hash();
    CHECK(base != make_set(books, reordered_decls)->wire_hash());
    CHECK(base != make_set(books, extra_decls)->wire_hash());
    CHECK(base != make_set(books, relaned_decls)->wire_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW3 the wire hash catches a type disagreement"
) {
    Books books;
    Vector<ColumnDecl> float_decls;
    float_decls.push_back(
        {StringName("pos"), PropertySet::VOLATILE, SchemaCore::F64}
    );
    Vector<ColumnDecl> vector_decls;
    vector_decls.push_back(
        {StringName("pos"), PropertySet::VOLATILE, SchemaCore::VECTOR3}
    );

    CHECK(
        make_set(books, float_decls)->wire_hash()
        != make_set(books, vector_decls)->wire_hash()
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW4 the wire hash catches a quantizer "
    "disagreement"
) {
    Books books;
    Vector<ColumnDecl> raw_decls;
    raw_decls.push_back(
        {StringName("pos"), PropertySet::VOLATILE, SchemaCore::VECTOR3}
    );
    Vector<ColumnDecl> packed_decls;
    packed_decls.push_back(
        {StringName("pos"),
         PropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.03)}
    );
    Vector<ColumnDecl> coarser_decls;
    coarser_decls.push_back(
        {StringName("pos"),
         PropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.5)}
    );

    const int64_t raw = make_set(books, raw_decls)->wire_hash();
    const int64_t packed = make_set(books, packed_decls)->wire_hash();
    const int64_t coarser = make_set(books, coarser_decls)->wire_hash();
    CHECK(raw != packed);
    CHECK(packed != coarser);

    const int64_t wide = make_set(books, packed_decls)->identity_hash();
    const int64_t narrow = make_set(books, coarser_decls)->identity_hash();
    CHECK(wide != narrow);
    NETW_CHECK_EQ(wide, make_set(books, packed_decls)->identity_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW5 the wire hash ignores the schema name"
) {
    Books books;
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("pos"), PropertySet::VOLATILE});
    const DeclaredSet here = make_set(books, decls);
    const DeclaredSet there = make_set(books, decls);
    here->schema.name = StringName("res://a.gd");
    there->schema.name = StringName("@script:8811");

    NETW_CHECK_EQ(here->wire_hash(), there->wire_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW7 the retained row heals whole, then masks "
    "what moved"
) {
    Books books;
    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(3.0, -4.0));
    src->set_rotation(0.5);

    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("position"), PropertySet::VOLATILE, SchemaCore::VECTOR2}
    );
    decls.push_back(
        {StringName("rotation"), PropertySet::RETAINED, SchemaCore::F32}
    );
    decls.push_back(
        {StringName("scale"), PropertySet::RETAINED, SchemaCore::VECTOR2}
    );
    const DeclaredSet set = make_set(books, decls);

    const PropertyBindingPtr binding = netw_test::bind(set, src);
    const PropertyBindingPtr dst_binding = netw_test::bind(set, dst);
    ReplicationSend send = retained_send();

    const Array retained
        = netw_test::lane_values(binding, PropertySet::RETAINED);
    NETW_CHECK_EQ(retained.size(), 2);
    NETW_CHECK_CLOSE(double(retained[0]), 0.5, 0.0001);
    CHECK(Vector2(retained[1]) == Vector2(1.0, 1.0));
    const Array volatile_values
        = netw_test::lane_values(binding, PropertySet::VOLATILE);
    NETW_CHECK_EQ(volatile_values.size(), 1);
    CHECK(Vector2(volatile_values[0]) == Vector2(3.0, -4.0));

    const Array whole = one_pass(send, binding);
    NETW_CHECK_EQ(whole.size(), 1);
    netw_test::StoreArrival landing(dst_binding);
    REQUIRE(landing.settle(whole[0], -1, false, 1));
    NETW_CHECK_CLOSE(
        double(landing.value(StringName("rotation"), 1)),
        0.5,
        0.0001
    );

    NETW_CHECK_EQ(one_pass(send, binding).size(), 0);
    accept_pass(send, binding);

    src->set_rotation(1.5);
    const Array partial = one_pass(send, binding);
    NETW_CHECK_EQ(partial.size(), 1);
    REQUIRE(landing.settle(partial[0], -1, false, 1));
    CHECK_FALSE(landing.arrival(1).whole);
    NETW_CHECK_CLOSE(
        double(landing.value(StringName("rotation"), 1)),
        1.5,
        0.0001
    );
    CHECK(Vector2(landing.value(StringName("scale"), 1)) == Vector2(1.0, 1.0));

    memdelete(dst);
    memdelete(src);
}

DeclaredSet integrated_set(Books &r_books, int64_t p_record) {
    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("velocity"),
         PropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.01)}
    );
    const DeclaredSet set = make_set(r_books, decls);
    set->record = p_record;
    return set;
}

netw::table::DeltaMode volatile_mode(const DeclaredSet &p_set) {
    return p_set->get_volatile_schema().at(0)->delta;
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW8 the derived mode ladders a quantized state "
    "column and nothing else"
) {
    Books books;
    const bool state_ladders
        = volatile_mode(integrated_set(books, PropertySet::RECORD_STATE))
        == netw::table::DeltaMode::LADDER;
    CHECK(state_ladders);

    const bool input_is_full
        = volatile_mode(integrated_set(books, PropertySet::RECORD_INPUT))
        == netw::table::DeltaMode::FULL;
    CHECK(input_is_full);

    Vector<ColumnDecl> raw;
    raw.push_back(
        {StringName("velocity"), PropertySet::VOLATILE, SchemaCore::VECTOR3}
    );
    const DeclaredSet unquantized = make_set(books, raw);
    unquantized->record = PropertySet::RECORD_STATE;
    const bool unquantized_is_full
        = volatile_mode(unquantized) == netw::table::DeltaMode::FULL;
    CHECK(unquantized_is_full);

    Ref<netw::NetwQuantizeAngle> spun;
    spun.instantiate();
    spun->set_bit_count(16);
    Vector<ColumnDecl> angled;
    angled.push_back(
        {StringName("heading"), PropertySet::VOLATILE, SchemaCore::F32}
    );
    const DeclaredSet heading = make_set(books, angled);
    heading->record = PropertySet::RECORD_STATE;
    heading->columns[0].set_quantizer(spun);
    heading->reproject_lanes();
    const bool angle_is_full
        = volatile_mode(heading) == netw::table::DeltaMode::FULL;
    CHECK(angle_is_full);

    Vector<ColumnDecl> narrow;
    narrow.push_back(
        {StringName("charge"),
         PropertySet::VOLATILE,
         SchemaCore::F32,
         fixed_step(256.0)}
    );
    const DeclaredSet coarse = make_set(books, narrow);
    coarse->record = PropertySet::RECORD_STATE;
    const bool coarse_is_full
        = volatile_mode(coarse) == netw::table::DeltaMode::FULL;
    CHECK(coarse_is_full);
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW9 an authored mode overrides the derivation "
    "and moves the identity"
) {
    Books books;
    const DeclaredSet input = integrated_set(books, PropertySet::RECORD_INPUT);
    const int64_t derived_identity = input->identity_hash();

    input->columns[0].delta_mode = PropertyColumn::DELTA_LADDER;
    input->reproject_lanes();
    const bool override_ladders
        = volatile_mode(input) == netw::table::DeltaMode::LADDER;
    CHECK(override_ladders);
    const bool identity_moved = input->identity_hash() != derived_identity;
    CHECK(identity_moved);

    const DeclaredSet state = integrated_set(books, PropertySet::RECORD_STATE);
    state->columns[0].delta_mode = PropertyColumn::DELTA_FULL;
    state->reproject_lanes();
    const bool override_refuses
        = volatile_mode(state) == netw::table::DeltaMode::FULL;
    CHECK(override_refuses);
}

} // namespace TestNetwPropertySetWire
