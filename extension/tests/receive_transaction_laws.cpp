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
#include "netw/repl/snapshot_frame.hpp"
#include "netw/replication_send.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/value_row.hpp"
#include "support/netw_call_log.h"

namespace TestNetwReceiveTransaction {

using namespace godot;
using netw::NetwPropertySet;
using netw::NetwPropertySetBinding;
using netw::NetwPropertySetColumn;
using netw::ReplicationSend;
using netw::SchemaCore;
using netw::repl::RowArrival;
using netw::wire::CodeRow;
using netw::wire::WirePlan;

const uint64_t TOKEN = 41;
const uint64_t COLUMN_POSITION = 1;
const uint64_t COLUMN_ROTATION = 2;
const uint64_t COLUMN_BOTH = COLUMN_POSITION | COLUMN_ROTATION;

Ref<NetwPropertySet> pose_set() {
    Ref<NetwPropertySet> set;
    set.instantiate();
    Ref<NetwPropertySetColumn> position = NetwPropertySetColumn::create(
        StringName("position"),
        Ref<netw::NetwQuantize>(),
        false,
        SchemaCore::VECTOR2
    );
    position->lane = NetwPropertySet::VOLATILE;
    set->bind_column(position);
    Ref<NetwPropertySetColumn> rotation = NetwPropertySetColumn::create(
        StringName("rotation"),
        Ref<netw::NetwQuantize>(),
        false,
        SchemaCore::F32
    );
    rotation->lane = NetwPropertySet::VOLATILE;
    set->bind_column(rotation);
    return set;
}

Array pose_values(const Vector2 &p_position, double p_rotation) {
    Array out;
    out.push_back(p_position);
    out.push_back(p_rotation);
    return out;
}

CodeRow pose_row(
    const Ref<NetwPropertySet> &p_set,
    const Vector2 &p_position,
    double p_rotation
) {
    const netw::table::SchemaRecord &schema = p_set->get_volatile_schema();
    const WirePlan plan = WirePlan::compile(schema);
    CodeRow row = CodeRow::for_plan(plan);
    netw::wire::encode_scalar_row(
        schema,
        pose_values(p_position, p_rotation),
        row
    );
    return row;
}

PackedByteArray pose_frame(
    const Ref<NetwPropertySet> &p_set,
    const Vector2 &p_position,
    double p_rotation,
    uint64_t p_mask,
    uint64_t p_revision = 1,
    const CodeRow *p_baseline = nullptr
) {
    const netw::table::SchemaRecord &schema = p_set->get_volatile_schema();
    const WirePlan plan = WirePlan::compile(schema);
    const CodeRow row = pose_row(p_set, p_position, p_rotation);
    netw::repl::SnapshotHeader header;
    header.token = TOKEN;
    header.revision = p_revision;
    header.distance = p_baseline == nullptr ? 0 : 1;
    header.tick = -1;
    header.reconcile_ack = -1;
    header.mask = p_baseline == nullptr ? plan.full_mask() : p_mask;
    return netw::repl::write_snapshot_row(
        header,
        -1,
        plan,
        row,
        p_baseline
    );
}

RowArrival arrival() {
    RowArrival out;
    out.base_tick = -1;
    out.life = -1;
    out.seq = -1;
    return out;
}


TEST_CASE(
    "[Networked][Repl][Hosted] C4-1 a staged row that is then denied leaves "
    "the node and the held codes exactly as the last committed row left them"
) {
    const Ref<NetwPropertySet> set = pose_set();
    Node2D *dst = memnew(Node2D);
    ReplicationSend send;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, dst);

