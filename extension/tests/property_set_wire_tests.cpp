#include "support/netw_test.h"

#include <cstdint>

#include "godot/node.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/property_set_binding.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/replication_send.hpp"

namespace TestNetwPropertySetWire {

using namespace godot;
using netw::NetwPropertySet;
using netw::NetwPropertySetBinding;
using netw::NetwPropertySetColumn;
using netw::NetwQuantizeScalar;
using netw::ReplicationSend;
using netw::SchemaCore;

const int64_t SYNC_ROW_DELTA = 40;

struct ColumnDecl {
    StringName key;
    int64_t lane = NetwPropertySet::VOLATILE;
    int64_t type = SchemaCore::VARIANT;
    Ref<NetwQuantizeScalar> quantizer;
};

Ref<NetwPropertySet> make_set(const Vector<ColumnDecl> &p_decls) {
    Ref<NetwPropertySet> set;
    set.instantiate();
    for (int at = 0; at < p_decls.size(); at++) {
        const ColumnDecl &decl = p_decls[at];
        Ref<NetwPropertySetColumn> column = NetwPropertySetColumn::create(
            decl.key,
            decl.quantizer,
            false,
            decl.type
        );
        column->lane = decl.lane;
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

netw::repl::RowOffer retained_offer(
    const Ref<NetwPropertySetBinding> &p_binding
) {
    netw::repl::RowOffer offer;
    offer.route = 1;
    offer.comp = 0;
    offer.channel = SYNC_ROW_DELTA;
    offer.schema = &p_binding->set->get_retained_schema();
    offer.values = p_binding->retained_row();
    offer.recipients.push_back(2);
    offer.tick = -1;
    offer.ack = -1;
    offer.reliable = true;
    offer.priority = 1.0f;
    return offer;
}

Array one_pass(
    ReplicationSend &p_send,
    const Ref<NetwPropertySetBinding> &p_binding
) {
    LocalVector<netw::repl::RowOffer> offers;
    offers.push_back(retained_offer(p_binding));
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
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("position"), NetwPropertySet::VOLATILE});
    decls.push_back({StringName("hp"), NetwPropertySet::RETAINED});
    const Ref<NetwPropertySet> a = make_set(decls);
    const Ref<NetwPropertySet> b = make_set(decls);

    NETW_CHECK_EQ(a->wire_hash(), b->wire_hash());
    NETW_CHECK_GE(a->wire_hash(), 0);
    NETW_CHECK_LE(a->wire_hash(), int64_t(0xFFFFFFFF));
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW1b two schemas whose wire hashes agree in "
    "their low sixteen bits still disagree, because the hash is not masked"
) {
    LocalVector<int64_t> seen;
    int64_t first = 0;
    int64_t second = 0;
    for (int at = 0; at < 4096 && second == 0; at++) {
        Vector<ColumnDecl> decls;
        ColumnDecl decl;
        decl.key = StringName(String("a") + String::num_int64(at));
        decl.lane = NetwPropertySet::VOLATILE;
        decls.push_back(decl);
        const int64_t hash = make_set(decls)->wire_hash();
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
    Vector<ColumnDecl> base_decls;
    base_decls.push_back({StringName("a"), NetwPropertySet::VOLATILE});
    base_decls.push_back({StringName("b"), NetwPropertySet::VOLATILE});

    Vector<ColumnDecl> reordered_decls;
    reordered_decls.push_back({StringName("b"), NetwPropertySet::VOLATILE});
    reordered_decls.push_back({StringName("a"), NetwPropertySet::VOLATILE});

    Vector<ColumnDecl> extra_decls = base_decls;
    extra_decls.push_back({StringName("c"), NetwPropertySet::VOLATILE});

    Vector<ColumnDecl> relaned_decls;
    relaned_decls.push_back({StringName("a"), NetwPropertySet::VOLATILE});
    relaned_decls.push_back({StringName("b"), NetwPropertySet::RETAINED});

    const int64_t base = make_set(base_decls)->wire_hash();
    CHECK(base != make_set(reordered_decls)->wire_hash());
    CHECK(base != make_set(extra_decls)->wire_hash());
    CHECK(base != make_set(relaned_decls)->wire_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW3 the wire hash catches a type disagreement"
) {
    Vector<ColumnDecl> float_decls;
    float_decls.push_back(
        {StringName("pos"), NetwPropertySet::VOLATILE, SchemaCore::F64}
    );
    Vector<ColumnDecl> vector_decls;
    vector_decls.push_back(
        {StringName("pos"), NetwPropertySet::VOLATILE, SchemaCore::VECTOR3}
    );

    CHECK(
        make_set(float_decls)->wire_hash()
        != make_set(vector_decls)->wire_hash()
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW4 the wire hash catches a quantizer "
    "disagreement"
) {
    Vector<ColumnDecl> raw_decls;
    raw_decls.push_back(
        {StringName("pos"), NetwPropertySet::VOLATILE, SchemaCore::VECTOR3}
    );
    Vector<ColumnDecl> packed_decls;
    packed_decls.push_back(
        {StringName("pos"),
         NetwPropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.03)}
    );
    Vector<ColumnDecl> coarser_decls;
    coarser_decls.push_back(
        {StringName("pos"),
         NetwPropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.5)}
    );

    const int64_t raw = make_set(raw_decls)->wire_hash();
    const int64_t packed = make_set(packed_decls)->wire_hash();
    const int64_t coarser = make_set(coarser_decls)->wire_hash();
    CHECK(raw != packed);
    CHECK(packed != coarser);

    const int64_t wide = make_set(packed_decls)->identity_hash();
    const int64_t narrow = make_set(coarser_decls)->identity_hash();
    CHECK(wide != narrow);
    NETW_CHECK_EQ(wide, make_set(packed_decls)->identity_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW5 the wire hash ignores the schema name"
) {
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("pos"), NetwPropertySet::VOLATILE});
    const Ref<NetwPropertySet> here = make_set(decls);
    const Ref<NetwPropertySet> there = make_set(decls);
    here->schema.name = StringName("res://a.gd");
    there->schema.name = StringName("@script:8811");

    NETW_CHECK_EQ(here->wire_hash(), there->wire_hash());
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW6 the volatile row bails on a missing field"
) {
    Node2D *src = memnew(Node2D);
    Vector<ColumnDecl> decls;
    decls.push_back({StringName("nonexistent"), NetwPropertySet::VOLATILE});

    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(make_set(decls), src);
    CHECK(binding->volatile_row().is_empty());

    memdelete(src);
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW7 the retained row heals whole, then masks "
    "what moved"
) {
    Node2D *src = memnew(Node2D);
    Node2D *dst = memnew(Node2D);
    src->set_position(Vector2(3.0, -4.0));
    src->set_rotation(0.5);

    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("position"), NetwPropertySet::VOLATILE, SchemaCore::VECTOR2}
    );
    decls.push_back(
        {StringName("rotation"), NetwPropertySet::RETAINED, SchemaCore::F32}
    );
    decls.push_back(
        {StringName("scale"), NetwPropertySet::RETAINED, SchemaCore::VECTOR2}
    );
    const Ref<NetwPropertySet> set = make_set(decls);

    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, src);
    const Ref<NetwPropertySetBinding> dst_binding
        = NetwPropertySetBinding::create(set, dst);
    ReplicationSend send = retained_send();

    const Array retained = binding->retained_row();
    NETW_CHECK_EQ(retained.size(), 2);
    NETW_CHECK_CLOSE(double(retained[0]), 0.5, 0.0001);
    CHECK(Vector2(retained[1]) == Vector2(1.0, 1.0));
    const Array volatile_values = binding->volatile_row();
    NETW_CHECK_EQ(volatile_values.size(), 1);
    CHECK(Vector2(volatile_values[0]) == Vector2(3.0, -4.0));

    const Array whole = one_pass(send, binding);
    NETW_CHECK_EQ(whole.size(), 1);
    CHECK_FALSE(
        dst_binding
            ->apply_retained_row(&send, whole[0], netw::repl::RowArrival())
            .is_empty()
    );
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 0.5, 0.0001);

    NETW_CHECK_EQ(one_pass(send, binding).size(), 0);

    src->set_rotation(1.5);
    const Array partial = one_pass(send, binding);
    NETW_CHECK_EQ(partial.size(), 1);
    const Dictionary header = dst_binding->apply_retained_row(
        &send,
        partial[0],
        netw::repl::RowArrival()
    );
    CHECK_FALSE(bool(header["whole"]));
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 1.5, 0.0001);
    CHECK(dst->get_scale() == Vector2(1.0, 1.0));

    memdelete(dst);
    memdelete(src);
}