    const CodeRow first = pose_row(set, Vector2(1.0, 1.0), 1.0);
    binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(1.0, 1.0), 1.0, COLUMN_BOTH),
        arrival()
    );
    const bool committed_the_first_row
        = dst->get_position() == Vector2(1.0, 1.0);
    CHECK(committed_the_first_row);

    binding->name_baseline(&first);
    const Dictionary denied = binding->stage_row_frame(
        &send,
        pose_frame(set, Vector2(2.0, 2.0), 1.0, COLUMN_POSITION, 2, &first),
        arrival()
    );
    const bool the_denied_row_decoded = !denied.is_empty();
    CHECK(the_denied_row_decoded);
    binding->discard_staged();

    const bool the_node_did_not_move
        = dst->get_position() == Vector2(1.0, 1.0);
    CHECK(the_node_did_not_move);

    binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(2.0, 2.0), 3.0, COLUMN_ROTATION, 3, &first),
        arrival()
    );
    binding->name_baseline(nullptr);
    const bool the_denied_column_never_reached_the_node
        = dst->get_position() == Vector2(1.0, 1.0);
    CHECK(the_denied_column_never_reached_the_node);
    NETW_CHECK_CLOSE(double(dst->get_rotation()), 3.0, 0.0001);

    memdelete(dst);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-2 staging a snapshot row leaves the "
    "baseline it composed from exactly where it was, and a torn body is "
    "refused whole"
) {
    const Ref<NetwPropertySet> set = pose_set();
    const netw::table::SchemaRecord &schema = set->get_volatile_schema();
    ReplicationSend send;

    const CodeRow base = pose_row(set, Vector2(1.0, 2.0), 0.5);
    const PackedByteArray base_before = base.to_bytes();
    const PackedByteArray delta
        = pose_frame(set, Vector2(4.0, 5.0), 2.0, COLUMN_BOTH, 2, &base);

    CodeRow staged;
    const Dictionary decoded
        = send.stage_snapshot(schema, delta, -1, &base, &staged);
    const bool the_row_decoded = bool(decoded.get("ok", false));
    CHECK(the_row_decoded);
    const bool staging_left_the_baseline_alone = base.to_bytes() == base_before;
    CHECK(staging_left_the_baseline_alone);

    PackedByteArray torn = delta;
    torn.resize(delta.size() - 1);
    CodeRow unused;
    const Dictionary refused
        = send.stage_snapshot(schema, torn, -1, &base, &unused);
    const bool the_torn_row_was_refused = !bool(refused.get("ok", false));
    CHECK(the_torn_row_was_refused);
    const bool the_refusal_left_the_baseline_alone
        = base.to_bytes() == base_before;
    CHECK(the_refusal_left_the_baseline_alone);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-3 a row naming a property the receiving "
    "node does not carry commits nothing and notifies nobody"
) {
    const Ref<NetwPropertySet> set = pose_set();
    Node *bare = memnew(Node);
    ReplicationSend send;
    netw_test::CallLog log;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, bare);
    binding->on_applied = log.callable(StringName("applied"));

    const Dictionary header = binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(3.0, 3.0), 1.0, COLUMN_BOTH),
        arrival()
    );
    const bool the_row_was_refused = header.is_empty();
    CHECK(the_row_was_refused);
    NETW_CHECK_EQ(log.count(StringName("applied")), 0);
    const bool nothing_is_left_staged = !binding->has_staged();
    CHECK(nothing_is_left_staged);

    memdelete(bare);
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Repl] C4-4 a target that vanishes after a committed row "
    "resets the stream, so the next row composes from nothing"
) {
    const Ref<NetwPropertySet> set = pose_set();
    Node *dst = netw_test::minted_node(
        "extends Node\n"
        "var position := Vector2.ZERO\n"
        "var rotation := 0.0\n"
    );
    REQUIRE(dst != nullptr);
    const Ref<Script> script = dst->get_script();
    ReplicationSend send;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, dst);

    binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(5.0, 5.0), 1.0, COLUMN_BOTH),
        arrival()
    );
    const bool the_first_row_committed
        = Vector2(dst->get(StringName("position"))) == Vector2(5.0, 5.0);
    CHECK(the_first_row_committed);

    const CodeRow first = pose_row(set, Vector2(5.0, 5.0), 1.0);
    dst->set_script(Variant());
    binding->name_baseline(&first);
    const Dictionary refused = binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(6.0, 6.0), 1.0, COLUMN_POSITION, 2, &first),
        arrival()
    );
    binding->name_baseline(nullptr);
    const bool the_row_was_refused = refused.is_empty();
    CHECK(the_row_was_refused);
    const bool the_stream_was_reset = binding->stream_is_reset();
    CHECK(the_stream_was_reset);

    dst->set_script(script);
    binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(0.0, 0.0), 4.0, COLUMN_BOTH, 3),
        arrival()
    );
    const bool the_lost_row_did_not_survive_as_a_baseline
        = Vector2(dst->get(StringName("position"))) == Vector2(0.0, 0.0);
    CHECK(the_lost_row_did_not_survive_as_a_baseline);
    NETW_CHECK_CLOSE(double(dst->get(StringName("rotation"))), 4.0, 0.0001);

    memdelete(dst);
}

#endif

TEST_CASE(
    "[Networked][Repl][Hosted] C4-5 a closed write gate still commits the "
    "row it hands to its consumer, so the next row composes on it"
) {
    const Ref<NetwPropertySet> set = pose_set();
    Node2D *dst = memnew(Node2D);
    dst->set_position(Vector2(-1.0, -1.0));
    ReplicationSend send;
    netw_test::CallLog log;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, dst);
    binding->write_gate = false;
    binding->on_applied = log.callable(StringName("applied"));

    binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(7.0, 8.0), 1.0, COLUMN_BOTH),
        arrival()
    );
    const bool the_node_was_not_snapped
        = dst->get_position() == Vector2(-1.0, -1.0);
    CHECK(the_node_was_not_snapped);
    NETW_CHECK_EQ(log.count(StringName("applied")), 1);

    const CodeRow first = pose_row(set, Vector2(7.0, 8.0), 1.0);
    binding->name_baseline(&first);
    const Dictionary second = binding->apply_row_frame(
        &send,
        pose_frame(set, Vector2(9.0, 9.0), 5.0, COLUMN_ROTATION, 2, &first),
        arrival()
    );
    binding->name_baseline(nullptr);
    const bool the_second_row_committed = !second.is_empty();
    CHECK(the_second_row_committed);
    NETW_CHECK_EQ(log.count(StringName("applied")), 2);
    const Dictionary payload = second[StringName("payload")];
    const bool the_consumer_saw_the_committed_baseline
        = Vector2(payload[StringName("position")]) == Vector2(7.0, 8.0);
    CHECK(the_consumer_saw_the_committed_baseline);

    memdelete(dst);
}

TEST_CASE(
    "[Networked][Repl][Hosted] C4-6 a decode hook that runs the body twice "
    "notifies once, and one that never commits notifies not at all"
) {
    const Ref<NetwPropertySet> set = pose_set();
    Node2D *dst = memnew(Node2D);
    ReplicationSend send;
    netw_test::CallLog log;
    const Ref<NetwPropertySetBinding> binding
        = NetwPropertySetBinding::create(set, dst);
    binding->on_applied = log.callable(StringName("applied"));

    const PackedByteArray whole
        = pose_frame(set, Vector2(2.0, 4.0), 6.0, COLUMN_BOTH);
    binding->stage_row_frame(&send, whole, arrival());
    binding->stage_row_frame(&send, whole, arrival());
    NETW_CHECK_EQ(log.count(StringName("applied")), 0);

    Dictionary header;
    const bool the_row_committed
        = binding->commit_staged(&send, header) == OK;
    CHECK(the_row_committed);
    NETW_CHECK_EQ(log.count(StringName("applied")), 1);

    const CodeRow first = pose_row(set, Vector2(2.0, 4.0), 6.0);
    binding->name_baseline(&first);
    binding->stage_row_frame(
        &send,
        pose_frame(set, Vector2(3.0, 3.0), 6.0, COLUMN_POSITION, 2, &first),
        arrival()
    );
    binding->discard_staged();
    binding->name_baseline(nullptr);
    NETW_CHECK_EQ(log.count(StringName("applied")), 1);

    memdelete(dst);
}

} // namespace TestNetwReceiveTransaction