Ref<NetwPropertySet> integrated_set(int64_t p_record) {
    Vector<ColumnDecl> decls;
    decls.push_back(
        {StringName("velocity"),
         NetwPropertySet::VOLATILE,
         SchemaCore::VECTOR3,
         fixed_step(0.01)}
    );
    const Ref<NetwPropertySet> set = make_set(decls);
    set->record = p_record;
    return set;
}

netw::table::DeltaMode volatile_mode(const Ref<NetwPropertySet> &p_set) {
    return p_set->get_volatile_schema().at(0)->delta;
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW8 the derived mode ladders a quantized state "
    "column and nothing else"
) {
    const bool state_ladders
        = volatile_mode(integrated_set(NetwPropertySet::RECORD_STATE))
        == netw::table::DeltaMode::LADDER;
    CHECK(state_ladders);

    const bool input_is_full
        = volatile_mode(integrated_set(NetwPropertySet::RECORD_INPUT))
        == netw::table::DeltaMode::FULL;
    CHECK(input_is_full);

    Vector<ColumnDecl> raw;
    raw.push_back(
        {StringName("velocity"), NetwPropertySet::VOLATILE, SchemaCore::VECTOR3}
    );
    const Ref<NetwPropertySet> unquantized = make_set(raw);
    unquantized->record = NetwPropertySet::RECORD_STATE;
    const bool unquantized_is_full
        = volatile_mode(unquantized) == netw::table::DeltaMode::FULL;
    CHECK(unquantized_is_full);

    Ref<netw::NetwQuantizeAngle> spun;
    spun.instantiate();
    spun->set_bit_count(16);
    Vector<ColumnDecl> angled;
    angled.push_back(
        {StringName("heading"), NetwPropertySet::VOLATILE, SchemaCore::F32}
    );
    const Ref<NetwPropertySet> heading = make_set(angled);
    heading->record = NetwPropertySet::RECORD_STATE;
    const Ref<NetwPropertySetColumn> spun_column = heading->columns[0];
    spun_column->set_quantizer(spun);
    heading->reproject_lanes();
    const bool angle_is_full
        = volatile_mode(heading) == netw::table::DeltaMode::FULL;
    CHECK(angle_is_full);

    Vector<ColumnDecl> narrow;
    narrow.push_back(
        {StringName("charge"),
         NetwPropertySet::VOLATILE,
         SchemaCore::F32,
         fixed_step(256.0)}
    );
    const Ref<NetwPropertySet> coarse = make_set(narrow);
    coarse->record = NetwPropertySet::RECORD_STATE;
    const bool coarse_is_full
        = volatile_mode(coarse) == netw::table::DeltaMode::FULL;
    CHECK(coarse_is_full);
}

TEST_CASE(
    "[Networked][Wire][Hosted] PW9 an authored mode overrides the derivation "
    "and moves the identity"
) {
    const Ref<NetwPropertySet> input
        = integrated_set(NetwPropertySet::RECORD_INPUT);
    const int64_t derived_identity = input->identity_hash();

    const Ref<NetwPropertySetColumn> column = input->columns[0];
    column->delta_mode = NetwPropertySetColumn::DELTA_LADDER;
    input->reproject_lanes();
    const bool override_ladders
        = volatile_mode(input) == netw::table::DeltaMode::LADDER;
    CHECK(override_ladders);
    const bool identity_moved = input->identity_hash() != derived_identity;
    CHECK(identity_moved);

    const Ref<NetwPropertySet> state
        = integrated_set(NetwPropertySet::RECORD_STATE);
    const Ref<NetwPropertySetColumn> held = state->columns[0];
    held->delta_mode = NetwPropertySetColumn::DELTA_FULL;
    state->reproject_lanes();
    const bool override_refuses
        = volatile_mode(state) == netw::table::DeltaMode::FULL;
    CHECK(override_refuses);
}

} // namespace TestNetwPropertySetWire
