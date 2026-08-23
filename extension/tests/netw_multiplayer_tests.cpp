#include "support/netw_test.h"

#include "netw/carrier_frame.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/loopback.hpp"
#include "godot/multiplayer_synchronizer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/resource.hpp"
#include "godot/script.hpp"
#include "godot/utility.hpp"
#include "godot/viewport.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/wire/registry.hpp"
#include "support/event_ring.h"
#include "support/entity_facets.h"
#include "support/netw_call_log.h"
#include "support/netw_recorder.h"

namespace TestNetwMultiplayer {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SchemaCore;

TEST_CASE("[Networked][Multiplayer][Hosted] offline core is server-shaped") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->get_unique_id(), 1);
    NETW_CHECK_EQ(core->is_server(), true);
    NETW_CHECK_EQ(core->has_multiplayer_peer(), false);
    NETW_CHECK_EQ(core->NETW_API_VIRTUAL(get_peer_ids)().size(), 0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] peer ids are a native read view") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    PackedInt32Array ids;
    ids.push_back(2);
    ids.push_back(7);

    core->set_peer_ids(ids);

    NETW_CHECK_EQ(core->NETW_API_VIRTUAL(get_peer_ids)().size(), 2);
    NETW_CHECK_EQ(core->NETW_API_VIRTUAL(get_peer_ids)()[0], 2);
    NETW_CHECK_EQ(core->NETW_API_VIRTUAL(get_peer_ids)()[1], 7);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the session owns one core a plane") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->get_liveness_core().is_valid(), true);
    NETW_CHECK_EQ(core->get_clock_handle().is_valid(), true);
    NETW_CHECK_EQ(core->get_scene_core().is_valid(), true);

    NETW_CHECK_EQ(
        core->get_liveness_core() == core->get_liveness_core(),
        true
    );
    NETW_CHECK_EQ(
        &core->interest_plane() == &core->interest_plane(),
        true
    );
}

TEST_CASE("[Networked][Multiplayer][Hosted] two sessions share no core") {
    Ref<NetwMultiplayerCore> first;
    first.instantiate();
    Ref<NetwMultiplayerCore> second;
    second.instantiate();

    NETW_CHECK_EQ(first->get_liveness_core() == second->get_liveness_core(),
        false);
    NETW_CHECK_EQ(
        &first->session_plane() == &second->session_plane(),
        false
    );
    NETW_CHECK_EQ(
        first->get_clock_handle() == second->get_clock_handle(),
        false
    );
    NETW_CHECK_EQ(first->get_scene_core() == second->get_scene_core(), false);
    NETW_CHECK_EQ(
        &first->interest_plane() == &second->interest_plane(),
        false
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a drained awareness relay hands back one "
    "row per target that has edges, and comes back empty"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    CHECK(core->interest_awareness_drain().is_empty());

    core->interest_awareness_queue_layer(7, 42, StringName("sight"), 1);
    core->interest_awareness_queue_layer(7, 43, StringName("sight"), 0);
    core->interest_awareness_queue_observer(9, 42, StringName("sight"), 7, 1);

    const Array drained = core->interest_awareness_drain();

    NETW_CHECK_EQ(drained.size(), 2);
    const Array first = drained[0];
    NETW_CHECK_EQ(int(first[0]), 7);
    NETW_CHECK_EQ(Array(first[1]).size(), 2);
    const Array second = drained[1];
    NETW_CHECK_EQ(int(second[0]), 9);
    NETW_CHECK_EQ(Array(second[1]).size(), 1);

    CHECK(core->interest_awareness_drain().is_empty());
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] an awareness edge addressed to peer 0 is "
    "refused rather than queued under an id no drain can send to"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    ERR_PRINT_OFF;

    core->interest_awareness_queue_layer(0, 42, StringName("sight"), 1);
    core->interest_awareness_queue_observer(0, 42, StringName("sight"), 7, 1);

    ERR_PRINT_ON;
    CHECK(core->interest_awareness_drain().is_empty());

    core->interest_awareness_queue_layer(7, 42, StringName("sight"), 1);
    NETW_CHECK_EQ(core->interest_awareness_drain().size(), 1);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] forgetting a peer drops that peer's "
    "awareness edges and leaves every other peer's queued"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->interest_awareness_queue_layer(7, 42, StringName("sight"), 1);
    core->interest_awareness_queue_layer(9, 42, StringName("sight"), 1);

    core->interest_awareness_forget(7);

    const Array drained = core->interest_awareness_drain();
    NETW_CHECK_EQ(drained.size(), 1);
    NETW_CHECK_EQ(int(Array(drained[0])[0]), 9);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] clearing the awareness relay sends "
    "nothing and leaves nothing to drain"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->interest_awareness_queue_layer(7, 42, StringName("sight"), 1);

    core->interest_awareness_clear();

    CHECK(core->interest_awareness_drain().is_empty());
}

TEST_CASE("[Networked][Multiplayer][Hosted] interest resets without a swap") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    netw::InterestEngine &engine = core->interest_plane();
    engine.set_intent_all(4);
    NETW_CHECK_EQ(engine.has_entity(4), true);

    core->reset_interest();

    NETW_CHECK_EQ(&core->interest_plane() == &engine, true);
    NETW_CHECK_EQ(engine.has_entity(4), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the session reads its own machine") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->get_state(), netw::SessionCore::STATE_OFFLINE);
    NETW_CHECK_EQ(core->get_role(), netw::SessionCore::ROLE_NONE);
    NETW_CHECK_EQ(core->is_online(), false);

    core->session_plane().transition(netw::SessionCore::STATE_CONNECTING);
    core->session_plane().transition(netw::SessionCore::STATE_ONLINE);

    NETW_CHECK_EQ(core->get_state(), netw::SessionCore::STATE_ONLINE);
    NETW_CHECK_EQ(core->is_online(), true);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a listen server is both faces") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    core->session_plane().set_role(netw::SessionCore::ROLE_CLIENT);
    NETW_CHECK_EQ(core->is_host(), false);
    NETW_CHECK_EQ(core->is_local_client(), true);

    core->session_plane().set_role(
        netw::SessionCore::ROLE_DEDICATED_SERVER
    );
    NETW_CHECK_EQ(core->is_host(), true);
    NETW_CHECK_EQ(core->is_local_client(), false);

    core->session_plane().set_role(
        netw::SessionCore::ROLE_LISTEN_SERVER
    );
    NETW_CHECK_EQ(core->is_host(), true);
    NETW_CHECK_EQ(core->is_local_client(), true);
}

TEST_CASE("[Networked][Multiplayer][Hosted] peer identity is not a role") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->is_server(), true);
    NETW_CHECK_EQ(core->is_host(), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the uncrossed verbs refuse") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(
        core->NETW_API_VIRTUAL(object_configuration_add)(nullptr, Variant()),
        ERR_UNAVAILABLE
    );
    NETW_CHECK_EQ(
        core->NETW_API_VIRTUAL(object_configuration_remove)(nullptr, Variant()),
        ERR_UNAVAILABLE
    );
#if defined(NETW_MODULE)
    NETW_CHECK_EQ(core->rpcp(nullptr, 1, "m", nullptr, 0), ERR_UNAVAILABLE);
#else
    NETW_CHECK_EQ(core->_rpc(1, nullptr, "m", Array()), ERR_UNAVAILABLE);
#endif
}

TEST_CASE("[Networked][Multiplayer][Hosted] a packet counts with its bytes") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    core->count_sent(40);
    core->count_sent(2);
    core->count_received(9);

    NETW_CHECK_EQ(core->get_sent_packets(), 2);
    NETW_CHECK_EQ(core->get_sent_bytes(), 42);
    NETW_CHECK_EQ(core->get_received_packets(), 1);
    NETW_CHECK_EQ(core->get_received_bytes(), 9);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a standalone ack is a state ack") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    core->count_state_ack_out();
    core->count_standalone_ack_out();
    core->count_state_ack_in();

    NETW_CHECK_EQ(core->get_state_acks_out(), 2);
    NETW_CHECK_EQ(core->get_standalone_acks_out(), 1);
    NETW_CHECK_EQ(core->get_state_acks_in(), 1);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the first poll has no delta") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->poll_delta(9000), 0.0);
    NETW_CHECK_CLOSE(core->poll_delta(9000 + 500000), 0.5, 1e-9);
    NETW_CHECK_EQ(core->poll_delta(9000 + 500000), 0.0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the frame counter is the stamp") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->get_frame_counter(), 0);
    core->advance_frame();
    core->advance_frame();
    NETW_CHECK_EQ(core->get_frame_counter(), 2);
}

TEST_CASE("[Networked][Multiplayer][Hosted] freshness reads the half window") {
    NETW_CHECK_EQ(NetwMultiplayerCore::seq_is_fresher(5, 4), true);
    NETW_CHECK_EQ(NetwMultiplayerCore::seq_is_fresher(4, 5), false);
    NETW_CHECK_EQ(NetwMultiplayerCore::seq_is_fresher(5, 5), false);
    NETW_CHECK_EQ(NetwMultiplayerCore::seq_is_fresher(2, 65534), true);
    NETW_CHECK_EQ(NetwMultiplayerCore::seq_is_fresher(65534, 2), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a send seq never repeats") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->next_send_seq(7), 1);
    NETW_CHECK_EQ(core->next_send_seq(7), 2);
    NETW_CHECK_EQ(core->next_send_seq(9), 1);
    NETW_CHECK_EQ(core->next_send_seq(7), 3);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a reordered datagram is dropped") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->has_inbound_seq(3), false);
    NETW_CHECK_EQ(core->note_inbound_seq(3, 100), true);
    NETW_CHECK_EQ(core->note_inbound_seq(3, 99), false);
    NETW_CHECK_EQ(core->inbound_seq(3), 100);
    NETW_CHECK_EQ(core->note_inbound_seq(3, 101), true);
    NETW_CHECK_EQ(core->inbound_seq(3), 101);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a stalled ack holds its baseline") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->peer_ack(4), -1);
    NETW_CHECK_EQ(core->note_peer_ack(4, 0), true);
    NETW_CHECK_EQ(core->peer_ack(4), 0);
    NETW_CHECK_EQ(core->note_peer_ack(4, 900), true);
    NETW_CHECK_EQ(core->note_peer_ack(4, 900), false);
    NETW_CHECK_EQ(core->note_peer_ack(4, 800), false);
    NETW_CHECK_EQ(core->peer_ack(4), 900);
}

TEST_CASE("[Networked][Multiplayer][Hosted] an unechoed peer is owed one") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->peers_owed_echo().size(), 0);
    core->note_inbound_seq(6, 20);
    NETW_CHECK_EQ(core->peers_owed_echo().size(), 1);
    NETW_CHECK_EQ(core->peers_owed_echo()[0], 6);

    core->note_echoed_seq(6, 20);
    NETW_CHECK_EQ(core->peers_owed_echo().size(), 0);

    core->note_inbound_seq(6, 21);
    NETW_CHECK_EQ(core->peers_owed_echo().size(), 1);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a departed peer keeps no book") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->next_send_seq(8);
    core->note_inbound_seq(8, 5);
    core->note_peer_ack(8, 5);

    core->forget_peer_seqs(8);

    NETW_CHECK_EQ(core->has_inbound_seq(8), false);
    NETW_CHECK_EQ(core->peer_ack(8), -1);
    NETW_CHECK_EQ(core->next_send_seq(8), 1);
}

TEST_CASE("[Networked][Multiplayer][Hosted] the verdict partition is closed") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(NetwMultiplayerCore::counts_verdict(ERR_BUSY), true);
    NETW_CHECK_EQ(NetwMultiplayerCore::counts_verdict(OK), false);
    NETW_CHECK_EQ(NetwMultiplayerCore::counts_verdict(ERR_UNCONFIGURED), false);

    NETW_CHECK_EQ(core->count_verdict(ERR_BUSY), true);
    NETW_CHECK_EQ(core->count_verdict(ERR_BUSY), true);
    NETW_CHECK_EQ(core->count_verdict(OK), false);

    NETW_CHECK_EQ(core->verdict_total(ERR_BUSY), 2);
    NETW_CHECK_EQ(core->verdict_total(ERR_SKIP), 0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] what a verdict is worth saying is "
          "a closed partition") {
    struct Row {
        int64_t verdict;
        netw::GateVerdictBook::Report report;
    };
    const Row TABLE[] = {
        { OK, netw::GateVerdictBook::QUIET },
        { ERR_DOES_NOT_EXIST, netw::GateVerdictBook::QUIET },
        { ERR_SKIP, netw::GateVerdictBook::QUIET },
        { ERR_UNAVAILABLE, netw::GateVerdictBook::QUIET },
        { ERR_UNAUTHORIZED, netw::GateVerdictBook::WARN },
        { ERR_INVALID_DATA, netw::GateVerdictBook::WARN },
        { ERR_BUSY, netw::GateVerdictBook::WARN },
        { ERR_UNCONFIGURED, netw::GateVerdictBook::DEFECT },
        { ERR_PARSE_ERROR, netw::GateVerdictBook::DEFECT },
    };
    for (const Row &row : TABLE) {
        NETW_CHECK_EQ(
            int(netw::GateVerdictBook::report_of(row.verdict)),
            int(row.report)
        );
    }
}

TEST_CASE("[Networked][Multiplayer][Hosted] a sink counts every verdict but "
          "OK, and answers what it was handed") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->sink_verdict(OK, 4), int64_t(OK));
    NETW_CHECK_EQ(core->sink_verdict(ERR_SKIP, 4), int64_t(ERR_SKIP));
    NETW_CHECK_EQ(core->sink_verdict(ERR_BUSY, 4), int64_t(ERR_BUSY));

    NETW_CHECK_EQ(core->verdict_total(ERR_SKIP), 1);
    NETW_CHECK_EQ(core->verdict_total(ERR_BUSY), 1);
    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_BUSY, 4), false);
    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_SKIP, 4), true);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a route warns once per verdict") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_BUSY, 12), true);
    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_BUSY, 12), false);
    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_BUSY, 13), true);
    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_INVALID_DATA, 12), true);

    core->clear_verdicts();

    NETW_CHECK_EQ(core->claim_verdict_warning(ERR_BUSY, 12), true);
    NETW_CHECK_EQ(core->verdict_total(ERR_BUSY), 0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a redeclared name keeps its handle") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    const RID first = core->schema_create("pose");
    NETW_CHECK_EQ(first.is_valid(), true);
    NETW_CHECK_EQ(core->schema_create("pose") == first, true);
    NETW_CHECK_EQ(core->schema_create("other") == first, false);
    NETW_CHECK_EQ(core->schema_find("pose") == first, true);
}

TEST_CASE("[Networked][Multiplayer][Hosted] an empty schema name is refused") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->schema_create(StringName()).is_valid(), false);
    NETW_CHECK_EQ(core->schema_find("nothing").is_valid(), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] columns address in declared order") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID schema = core->schema_create("pose");

    NETW_CHECK_EQ(
        core->schema_add_column(schema, "at", SchemaCore::VECTOR3, 1),
        0
    );
    NETW_CHECK_EQ(
        core->schema_add_column(schema, "cooldowns", SchemaCore::F32, 8),
        1
    );
    NETW_CHECK_EQ(core->schema_seal(schema), OK);

    NETW_CHECK_EQ(core->schema_get_column_count(schema), 2);
    CHECK(core->schema_get_column_key(schema, 1) == StringName("cooldowns"));
    NETW_CHECK_EQ(core->schema_get_column_stride(schema, 1), 8);
    NETW_CHECK_EQ(core->schema_get_column_type(schema, 0), SchemaCore::VECTOR3);
    NETW_CHECK_ORDER(core->schema_get_hash(schema), 0, !=);
}

TEST_CASE("[Networked][Multiplayer][Hosted] two sessions share no schema") {
    Ref<NetwMultiplayerCore> first;
    first.instantiate();
    Ref<NetwMultiplayerCore> second;
    second.instantiate();

    first->schema_create("pose");

    NETW_CHECK_EQ(second->schema_find("pose").is_valid(), false);
    NETW_CHECK_EQ(
        first->get_schema_core() == second->get_schema_core(),
        false
    );
}

TEST_CASE("[Networked][Multiplayer][Hosted] an unsealed schema binds no table") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID schema = core->schema_create("pose");
    core->schema_add_column(schema, "at", SchemaCore::F32, 1);

    NETW_CHECK_EQ(core->table_create(schema).is_valid(), false);

    core->schema_seal(schema);

    NETW_CHECK_EQ(core->table_create(schema).is_valid(), true);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a rebound schema keeps its table") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID schema = core->schema_create("pose");
    core->schema_add_column(schema, "at", SchemaCore::F32, 1);
    core->schema_seal(schema);

    const RID table = core->table_create(schema);

    NETW_CHECK_EQ(core->table_create(schema) == table, true);
    NETW_CHECK_EQ(core->table_find("pose") == table, true);
    NETW_CHECK_EQ(core->table_get_schema(table) == schema, true);
    NETW_CHECK_EQ(
        core->table_get_wire_hash(table),
        core->schema_get_hash(schema)
    );
}

TEST_CASE("[Networked][Multiplayer][Hosted] a commit is stamped by the session") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID schema = core->schema_create("pose");
    core->schema_add_column(schema, "hp", SchemaCore::F32, 1);
    core->schema_seal(schema);
    const RID table = core->table_create(schema);
    core->get_clock_handle()->engine.set_tick(41);

    PackedInt64Array routes;
    routes.push_back(7);
    PackedFloat32Array hp;
    hp.push_back(3.5f);
    NETW_CHECK_EQ(core->table_write_routes(table, routes), OK);
    NETW_CHECK_EQ(core->table_write_column(table, 0, hp), OK);
    NETW_CHECK_EQ(core->table_commit(table), OK);

    NETW_CHECK_EQ(core->table_get_tick(table), 41);
    NETW_CHECK_EQ(core->table_read_routes(table).size(), 1);
    NETW_CHECK_EQ(core->table_get_row(table, 7), 0);
}

TEST_CASE("[Networked][Multiplayer][Hosted] an unbound table name is invalid") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(core->table_find("never-bound").is_valid(), false);
    NETW_CHECK_EQ(core->table_get_schema(RID()).is_valid(), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] an effect deadline is the session's") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_tick(10);

    core->effect_arm("act", Callable(), 5);
    NETW_CHECK_EQ(core->effect_pending("act"), true);
    NETW_CHECK_EQ(core->effect_count(), 1);

    core->effect_sweep(14);
    NETW_CHECK_EQ(core->effect_pending("act"), true);

    core->effect_sweep(15);
    NETW_CHECK_EQ(core->effect_pending("act"), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a zero wait takes the default") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_tick(0);

    core->effect_arm("act", Callable(), 0);

    core->effect_sweep(NetwMultiplayerCore::EFFECT_TIMEOUT_TICKS - 1);
    NETW_CHECK_EQ(core->effect_pending("act"), true);
    core->effect_sweep(NetwMultiplayerCore::EFFECT_TIMEOUT_TICKS);
    NETW_CHECK_EQ(core->effect_pending("act"), false);
}

TEST_CASE("[Networked][Multiplayer][Hosted] watching an unarmed key refuses") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    NETW_CHECK_EQ(
        core->effect_watch("never-armed", Callable(), Callable()),
        false
    );

    core->effect_arm("act", Callable(), 5);
    NETW_CHECK_EQ(core->effect_watch("act", Callable(), Callable()), true);

    core->effect_adopt("act");
    NETW_CHECK_EQ(core->effect_pending("act"), false);
    NETW_CHECK_EQ(core->effect_watch("act", Callable(), Callable()), false);
}

static Ref<RefCounted> a_seated_peer(
    const Ref<NetwMultiplayerCore> &p_core,
    int64_t p_peer
) {
    Ref<RefCounted> row;
    row.instantiate();
    p_core->participant_adopt(p_peer, row);
    return row;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P6 a seat answers as an identity: "
    "re-seating where a peer already sits announces the change exactly once, "
    "and a peer holding no row has no seat to take"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID scene = core->get_liveness_core()->entity_create();
    a_seated_peer(core, 7);

    NETW_CHECK_EQ(core->participant_seat(7).is_valid(), false);
    NETW_CHECK_EQ(core->participant_take_seat(7, scene), true);
    NETW_CHECK_EQ(core->participant_seat(7) == scene, true);

    NETW_CHECK_EQ(core->participant_take_seat(7, scene), false);

    NETW_CHECK_EQ(core->participant_take_seat(9, scene), false);
    NETW_CHECK_EQ(core->participant_seat(9).is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P7 a move reassigns a peer's seat "
    "before the scene it left can release it, so a release clears only its "
    "own seat"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID left = core->get_liveness_core()->entity_create();
    const RID arrived = core->get_liveness_core()->entity_create();
    a_seated_peer(core, 7);
    core->participant_take_seat(7, left);

    core->participant_take_seat(7, arrived);

    NETW_CHECK_EQ(core->participant_leave_seat(7, left), false);
    NETW_CHECK_EQ(core->participant_seat(7) == arrived, true);

    NETW_CHECK_EQ(core->participant_leave_seat(7, arrived), true);
    NETW_CHECK_EQ(core->participant_seat(7).is_valid(), false);
    NETW_CHECK_EQ(core->participant_leave_seat(7, arrived), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P8 the seat rides the row, so a "
    "forgotten peer leaves no seat and a new peer at the same id inherits "
    "nothing"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID scene = core->get_liveness_core()->entity_create();
    a_seated_peer(core, 7);
    a_seated_peer(core, 3);
    core->participant_take_seat(7, scene);
    core->participant_take_seat(3, scene);

    NETW_CHECK_EQ(core->participant_seated_in(scene).size(), 2);
    NETW_CHECK_EQ(core->participant_seated_in(scene)[0], 3);
    NETW_CHECK_EQ(core->participant_seated_in(scene)[1], 7);

    core->participant_forget(7);

    NETW_CHECK_EQ(core->participant_seat(7).is_valid(), false);
    a_seated_peer(core, 7);
    NETW_CHECK_EQ(core->participant_seat(7).is_valid(), false);
    NETW_CHECK_EQ(core->participant_seated_in(scene).size(), 1);

    NETW_CHECK_EQ(core->participant_seated_in(RID()).size(), 0);
}


} // namespace TestNetwMultiplayer

namespace TestNetwMultiplayerDatagram {

using namespace godot;
using netw::NetwCarrierDatagram;
using netw::NetwCarrierFrame;
using netw::NetwMultiplayerCore;

constexpr int64_t PEER = 4;


Ref<NetwMultiplayerCore> fresh_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    return core;
}

PackedByteArray body(int p_size) {
    PackedByteArray out;
    out.resize(p_size);
    return out;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D1 the bytes and the stamp come back "
    "together, and they agree"
) {
    Ref<NetwMultiplayerCore> core = fresh_core();

    const Ref<NetwCarrierDatagram> sent
        = core->frame_datagram(PEER, body(6), false);

    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(sent->bytes);
    NETW_CHECK_EQ(header->kind, NetwCarrierFrame::UNRELIABLE);
    NETW_CHECK_EQ(header->seq, sent->seq);
    NETW_CHECK_EQ(sent->bytes.size(), 9);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D2 a reliable datagram is unstamped and "
    "spends no sequence"
) {
    Ref<NetwMultiplayerCore> core = fresh_core();

    const Ref<NetwCarrierDatagram> reliable
        = core->frame_datagram(PEER, body(6), true);
    NETW_CHECK_EQ(reliable->seq, -1);
    NETW_CHECK_EQ(
        NetwCarrierFrame::read(reliable->bytes)->kind,
        NetwCarrierFrame::RELIABLE
    );

    const Ref<NetwCarrierDatagram> first
        = core->frame_datagram(PEER, body(1), false);
    Ref<NetwMultiplayerCore> untouched = fresh_core();
    const Ref<NetwCarrierDatagram> baseline
        = untouched->frame_datagram(PEER, body(1), false);
    NETW_CHECK_EQ(first->seq, baseline->seq);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D3 hearing from a peer turns the next "
    "datagram into the echo, and settles the debt"
) {
    Ref<NetwMultiplayerCore> core = fresh_core();

    core->note_inbound_seq(PEER, 21);
    NETW_CHECK_EQ(core->peers_owed_echo().size(), 1);

    const Ref<NetwCarrierDatagram> sent
        = core->frame_datagram(PEER, body(2), false);
    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(sent->bytes);
    NETW_CHECK_EQ(header->kind, NetwCarrierFrame::UNRELIABLE_ACKED);
    NETW_CHECK_EQ(header->ack, 21);

    NETW_CHECK_EQ(core->peers_owed_echo().size(), 0);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D4 the stamp advances per peer and the "
    "payload rides behind the header"
) {
    Ref<NetwMultiplayerCore> core = fresh_core();

    const Ref<NetwCarrierDatagram> a = core->frame_datagram(PEER, body(2), false);
    const Ref<NetwCarrierDatagram> b = core->frame_datagram(PEER, body(2), false);
    const Ref<NetwCarrierDatagram> other = core->frame_datagram(9, body(2), false);

    NETW_CHECK_EQ(b->seq, a->seq + 1);
    NETW_CHECK_EQ(other->seq, a->seq);

    const Ref<NetwCarrierFrame> header = NetwCarrierFrame::read(b->bytes);
    NETW_CHECK_EQ(b->bytes.size() - header->payload_offset, 2);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D5 a refusal is checked before the "
    "stamp, so a send refused for no transport or for a departed peer "
    "spends no sequence"
) {
    ERR_PRINT_OFF;
    Ref<NetwMultiplayerCore> core = fresh_core();

    NETW_CHECK_EQ(core->send_datagram(PEER, body(4), false), -1);

    Ref<SceneMultiplayer> transport;
    transport.instantiate();
    core->set_inner(transport);

    NETW_CHECK_EQ(core->send_datagram(PEER, body(4), false), -1);
    NETW_CHECK_EQ(core->send_datagram(PEER, PackedByteArray(), false), -1);

    Ref<NetwMultiplayerCore> untouched = fresh_core();
    NETW_CHECK_EQ(
        core->frame_datagram(PEER, body(1), false)->seq,
        untouched->frame_datagram(PEER, body(1), false)->seq
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D6 the datagram budget floors at 128 "
    "rather than answering a size nothing fits in, whether the transport is "
    "unset or clamps to its own minimum"
) {
    Ref<NetwMultiplayerCore> core = fresh_core();

    NETW_CHECK_EQ(core->datagram_budget(), 128);

    Ref<SceneMultiplayer> transport;
    transport.instantiate();
    core->set_inner(transport);

    transport->set_max_sync_packet_size(1400);
    NETW_CHECK_EQ(core->datagram_budget(), 1250);

    transport->set_max_sync_packet_size(128);
    NETW_CHECK_EQ(core->datagram_budget(), 128);
}

struct CoreWithPeerTransport {
    Ref<netw::LocalLoopbackSession> bus;
    Ref<SceneMultiplayer> transport;
    Ref<netw::LocalMultiplayerPeer> client;
    Ref<NetwMultiplayerCore> core;
    int64_t first = 0;
    int64_t second = 0;
};

CoreWithPeerTransport wired_core() {
    CoreWithPeerTransport out;
    out.bus.instantiate();
    out.transport.instantiate();
    out.transport->set_multiplayer_peer(out.bus->get_server_peer());
    out.client = out.bus->create_client_peer();
    const Ref<netw::LocalMultiplayerPeer> other = out.bus->create_client_peer();
    out.bus->poll();
    out.transport->poll();
    out.first = out.client->get_unique_id();
    out.second = other->get_unique_id();
    out.core = fresh_core();
    out.core->set_inner(out.transport);
    return out;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D7 the flush empties every lane and "
    "reports the sequence each peer's own datagram book rode"
) {
    CoreWithPeerTransport rig = wired_core();

    const int64_t ahead = rig.core->send_datagram(rig.first, body(2), false);
    NETW_CHECK_GE(ahead, 0);

    rig.core->carrier_append(rig.first, body(4), false);
    rig.core->carrier_append(rig.second, body(4), false);
    rig.core->carrier_append(rig.first, body(4), true);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, false), 4);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, true), 4);

    const PackedInt64Array staged = rig.core->carrier_flush();

    NETW_CHECK_EQ(staged.size(), 4);
    if (staged.size() == 4) {
        NETW_CHECK_EQ(staged[0], rig.first);
        NETW_CHECK_EQ(staged[1], ahead + 1);
        NETW_CHECK_EQ(staged[2], rig.second);
        NETW_CHECK_EQ(staged[3], ahead);
    }

    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, false), 0);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, true), 0);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.second, false), 0);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D8 an append answers the sequence of the "
    "run it displaced, not of the frame it took"
) {
    CoreWithPeerTransport rig = wired_core();
    const int64_t budget = rig.core->datagram_budget();

    NETW_CHECK_EQ(rig.core->carrier_append(rig.first, body(4), false), -1);
    NETW_CHECK_EQ(rig.core->carrier_append(rig.first, body(4), false), -1);

    const int64_t seq
        = rig.core->carrier_append(rig.first, body(int(budget)), false);
    NETW_CHECK_GE(seq, 0);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, false), budget);

    const PackedInt64Array staged = rig.core->carrier_flush();
    NETW_CHECK_EQ(staged.size(), 2);
    if (staged.size() == 2) {
        NETW_CHECK_EQ(staged[1], seq + 1);
    }
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] D9 a reliable flush stages nothing while "
    "still sending"
) {
    CoreWithPeerTransport rig = wired_core();
    const int64_t before = rig.core->get_sent_packets();

    rig.core->carrier_append(rig.first, body(6), true);
    const PackedInt64Array staged = rig.core->carrier_flush();

    NETW_CHECK_EQ(staged.size(), 0);
    NETW_CHECK_EQ(rig.core->get_sent_packets(), before + 1);
    NETW_CHECK_EQ(rig.core->carrier_pending(rig.first, true), 0);
}

} // namespace TestNetwMultiplayerDatagram

namespace TestNetwMultiplayerSessionBand {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

constexpr int EXACT_BINARY_TICKRATE = 8;
constexpr double ONE_TICK_AT_EXACT_BINARY_RATE = 0.125;

Vector<StringName> declared_tick_band() {
    return Vector<StringName>({
        "before_tick_loop",
        "before_tick",
        "on_tick",
        "after_tick",
        "after_tick_loop",
    });
}

Ref<NetwMultiplayerCore> clocked_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->get_clock_handle()->engine.set_tickrate(EXACT_BINARY_TICKRATE);
    return core;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the session announces the whole tick "
    "band in one order, carrying the clock's own advancing tick rather than "
    "a constant an announcer could invent"
) {
    Ref<NetwMultiplayerCore> core = clocked_core();
    Recorder session(core.ptr(), declared_tick_band());

    netw::ClockEngine &clock = core->get_clock_handle()->engine;
    clock.physics_step(ONE_TICK_AT_EXACT_BINARY_RATE);
    clock.physics_step(ONE_TICK_AT_EXACT_BINARY_RATE);

    Vector<StringName> twice = declared_tick_band();
    twice.append_array(declared_tick_band());
    CHECK(session.order() == twice);

    REQUIRE(session.args("on_tick", 0).size() == 2);
    NETW_CHECK_EQ(
        double(session.args("on_tick", 0)[0]),
        ONE_TICK_AT_EXACT_BINARY_RATE
    );
    NETW_CHECK_EQ(int(session.args("on_tick", 0)[1]), 0);
    REQUIRE(session.args("after_tick", 1).size() == 2);
    NETW_CHECK_EQ(int(session.args("after_tick", 1)[1]), 1);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a calibration edge reaches the session "
    "with its argument type intact, driven rather than staged"
) {
    Ref<NetwMultiplayerCore> core = clocked_core();
    netw::ClockEngine &clock = core->get_clock_handle()->engine;
    clock.set_jitter_window(4);
    clock.set_jitter_stability_threshold(0.01);
    clock.set_jitter_multiplier(2.0);
    clock.set_display_offset(1);
    Recorder session(
        core.ptr(),
        Vector<StringName>({
            "clock_synchronized",
            "display_offset_insufficient",
            "stability_changed",
        })
    );

    clock.handle_pong(0.02, 100, 0.0, true);
    clock.handle_pong(0.02, 100, 0.0, true);
    clock.handle_pong(0.2, 100, 0.0, true);

    NETW_CHECK_EQ(session.count("clock_synchronized"), 1);
    REQUIRE(session.args("display_offset_insufficient").size() == 1);
    NETW_CHECK_EQ(
        int(session.args("display_offset_insufficient")[0]),
        clock.recommended_display_offset()
    );
    REQUIRE(session.args("stability_changed").size() == 1);
    NETW_CHECK_EQ(bool(session.args("stability_changed")[0]), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] two sessions announce their own tick "
    "band, the one wiring error a single session cannot show"
) {
    Ref<NetwMultiplayerCore> first = clocked_core();
    Ref<NetwMultiplayerCore> second = clocked_core();
    Recorder watched(first.ptr(), declared_tick_band());
    Recorder quiet(second.ptr(), declared_tick_band());

    first->get_clock_handle()->engine.physics_step(
        ONE_TICK_AT_EXACT_BINARY_RATE
    );

    NETW_CHECK_EQ(watched.count("on_tick"), 1);
    NETW_CHECK_EQ(quiet.count("on_tick"), 0);
}

} // namespace TestNetwMultiplayerSessionBand

namespace TestNetwMultiplayerSessionEdges {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::Recorder;

Vector<StringName> session_edges() {
    return Vector<StringName>({
        "state_changed",
        "session_entered",
        "session_ended",
    });
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the session publishes its own bring-up "
    "and teardown: entering rides the arrival at ONLINE, ending rides the "
    "departure, and the two bracket the session rather than both landing on "
    "one edge"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), session_edges());

    core->session_plane().transition(SessionCore::STATE_CONNECTING);
    core->session_plane().transition(SessionCore::STATE_ONLINE);
    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);

    CHECK(
        session.order()
        == Vector<StringName>({
            "state_changed",
            "state_changed",
            "session_entered",
            "session_ended",
            "state_changed",
        })
    );
    REQUIRE(session.args("state_changed", 1).size() == 2);
    NETW_CHECK_EQ(
        int(session.args("state_changed", 1)[0]),
        int(SessionCore::STATE_CONNECTING)
    );
    NETW_CHECK_EQ(
        int(session.args("state_changed", 1)[1]),
        int(SessionCore::STATE_ONLINE)
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a transition the machine refuses, "
    "OFFLINE straight to ONLINE with no connect between, publishes nothing"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), session_edges());

    core->session_plane().transition(SessionCore::STATE_ONLINE);

    NETW_CHECK_EQ(session.order().size(), 0);
    NETW_CHECK_EQ(
        int(core->session_plane().get_state()),
        int(SessionCore::STATE_OFFLINE)
    );
}

} // namespace TestNetwMultiplayerSessionEdges

namespace TestNetwMultiplayerAuthEdges {

using namespace godot;
using netw::EventPlane;
using netw::NetwMultiplayerCore;
using netw_test::EventRing;
using netw_test::Recorder;

Vector<StringName> auth_edges() {
    return Vector<StringName>({
        "peer_authenticating",
        "peer_authentication_failed",
    });
}

struct Transported {
    Ref<NetwMultiplayerCore> core;
    Ref<SceneMultiplayer> inner;
};

Transported transported_core() {
    Transported rig;
    rig.core.instantiate();
    rig.inner.instantiate();
    rig.core->set_inner(rig.inner);
    return rig;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the session republishes its transport's "
    "handshake"
) {
    Transported rig = transported_core();
    Recorder session(rig.core.ptr(), auth_edges());

    rig.inner->emit_signal("peer_authenticating", 7);
    rig.inner->emit_signal("peer_authentication_failed", 7);

    CHECK(
        session.order()
        == Vector<StringName>({
            "peer_authenticating",
            "peer_authentication_failed",
        })
    );
    REQUIRE(session.args("peer_authentication_failed").size() == 1);
    NETW_CHECK_EQ(int(session.args("peer_authentication_failed")[0]), 7);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a refused peer is recorded as well as "
    "announced"
) {
    Transported rig = transported_core();
    rig.core->event_arm(true);
    rig.core->event_watch(
        PackedInt64Array({EventPlane::PEER_AUTH_FAILED}),
        Dictionary(),
        Dictionary(),
        Callable(),
        Dictionary()
    );

    rig.inner->emit_signal("peer_authentication_failed", 7);

    const EventRing ring(rig.core->event_ring(0));
    REQUIRE(ring.size() == 1);
    NETW_CHECK_EQ(ring.event_at(0), int64_t(EventPlane::PEER_AUTH_FAILED));
    NETW_CHECK_EQ(ring.at(0)->peer, int64_t(7));
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a replaced transport goes quiet, "
    "because the handshake belongs to whichever transport the session is "
    "holding now"
) {
    Transported rig = transported_core();
    Ref<SceneMultiplayer> replacement;
    replacement.instantiate();
    Recorder session(rig.core.ptr(), auth_edges());

    rig.core->set_inner(replacement);
    rig.inner->emit_signal("peer_authenticating", 7);
    replacement->emit_signal("peer_authenticating", 9);

    NETW_CHECK_EQ(session.count("peer_authenticating"), 1);
    REQUIRE(session.args("peer_authenticating").size() == 1);
    NETW_CHECK_EQ(int(session.args("peer_authenticating")[0]), 9);
}

} // namespace TestNetwMultiplayerAuthEdges

namespace TestNetwMultiplayerGateDefaults {

using namespace godot;
using netw::NetwMultiplayerCore;

constexpr int64_t SERVER = 1;
constexpr int64_t CLIENT = 4;
constexpr int64_t PRE_ADMIT_ROUTE = 0;

PackedByteArray body(int p_size) {
    PackedByteArray bytes;
    bytes.resize(p_size);
    return bytes;
}

struct GateIds {
    int64_t spawn = -1;
    int64_t reparent = -1;
    int64_t table = -1;
    int64_t sync = -1;
};

int64_t declared_id(
    const netw::wire::WireRegistry &p_registry,
    const char *p_name
) {
    const netw::wire::ChannelDecl *decl
        = p_registry.find_channel_by_name(StringName(p_name));
    return decl ? int64_t(decl->id) : -1;
}

GateIds gate_ids() {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    GateIds ids;
    ids.spawn = declared_id(registry, "SPAWN");
    ids.reparent = declared_id(registry, "REPARENT");
    ids.table = declared_id(registry, "TABLE");
    ids.sync = declared_id(registry, "SYNC");
    REQUIRE(ids.spawn > 0);
    REQUIRE(ids.reparent > 0);
    REQUIRE(ids.table > 0);
    REQUIRE(ids.sync > 0);
    return ids;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the spawn gate admits only the "
    "server's own channels; the sender is judged before anything else, so a "
    "client is refused for who it is even on a channel this gate would "
    "never admit"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const GateIds ids = gate_ids();

    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            SERVER,
            PRE_ADMIT_ROUTE,
            ids.spawn,
            body(4)
        ),
        OK
    );
    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            SERVER,
            PRE_ADMIT_ROUTE,
            ids.reparent,
            body(4)
        ),
        OK
    );
    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            CLIENT,
            PRE_ADMIT_ROUTE,
            ids.spawn,
            body(4)
        ),
        ERR_UNAUTHORIZED
    );
    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            CLIENT,
            PRE_ADMIT_ROUTE,
            ids.sync,
            body(0)
        ),
        ERR_UNAUTHORIZED
    );
    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            SERVER,
            PRE_ADMIT_ROUTE,
            ids.sync,
            body(4)
        ),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        core->spawn_admit_frame_default(
            SERVER,
            PRE_ADMIT_ROUTE,
            ids.spawn,
            body(0)
        ),
        ERR_INVALID_DATA
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the table gate counts the sender it "
    "refuses"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const GateIds ids = gate_ids();

    NETW_CHECK_EQ(
        core->table_admit_frame_default(CLIENT, ids.table, body(4)),
        ERR_UNAUTHORIZED
    );

    const Dictionary counters = core->get_table_core()->counters();
    NETW_CHECK_EQ(int64_t(counters["drops_table_bad_sender"]), 1);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the table gate refuses a frame by "
    "shape, including a truncated header that names no table to look for"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const GateIds ids = gate_ids();

    NETW_CHECK_EQ(
        core->table_admit_frame_default(SERVER, ids.sync, body(4)),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        core->table_admit_frame_default(SERVER, ids.table, body(0)),
        ERR_INVALID_DATA
    );
    NETW_CHECK_EQ(
        core->table_admit_frame_default(SERVER, ids.table, body(1)),
        ERR_INVALID_DATA
    );
}

} // namespace TestNetwMultiplayerGateDefaults

namespace TestNetwMultiplayerPumpEdges {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SchemaCore;
using netw_test::Recorder;

constexpr int64_t SECOND = 1000000;

TEST_CASE(
    "[Networked][Multiplayer][Hosted] marking a poll announces it with the "
    "gap since the last mark, zero on the first poll"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"poll_started"}));

    core->poll_delta(9000);
    core->poll_delta(9000 + SECOND / 2);

    NETW_CHECK_EQ(session.count("poll_started"), 2);
    REQUIRE(session.args("poll_started", 0).size() == 1);
    NETW_CHECK_EQ(double(session.args("poll_started", 0)[0]), 0.0);
    REQUIRE(session.args("poll_started", 1).size() == 1);
    NETW_CHECK_CLOSE(double(session.args("poll_started", 1)[0]), 0.5, 1e-9);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a second reader marked no poll") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"poll_started"}));

    core->poll_delta(9000);
    const double again = core->poll_delta(9000);

    NETW_CHECK_EQ(again, 0.0);
    NETW_CHECK_EQ(session.count("poll_started"), 1);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] an intake announces what it touched, "
    "not a table it only committed and published locally, and reopens"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID schema = core->schema_create("Row");
    core->schema_add_column(schema, "value", int(Variant::INT), 0);
    core->schema_seal(schema);
    const RID table = core->table_create(schema);
    Recorder session(core.ptr(), Vector<StringName>({"table_received"}));

    PackedInt64Array routes;
    routes.push_back(7);
    core->table_write_routes(table, routes);
    PackedInt64Array values;
    values.push_back(3);
    core->table_write_column(table, 0, values);
    core->table_commit(table);

    core->table_publish(table);

    core->table_publish_intake();

    NETW_CHECK_EQ(session.count("table_received"), 1);
    REQUIRE(session.args("table_received").size() == 2);
    CHECK(RID(session.args("table_received")[0]) == table);
    NETW_CHECK_EQ(
        int64_t(session.args("table_received")[1]),
        core->table_get_tick(table)
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a packet that is not ours is announced, "
    "not counted"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"peer_packet"}));
    PackedByteArray foreign;
    foreign.push_back(0x01);
    foreign.push_back(0x02);

    const Ref<netw::NetwCarrierFrame> header
        = core->receive_header(4, foreign);

    REQUIRE(header.is_valid());
    NETW_CHECK_EQ(header->kind, int64_t(netw::NetwCarrierFrame::FOREIGN));
    NETW_CHECK_EQ(session.count("peer_packet"), 1);
    REQUIRE(session.args("peer_packet").size() == 2);
    NETW_CHECK_EQ(int(session.args("peer_packet")[0]), 4);
    NETW_CHECK_EQ(core->get_received_packets(), 0);
    NETW_CHECK_EQ(core->get_received_bytes(), 0);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a malformed datagram is ours and is "
    "neither announced nor counted"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"peer_packet"}));
    PackedByteArray truncated;
    truncated.push_back(netw::NetwCarrierFrame::MAGIC_UNRELIABLE);

    const Ref<netw::NetwCarrierFrame> header
        = core->receive_header(4, truncated);

    REQUIRE(header.is_valid());
    NETW_CHECK_EQ(header->kind, int64_t(netw::NetwCarrierFrame::MALFORMED));
    NETW_CHECK_EQ(session.count("peer_packet"), 0);
    NETW_CHECK_EQ(core->get_received_packets(), 0);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] our own datagram counts its body, not "
    "the wire size, so two peers' throughput is comparable"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"peer_packet"}));
    PackedByteArray body;
    body.resize(8);
    const PackedByteArray sent
        = netw::NetwCarrierFrame::build(body, true, 0, -1);

    const Ref<netw::NetwCarrierFrame> header = core->receive_header(4, sent);

    REQUIRE(header.is_valid());
    NETW_CHECK_EQ(header->kind, int64_t(netw::NetwCarrierFrame::RELIABLE));
    NETW_CHECK_EQ(session.count("peer_packet"), 0);
    NETW_CHECK_EQ(core->get_received_packets(), 1);
    NETW_CHECK_EQ(core->get_received_bytes(), 8);
}

} // namespace TestNetwMultiplayerPumpEdges

namespace TestNetwMultiplayerControlBand {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

constexpr int64_t SERVER = 1;
constexpr int64_t CLIENT = 4;

Vector<StringName> control_band() {
    return Vector<StringName>({
        "kicked",
        "server_disconnecting",
        "kick_requested",
        "disconnect_requested",
    });
}

int64_t control_channel(const char *p_name) {
    const netw::wire::WireRegistry registry
        = netw::wire::WireRegistry::create_default();
    const netw::wire::ChannelDecl *decl
        = registry.find_channel_by_name(StringName(p_name));
    REQUIRE(decl != nullptr);
    return int64_t(decl->id);
}

Ref<NetwMultiplayerCore> session_at_peer_one() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    return core;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a notice channel is consumed whether "
    "the server's is honored or a client's is refused, so a caller never "
    "falls through and routes it elsewhere, but only the server ever "
    "speaks for the session"
) {
    Ref<NetwMultiplayerCore> core = session_at_peer_one();
    Recorder session(core.ptr(), control_band());
    const int64_t kicked = control_channel("SESSION_KICKED");
    const int64_t shutdown = control_channel("SESSION_SHUTDOWN");

    NETW_CHECK_EQ(
        core->session_publish_control(
            kicked,
            SERVER,
            netw::gd::var_to_bytes("rude")
        ),
        true
    );
    NETW_CHECK_EQ(
        core->session_publish_control(
            shutdown,
            CLIENT,
            netw::gd::var_to_bytes("bye")
        ),
        true
    );

    CHECK(session.order() == Vector<StringName>({"kicked"}));
    REQUIRE(session.args("kicked").size() == 1);
    CHECK(String(session.args("kicked")[0]) == String("rude"));
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a kick request names its target from "
    "the payload pair and drops a payload that is not one, and the "
    "requester it announces is the frame's own sender, never anything the "
    "payload claims"
) {
    Ref<NetwMultiplayerCore> core = session_at_peer_one();
    Recorder session(core.ptr(), control_band());
    const int64_t request = control_channel("SESSION_KICK_REQUEST");

    Array pair;
    pair.push_back(9);
    pair.push_back("afk");
    core->session_publish_control(
        request,
        CLIENT,
        netw::gd::var_to_bytes(pair)
    );
    core->session_publish_control(
        request,
        CLIENT,
        netw::gd::var_to_bytes("afk")
    );

    NETW_CHECK_EQ(session.count("kick_requested"), 1);
    REQUIRE(session.args("kick_requested").size() == 3);
    NETW_CHECK_EQ(int(session.args("kick_requested")[0]), int(CLIENT));
    NETW_CHECK_EQ(int(session.args("kick_requested")[1]), 9);
    CHECK(String(session.args("kick_requested")[2]) == String("afk"));
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a pause is announced once as the "
    "edge, not applied, so every reactor including this addon reads the "
    "same announcement, and a client's unpause is not an edge at all"
) {
    Ref<NetwMultiplayerCore> core = session_at_peer_one();
    Recorder session(
        core.ptr(),
        Vector<StringName>({"tree_paused", "tree_unpaused"})
    );
    const int64_t pause = control_channel("SESSION_PAUSE");
    const int64_t unpause = control_channel("SESSION_UNPAUSE");

    core->session_publish_control(
        pause,
        SERVER,
        netw::gd::var_to_bytes("intermission")
    );
    core->session_publish_control(unpause, CLIENT, PackedByteArray());
    core->session_publish_control(unpause, SERVER, PackedByteArray());

    CHECK(
        session.order() == Vector<StringName>({"tree_paused", "tree_unpaused"})
    );
    REQUIRE(session.args("tree_paused").size() == 1);
    CHECK(String(session.args("tree_paused")[0]) == String("intermission"));
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a channel this band does not own is not "
    "consumed"
) {
    Ref<NetwMultiplayerCore> core = session_at_peer_one();
    Recorder session(core.ptr(), control_band());

    NETW_CHECK_EQ(
        core->session_publish_control(
            control_channel("SESSION_JOIN"),
            SERVER,
            PackedByteArray()
        ),
        false
    );

    NETW_CHECK_EQ(session.order().size(), 0);
}

} // namespace TestNetwMultiplayerControlBand

namespace TestNetwMultiplayerServices {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

Vector<StringName> service_edges() {
    return Vector<StringName>({"service_registered", "service_unregistered"});
}

Ref<Resource> an_identity_only_type() {
    Ref<Resource> type;
    type.instantiate();
    return type;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] registering is announcing, and "
    "re-registering the identical instance is inert"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), service_edges());
    const Ref<Resource> type = an_identity_only_type();
    Node *service = memnew(Node);

    core->service_register(type.ptr(), service);
    core->service_register(type.ptr(), service);

    NETW_CHECK_EQ(session.count("service_registered"), 1);
    NETW_CHECK_EQ(core->service_of(type.ptr()), service);

    core->service_unregister(type.ptr(), service);

    CHECK(
        session.order()
        == Vector<StringName>({"service_registered", "service_unregistered"})
    );
    NETW_CHECK_EQ(core->service_of(type.ptr()), nullptr);
    memdelete(service);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a replaced service cannot be "
    "unregistered by the one it replaced, because the key names the "
    "replacement now and honoring a stale unregister would drop the live "
    "service and announce the wrong one gone"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const Ref<Resource> type = an_identity_only_type();
    Node *first = memnew(Node);
    Node *second = memnew(Node);
    core->service_register(type.ptr(), first);
    core->service_register(type.ptr(), second);
    Recorder session(core.ptr(), service_edges());

    core->service_unregister(type.ptr(), first);

    NETW_CHECK_EQ(session.count("service_unregistered"), 0);
    NETW_CHECK_EQ(core->service_of(type.ptr()), second);
    memdelete(first);
    memdelete(second);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a freed service reads as absent, "
    "because the book holds an id rather than a live reference"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const Ref<Resource> type = an_identity_only_type();
    Node *service = memnew(Node);
    core->service_register(type.ptr(), service);

    memdelete(service);

    NETW_CHECK_EQ(core->service_of(type.ptr()), nullptr);
}

} // namespace TestNetwMultiplayerServices

namespace TestNetwMultiplayerWrapperIndex {

using namespace godot;
using netw::NetwLivenessCore;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

Vector<StringName> entity_edges() {
    return Vector<StringName>({
        "entity_live",
        "entity_lingering",
        "entity_dead",
    });
}

Ref<RefCounted> an_identity_only_wrapper() {
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    return wrapper;
}

Ref<netw::NetwEntityRecord> a_record_holding_peer_and_route(
    int64_t p_peer,
    int64_t p_route = 0
) {
    Ref<netw::NetwEntityRecord> record;
    record.instantiate();
    record->set_peer_id(p_peer);
    record->set_route(p_route);
    return record;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the index is what holds a wrapper, so "
    "letting go of the only other reference does not free it and does not "
    "leave a dead object for the rest of the route's life"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID entity = core->get_liveness_core()->entity_create();
    const int64_t route = core->get_liveness_core()->reserve_route();
    Ref<RefCounted> wrapper = an_identity_only_wrapper();
    RefCounted *raw = wrapper.ptr();
    core->liveness_bind(
        entity,
        route,
        wrapper,
        a_record_holding_peer_and_route(0),
        nullptr
    );

    wrapper.unref();

    NETW_CHECK_EQ(core->wrapper_of(entity).ptr(), raw);
    NETW_CHECK_EQ(core->wrapper_for_route(route).ptr(), raw);
}

TEST_CASE("[Networked][Multiplayer][Hosted] a route's life is three edges") {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID entity = core->get_liveness_core()->entity_create();
    const int64_t route = core->get_liveness_core()->reserve_route();
    Recorder session(core.ptr(), entity_edges());

    NETW_CHECK_EQ(
        core->liveness_bind(
            entity,
            route,
            an_identity_only_wrapper(),
            a_record_holding_peer_and_route(0),
            nullptr
        ),
        true
    );
    core->liveness_publish_live(route);
    core->liveness_linger(entity);
    core->liveness_retire(route);

    CHECK(
        session.order()
        == Vector<StringName>({
            "entity_live",
            "entity_lingering",
            "entity_dead",
        })
    );
    REQUIRE(session.args("entity_live").size() == 2);
    NETW_CHECK_EQ(int64_t(session.args("entity_live")[0]), route);
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("entity_live")[1]),
        core->wrapper_for_id(entity.get_id()).ptr()
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a retired wrapper outlasts its route, "
    "waiting in the retired book rather than being dropped, because its "
    "last act is a hide the delta names only in the NEXT cycle"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID entity = core->get_liveness_core()->entity_create();
    const int64_t route = core->get_liveness_core()->reserve_route();
    Ref<RefCounted> wrapper = an_identity_only_wrapper();
    RefCounted *raw = wrapper.ptr();
    core->liveness_bind(
        entity,
        route,
        wrapper,
        a_record_holding_peer_and_route(0),
        nullptr
    );
    wrapper.unref();

    core->liveness_retire(route);

    NETW_CHECK_EQ(core->wrapper_of(entity).is_valid(), false);
    NETW_CHECK_EQ(core->wrapper_for_id(entity.get_id()).ptr(), raw);
    NETW_CHECK_EQ(
        int(core->get_liveness_core()->state_of(entity)),
        int(NetwLivenessCore::STATE_DEAD)
    );

    core->wrapper_sweep_retired();

    NETW_CHECK_EQ(core->wrapper_for_id(entity.get_id()).is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a refused bind announces nothing, "
    "because a route names one handle for the whole session and a second "
    "wrapper arriving for a live route is refused rather than allowed to "
    "rename it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const RID first = core->get_liveness_core()->entity_create();
    const RID second = core->get_liveness_core()->entity_create();
    const int64_t route = core->get_liveness_core()->reserve_route();
    core->liveness_bind(
        first,
        route,
        an_identity_only_wrapper(),
        a_record_holding_peer_and_route(0),
        nullptr
    );
    Recorder session(core.ptr(), entity_edges());

    NETW_CHECK_EQ(
        core->liveness_bind(
            second,
            route,
            an_identity_only_wrapper(),
            a_record_holding_peer_and_route(0),
            nullptr
        ),
        false
    );

    NETW_CHECK_EQ(session.order().size(), 0);
    NETW_CHECK_EQ(core->wrapper_of(second).is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the local player is the route that "
    "represents this peer: a route the local peer does not own changes "
    "nothing, and once the owning route retires it represents nobody until "
    "another route says otherwise"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    const int64_t mine = core->get_unique_id();
    Recorder session(core.ptr(), Vector<StringName>({"local_player_changed"}));

    const RID theirs = core->get_liveness_core()->entity_create();
    const int64_t their_route = core->get_liveness_core()->reserve_route();
    core->liveness_bind(
        theirs,
        their_route,
        an_identity_only_wrapper(),
        a_record_holding_peer_and_route(mine + 1, their_route),
        nullptr
    );
    core->liveness_publish_live(their_route);
    core->liveness_settle_local_player(their_route);

    NETW_CHECK_EQ(session.count("local_player_changed"), 0);
    NETW_CHECK_EQ(core->get_local_player().is_valid(), false);

    const RID ours = core->get_liveness_core()->entity_create();
    const int64_t our_route = core->get_liveness_core()->reserve_route();
    const Ref<RefCounted> wrapper = an_identity_only_wrapper();
    core->liveness_bind(
        ours,
        our_route,
        wrapper,
        a_record_holding_peer_and_route(mine, our_route),
        nullptr
    );
    core->liveness_publish_live(our_route);
    core->liveness_settle_local_player(our_route);

    NETW_CHECK_EQ(session.count("local_player_changed"), 1);
    NETW_CHECK_EQ(core->get_local_player().ptr(), wrapper.ptr());

    core->liveness_retire(our_route);

    NETW_CHECK_EQ(session.count("local_player_changed"), 2);
    NETW_CHECK_EQ(core->get_local_player().is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a peerless session represents "
    "nobody, even for a record carrying peer 1, the unique id an offline "
    "session answers, which would look local to a check that skipped the "
    "transport"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Recorder session(core.ptr(), Vector<StringName>({"local_player_changed"}));
    const RID entity = core->get_liveness_core()->entity_create();
    const int64_t route = core->get_liveness_core()->reserve_route();

    core->liveness_bind(
        entity,
        route,
        an_identity_only_wrapper(),
        a_record_holding_peer_and_route(1, route),
        nullptr
    );
    core->liveness_publish_live(route);
    core->liveness_settle_local_player(route);

    NETW_CHECK_EQ(core->has_multiplayer_peer(), false);
    NETW_CHECK_EQ(session.count("local_player_changed"), 0);
    NETW_CHECK_EQ(core->get_local_player().is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a replacement outlives the entity it "
    "replaced: a scene change binds the replacement BEFORE retiring the "
    "original, so the entity that died is not asked by identity for "
    "whether it represents this peer, which would answer yes for every "
    "entity that has one and drop a local player that had already moved on"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    const int64_t mine = core->get_unique_id();

    const RID before = core->get_liveness_core()->entity_create();
    const int64_t before_route = core->get_liveness_core()->reserve_route();
    core->liveness_bind(
        before,
        before_route,
        an_identity_only_wrapper(),
        a_record_holding_peer_and_route(mine, before_route),
        nullptr
    );
    core->liveness_publish_live(before_route);
    core->liveness_settle_local_player(before_route);

    const RID after = core->get_liveness_core()->entity_create();
    const int64_t after_route = core->get_liveness_core()->reserve_route();
    const Ref<RefCounted> replacement = an_identity_only_wrapper();
    core->liveness_bind(
        after,
        after_route,
        replacement,
        a_record_holding_peer_and_route(mine, after_route),
        nullptr
    );
    core->liveness_publish_live(after_route);
    core->liveness_settle_local_player(after_route);
    Recorder session(core.ptr(), Vector<StringName>({"local_player_changed"}));

    core->liveness_retire(before_route);

    NETW_CHECK_EQ(session.count("local_player_changed"), 0);
    NETW_CHECK_EQ(core->get_local_player().ptr(), replacement.ptr());
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a route names one entity, and a "
    "wrapper arriving on a standing one is refused; the bind is what "
    "stamps the route onto the record, so a caller never reads a route "
    "the plane refused to give"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const int64_t route = core->get_liveness_core()->reserve_route();
    Ref<RefCounted> first = an_identity_only_wrapper();
    Ref<netw::NetwEntityRecord> held = a_record_holding_peer_and_route(0);
    core->get_liveness_core()->adopt(held->get_handle());

    NETW_CHECK_EQ(
        core->liveness_adopt_route(route, first, held, nullptr),
        OK
    );

    NETW_CHECK_EQ(held->get_route(), route);
    NETW_CHECK_EQ(core->wrapper_for_route(route).ptr(), first.ptr());

    SUBCASE("the wrapper already standing owes no second announcement") {
        NETW_CHECK_EQ(
            core->liveness_adopt_route(route, first, held, nullptr),
            ERR_ALREADY_EXISTS
        );
    }

    SUBCASE("a second wrapper is refused the route, which keeps its own") {
        Ref<RefCounted> second = an_identity_only_wrapper();
        Ref<netw::NetwEntityRecord> arriving
            = a_record_holding_peer_and_route(0);
        NETW_CHECK_EQ(
            core->liveness_adopt_route(route, second, arriving, nullptr),
            ERR_UNAVAILABLE
        );
        NETW_CHECK_EQ(core->wrapper_for_route(route).ptr(), first.ptr());
        NETW_CHECK_EQ(arriving->get_route(), 0);
    }

    SUBCASE("a route nobody named is not a route") {
        NETW_CHECK_EQ(
            core->liveness_adopt_route(0, first, held, nullptr),
            ERR_INVALID_PARAMETER
        );
    }
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a re-admission adopts the record its "
    "route already stands for: the receive path builds a fresh wrapper "
    "with no handle this session knows and arrives on the tombstoned "
    "route, and one route holds one record across every life it has, so "
    "the arriving record takes the standing handle rather than minting a "
    "second name for it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const int64_t route = core->get_liveness_core()->reserve_route();
    Ref<RefCounted> died = an_identity_only_wrapper();
    Ref<netw::NetwEntityRecord> record = a_record_holding_peer_and_route(0);
    const RID standing = record->get_handle();
    core->get_liveness_core()->adopt(standing);
    REQUIRE(core->liveness_adopt_route(route, died, record, nullptr) == OK);
    core->liveness_retire(route);
    REQUIRE(
        core->get_liveness_core()->state_of(standing)
        == NetwLivenessCore::STATE_DEAD
    );
    const int before = core->get_liveness_core()->epoch_of(standing);

    Ref<RefCounted> revived = an_identity_only_wrapper();
    Ref<netw::NetwEntityRecord> arriving = a_record_holding_peer_and_route(0);
    NETW_CHECK_EQ(
        core->liveness_adopt_route(route, revived, arriving, nullptr),
        OK
    );

    CHECK(arriving->get_handle() == standing);
    NETW_CHECK_EQ(
        core->get_liveness_core()->state_of(standing),
        NetwLivenessCore::STATE_LIVE
    );
    NETW_CHECK_EQ(core->get_liveness_core()->epoch_of(standing), before + 1);
    NETW_CHECK_EQ(core->wrapper_for_route(route).ptr(), revived.ptr());

    SUBCASE(
        "a wrapper this session already knows is refused the record, "
        "because it carries an identity this plane already holds and "
        "taking the dead route's record would merge two entities into one "
        "name"
    ) {
        core->liveness_retire(route);
        Ref<RefCounted> known = an_identity_only_wrapper();
        Ref<netw::NetwEntityRecord> mine = a_record_holding_peer_and_route(0);
        const int64_t other = core->get_liveness_core()->reserve_route();
        core->get_liveness_core()->adopt(mine->get_handle());
        REQUIRE(
            core->liveness_adopt_route(other, known, mine, nullptr) == OK
        );

        NETW_CHECK_EQ(
            core->liveness_adopt_route(route, known, mine, nullptr),
            ERR_UNAVAILABLE
        );
        NETW_CHECK_EQ(mine->get_route(), other);
    }
}

} // namespace TestNetwMultiplayerWrapperIndex

namespace TestNetwMultiplayerParticipants {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

Vector<StringName> join_edges() {
    return Vector<StringName>({
        "local_participant_joined",
        "participant_joined",
    });
}

Ref<RefCounted> a_row() {
    Ref<RefCounted> row;
    row.instantiate();
    return row;
}

Ref<NetwMultiplayerCore> peered_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] one peer keeps one roster row, so two "
    "callers reading it never end up with two different objects for one "
    "participant"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const Ref<RefCounted> first = a_row();

    core->participant_adopt(7, first);
    core->participant_adopt(7, a_row());

    NETW_CHECK_EQ(core->participant_of(7).ptr(), first.ptr());
    NETW_CHECK_EQ(core->participant_has(7), true);

    core->participant_forget(7);

    NETW_CHECK_EQ(core->participant_has(7), false);
    NETW_CHECK_EQ(core->participant_of(7).is_valid(), false);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] joining announces local first, so a "
    "listener that handles both sees itself arrive before it sees the "
    "roster grow, and a peer that is not this one is never announced local"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const int64_t mine = core->get_unique_id();
    core->participant_adopt(mine, a_row());
    core->participant_adopt(mine + 1, a_row());
    Recorder session(core.ptr(), join_edges());

    core->participant_publish_joined(mine + 1);
    core->participant_publish_joined(mine);

    CHECK(
        session.order()
        == Vector<StringName>({
            "participant_joined",
            "local_participant_joined",
            "participant_joined",
        })
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] an unknown peer announces nothing, "
    "because it has not been admitted and announcing it would put a null "
    "participant in front of every listener"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    Recorder session(core.ptr(), join_edges());

    core->participant_publish_joined(9);

    NETW_CHECK_EQ(session.order().size(), 0);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] rows answer in peer order, so two "
    "reads in one frame agree rather than following whatever order a hash "
    "happened to hold"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const Ref<RefCounted> low = a_row();
    const Ref<RefCounted> high = a_row();

    core->participant_adopt(9, high);
    core->participant_adopt(2, low);

    const TypedArray<Object> all = core->participant_all();
    REQUIRE(all.size() == 2);
    NETW_CHECK_EQ(Object::cast_to<Object>(all[0]), low.ptr());
    NETW_CHECK_EQ(Object::cast_to<Object>(all[1]), high.ptr());

    core->participant_clear();

    NETW_CHECK_EQ(core->participant_all().size(), 0);
}

Ref<RefCounted> a_row_with_instance_scene_changed_signal() {
    Ref<RefCounted> row;
    row.instantiate();
    Array args;
    Dictionary from;
    from["name"] = "from";
    from["type"] = int(Variant::OBJECT);
    Dictionary to;
    to["name"] = "to";
    to["type"] = int(Variant::OBJECT);
    args.push_back(from);
    args.push_back(to);
    netw::gd::add_user_signal(row.ptr(), "scene_changed", args);
    return row;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] only the local participant's scene "
    "change is the session's, never another peer's, and the published "
    "edge names WHICH participant moved because each row names itself as "
    "the scene it moved to"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const int64_t mine = core->get_unique_id();
    const Ref<RefCounted> ours = a_row_with_instance_scene_changed_signal();
    const Ref<RefCounted> theirs = a_row_with_instance_scene_changed_signal();
    core->participant_adopt(mine, ours);
    core->participant_adopt(mine + 1, theirs);
    core->participant_publish_joined(mine);
    core->participant_publish_joined(mine + 1);
    Recorder session(
        core.ptr(),
        Vector<StringName>({"local_scene_changed"})
    );

    theirs->emit_signal("scene_changed", Variant(), theirs);
    ours->emit_signal("scene_changed", Variant(), ours);

    NETW_CHECK_EQ(session.count("local_scene_changed"), 1);
    REQUIRE(session.args("local_scene_changed").size() == 2);
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("local_scene_changed")[1]),
        ours.ptr()
    );
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a forgotten participant stops "
    "speaking for the session, because a row the roster dropped is no "
    "longer this peer"
) {
    Ref<NetwMultiplayerCore> core = peered_core();
    const int64_t mine = core->get_unique_id();
    const Ref<RefCounted> ours = a_row_with_instance_scene_changed_signal();
    core->participant_adopt(mine, ours);
    core->participant_publish_joined(mine);
    Recorder session(
        core.ptr(),
        Vector<StringName>({"local_scene_changed"})
    );

    core->participant_forget(mine);
    ours->emit_signal("scene_changed", Variant(), ours);

    NETW_CHECK_EQ(session.count("local_scene_changed"), 0);
}

} // namespace TestNetwMultiplayerParticipants

namespace TestNetwMultiplayerEntityTree {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw_test::Recorder;

constexpr const char *ENTITY_ROOT_META_KEY = "netw_entity";

struct Bound {
    Ref<NetwMultiplayerCore> core;
    Ref<RefCounted> wrapper;
    Ref<netw::NetwEntityRecord> record;
    RID handle;
    Node *owner = nullptr;
};

Bound bind_under(
    const Ref<NetwMultiplayerCore> &p_core,
    Node *p_parent,
    bool p_marked,
    bool p_declares_scene = false,
    int64_t p_peer_id = 0
) {
    Bound out;
    out.core = p_core;
    out.owner = memnew(Node);
    if (p_parent != nullptr) {
        p_parent->add_child(out.owner);
    }
    out.wrapper.instantiate();
    out.handle = p_core->get_liveness_core()->entity_create();
    out.record.instantiate();
    out.record->adopt_handle(out.handle);
    out.record->set_declares_scene(p_declares_scene);
    out.record->set_peer_id(p_peer_id);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    p_core->liveness_bind(out.handle, route, out.wrapper, out.record, out.owner);
    if (p_marked) {
        out.owner->set_meta(ENTITY_ROOT_META_KEY, out.wrapper);
    }
    return out;
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] the nearest marked ancestor is the "
    "parent entity, passing through an unmarked node in between rather "
    "than stopping there, and a node's own mark is not its parent's, so a "
    "root answers invalid rather than answering itself"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound grandparent = bind_under(core, root, true);
    Node *plain = memnew(Node);
    grandparent.owner->add_child(plain);
    const Bound child = bind_under(core, plain, true);

    CHECK(core->entity_parent_of(child.handle) == grandparent.handle);
    NETW_CHECK_EQ(core->entity_parent_of(grandparent.handle).is_valid(), false);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a handle answers its own wrapper, and "
    "a wrapper this session never adopted has no handle here, which is "
    "what separates another session's entity from one of ours"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound bound = bind_under(core, root, true);

    CHECK(core->handle_of_wrapper(bound.wrapper.ptr()) == bound.handle);
    NETW_CHECK_EQ(core->wrapper_owner(bound.handle), bound.owner);
    Ref<RefCounted> stranger;
    stranger.instantiate();
    NETW_CHECK_EQ(core->handle_of_wrapper(stranger.ptr()).is_valid(), false);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a path addresses or it is empty: a "
    "node addresses itself as \".\", so a property on the base is a valid "
    "self-address; a base or a source that is gone addresses nothing "
    "rather than falling through to a bare \":position\" that writes "
    "whichever node the reader happened to be holding"
) {
    Node *root = memnew(Node);
    Node *child = memnew(Node);
    child->set_name("Body");
    root->add_child(child);
    Node *orphan = memnew(Node);

    CHECK(String(NetwMultiplayerCore::relative_path(root, child))
          == String("Body"));
    CHECK(String(NetwMultiplayerCore::property_path(child, "position", root))
          == String("Body:position"));
    CHECK(String(NetwMultiplayerCore::property_path(root, "position", root))
          == String(".:position"));
    NETW_CHECK_EQ(
        NetwMultiplayerCore::property_path(child, "position", nullptr)
            .is_empty(),
        true
    );
    NETW_CHECK_EQ(
        NetwMultiplayerCore::property_path(nullptr, "position", root)
            .is_empty(),
        true
    );
    NETW_CHECK_EQ(
        NetwMultiplayerCore::relative_path(nullptr, child).is_empty(),
        true
    );

    memdelete(orphan);
    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a live route announces the container "
    "it was named with, because the caller's own resolution is the one "
    "answer and a second walk here could disagree and publish a handle "
    "nobody holds; the facet an entity belongs to is its scene's, and a "
    "scene's own facet is itself, both resolved through that same walk"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound container = bind_under(core, root, true, true);
    const Bound inside = bind_under(core, container.owner, true, false);
    netw_test::EntityFactories factories;
    netw_test::CallLog facets;
    netw::NetwEntityRecord::set_part_factory(
        netw::NetwEntityRecord::PART_SCENE,
        facets.minting("scene")
    );
    const Ref<RefCounted> container_handle = core->scene_handle_of(
        container.handle
    );
    Recorder session(core.ptr(), Vector<StringName>({"scene_live"}));

    core->scene_publish_live(
        core->get_liveness_core()->route_of(inside.handle),
        container.handle,
        "Level"
    );

    REQUIRE(session.args("scene_live").size() == 1);
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("scene_live")[0]),
        container_handle.ptr()
    );
    CHECK(core->scene_handle_of(inside.handle) != container_handle);
    CHECK(core->entity_scene_of(inside.handle) == container.handle);
    CHECK(
        core->entity_scene_facet(inside.record, inside.wrapper.ptr())
        == container_handle
    );
    CHECK(
        core->entity_scene_facet(container.record, container.wrapper.ptr())
        == container_handle
    );

    SUBCASE(
        "a nested entity two levels down, with a non-declaring entity in "
        "between, answers its SCENE's facet, not its parent's, because the "
        "scene walk and the parent walk give different answers and only "
        "one of them names a scene"
    ) {
        const Bound middle = bind_under(core, container.owner, true, false);
        const Bound deep = bind_under(core, middle.owner, true, false);
        CHECK(core->entity_parent_of(deep.handle) == middle.handle);
        CHECK(core->entity_scene_of(deep.handle) == container.handle);
        CHECK(core->entity_scene_facet(deep.record, deep.wrapper.ptr()) == container_handle);
        CHECK(
            core->entity_scene_facet(deep.record, deep.wrapper.ptr())
            != core->scene_handle_of(middle.handle)
        );
    }

    SUBCASE(
        "an entity this session never adopted still has its own facet, "
        "because the scene an entity belongs to is asked before a session "
        "adopts it, so a record the index does not hold must still answer "
        "its own rather than nothing"
    ) {
        Ref<netw::NetwEntityRecord> stranger;
        stranger.instantiate();
        Ref<RefCounted> its_wrapper;
        its_wrapper.instantiate();
        CHECK(core->entity_scene_facet(stranger, its_wrapper.ptr()).is_valid());
    }

    SUBCASE("an entity under no declared scene answers its own facet") {
        const Bound loose = bind_under(core, root, true, false);
        CHECK(
            core->entity_scene_facet(loose.record, loose.wrapper.ptr())
            == core->scene_handle_of(loose.handle)
        );
        CHECK(
            core->entity_scene_facet(loose.record, loose.wrapper.ptr())
            != container_handle
        );
    }

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a scene request flood bounds a peer and "
    "never the host"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    const int64_t now = 1000;

    for (int at = 0; at < 20; ++at) {
        CHECK_FALSE(core->scene_request_flooded(1, now));
    }
    NETW_CHECK_EQ(core->verdict_total(int64_t(ERR_BUSY)), 0);

    for (int at = 0; at < 8; ++at) {
        CHECK_FALSE(core->scene_request_flooded(42, now));
    }
    CHECK(core->scene_request_flooded(42, now));
    NETW_CHECK_EQ(core->verdict_total(int64_t(ERR_BUSY)), 1);

    CHECK_FALSE(core->scene_request_flooded(43, now));
    NETW_CHECK_EQ(core->verdict_total(int64_t(ERR_BUSY)), 1);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a scene container is built for the "
    "isolation it declared"
) {
    Node *shared = NetwMultiplayerCore::scene_build_container(true, false);
    Node *viewed = NetwMultiplayerCore::scene_build_container(false, true);
    Node *isolated = NetwMultiplayerCore::scene_build_container(true, true);

    const StringName mark("_netw_scene_container");
    for (Node *container : {shared, viewed, isolated}) {
        CHECK(bool(container->get_name() == StringName("Scene")));
        CHECK(container->has_meta(mark));
        CHECK(container->get_script().get_type() == Variant::NIL);
        NETW_CHECK_EQ(container->get_child_count(), 0);
    }

    CHECK(Object::cast_to<SubViewport>(shared) == nullptr);
    CHECK(Object::cast_to<SubViewport>(viewed) == nullptr);
    REQUIRE(Object::cast_to<SubViewport>(isolated) != nullptr);
    CHECK(Object::cast_to<SubViewport>(isolated)->is_using_own_world_3d());
    NETW_CHECK_EQ(
        int(Object::cast_to<SubViewport>(isolated)->get_update_mode()),
        int(SubViewport::UPDATE_DISABLED)
    );

    Node *level = memnew(Node);
    level->set_name("Arena");
    NetwMultiplayerCore::scene_install_level(shared, level);

    CHECK(bool(shared->get_name() == StringName("ArenaScene")));
    REQUIRE(shared->get_child_count() == 1);
    CHECK(shared->get_child(0) == level);
    CHECK(level->get_owner() == shared);

    NetwMultiplayerCore::scene_install_level(nullptr, level);
    NetwMultiplayerCore::scene_install_level(viewed, nullptr);
    NETW_CHECK_EQ(viewed->get_child_count(), 0);

    memdelete(shared);
    memdelete(viewed);
    memdelete(isolated);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a mounted scene resolves in one stage"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);

    const Bound container = bind_under(core, root, true, true);
    Node *level = memnew(Node);
    level->set_name("Arena");
    container.owner->add_child(level);
    const Bound seated = bind_under(core, level, true, false);

    CHECK(core->entity_scene_of(seated.handle) == container.handle);
    CHECK(core->entity_scene_of(container.handle) == container.handle);

    const Bound bare = bind_under(core, root, true, false);
    Node *bare_level = memnew(Node);
    bare_level->set_name("Annex");
    bare.owner->add_child(bare_level);
    const Bound stranded = bind_under(core, bare_level, true, false);

    NETW_CHECK_EQ(core->entity_scene_of(stranded.handle).is_valid(), false);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a reparent crosses a boundary only "
    "when a player changes scene: a destination already inside the "
    "player's scene is not re-sent an admission edge it already holds, a "
    "destination under no declared scene has no boundary to admit to, and "
    "a server-owned entity is admitted nowhere because admission is a "
    "peer's"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound here = bind_under(core, root, true, true);
    const Bound there = bind_under(core, root, true, true);
    const Bound plain = bind_under(core, root, true, false);
    const Bound player = bind_under(core, here.owner, true, false, 7);

    CHECK(core->entity_reparent_crosses(player.handle, there.handle));

    CHECK_FALSE(core->entity_reparent_crosses(player.handle, here.handle));
    CHECK_FALSE(core->entity_reparent_crosses(player.handle, plain.handle));
    const Bound prop = bind_under(core, here.owner, true, false, 0);
    CHECK_FALSE(core->entity_reparent_crosses(prop.handle, there.handle));

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a move outside the tree is the two steps "
    "the engine would refuse to take as one"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound inside = bind_under(core, root, true);
    Node *destination = memnew(Node);
    root->add_child(destination);

    Ref<netw::NetwReparentOpts> opts;
    opts.instantiate();
    core->entity_reparent(inside.record, inside.owner, destination, opts);

    NETW_CHECK_EQ(inside.owner->get_parent(), destination);
    CHECK(inside.record->get_reparenting().is_null());

    SUBCASE("an owner no parent holds still moves") {
        Node *orphan_target = memnew(Node);
        const Bound orphan = bind_under(core, nullptr, true);
        REQUIRE(orphan.owner->get_parent() == nullptr);

        core->entity_reparent(orphan.record, orphan.owner, orphan_target, opts);

        NETW_CHECK_EQ(orphan.owner->get_parent(), orphan_target);
        CHECK(orphan.record->get_reparenting().is_null());
        memdelete(orphan_target);
    }

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a refused move leaves nothing in "
    "flight, because a record left holding one would read every later "
    "tree exit as a reparent and keep a dead route alive; a destination "
    "that is not a node is the whole of what a move can refuse"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound bound = bind_under(core, root, true);
    Ref<netw::NetwReparentOpts> opts;
    opts.instantiate();

    ERR_PRINT_OFF;
    core->entity_reparent(bound.record, bound.owner, nullptr, opts);
    ERR_PRINT_ON;

    NETW_CHECK_EQ(bound.owner->get_parent(), root);
    CHECK(bound.record->get_reparenting().is_null());

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a node is given one wrapper and keeps it"
) {
    netw_test::EntityFactories factories;
    netw_test::CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *root = memnew(Node);

    const Ref<RefCounted> minted = NetwMultiplayerCore::wrapper_ensure(root);

    CHECK(minted.is_valid());
    NETW_CHECK_EQ(log.count("wrapper"), 1);
    REQUIRE(log.args("wrapper").size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Node>(log.args("wrapper")[0]), root);

    SUBCASE("a node that already carries one is not given a second") {
        root->set_meta(NetwMultiplayerCore::wrapper_meta(), minted);
        CHECK(NetwMultiplayerCore::wrapper_ensure(root) == minted);
        NETW_CHECK_EQ(log.count("wrapper"), 1);
    }

    SUBCASE("the walk finds the nearest marked ancestor and nothing else") {
        root->set_meta(NetwMultiplayerCore::wrapper_meta(), minted);
        Node *plain = memnew(Node);
        root->add_child(plain);
        Node *deep = memnew(Node);
        plain->add_child(deep);

        CHECK(NetwMultiplayerCore::wrapper_at(deep) == minted);
        Node *stranger = memnew(Node);
        CHECK(NetwMultiplayerCore::wrapper_at(stranger).is_null());
        memdelete(stranger);
    }

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a record is provisioned on the "
    "topmost orphan, and never once the tree holds one, because a record "
    "provisioned on the leaf would name a node that is about to become "
    "somebody else's child"
) {
    netw_test::EntityFactories factories;
    netw_test::CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.minting("wrapper"));
    Node *top = memnew(Node);
    Node *middle = memnew(Node);
    top->add_child(middle);
    Node *leaf = memnew(Node);
    middle->add_child(leaf);

    const Ref<RefCounted> minted = NetwMultiplayerCore::wrapper_resolve(leaf);

    CHECK(minted.is_valid());
    REQUIRE(log.args("wrapper").size() == 1);
    NETW_CHECK_EQ(Object::cast_to<Node>(log.args("wrapper")[0]), top);

    memdelete(top);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a bind names the node and stamps the "
    "identity onto its wrapper"
) {
    netw_test::EntityFactories factories;
    Ref<netw::NetwEntityRecord> wrapper;
    wrapper.instantiate();
    netw_test::CallLog log;
    NetwMultiplayerCore::set_wrapper_factory(log.answering("wrapper", wrapper));
    Node *node = memnew(Node);

    NetwMultiplayerCore::wrapper_bind(node, "valeria", 7);

    CHECK(node->get_name() == StringName("valeria|7"));
    CHECK(wrapper->get_entity_id() == StringName("valeria"));
    NETW_CHECK_EQ(wrapper->get_peer_id(), 7);

    SUBCASE(
        "an id the codec refuses binds nothing at all, because a name that "
        "spells no identity would leave the node and the record "
        "disagreeing about who the entity is, which is worse than not "
        "binding"
    ) {
        Ref<netw::NetwEntityRecord> untouched;
        untouched.instantiate();
        NetwMultiplayerCore::set_wrapper_factory(
            log.answering("wrapper", untouched)
        );
        Node *refused = memnew(Node);
        refused->set_name("Standing");

        ERR_PRINT_OFF;
        NetwMultiplayerCore::wrapper_bind(refused, "a|b", 3);
        ERR_PRINT_ON;

        CHECK(refused->get_name() == StringName("Standing"));
        CHECK(untouched->get_entity_id() == StringName());
        NETW_CHECK_EQ(untouched->get_peer_id(), 0);
        memdelete(refused);
    }

    memdelete(node);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a linger deactivates at once and frees on "
    "the session's own count"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound bound = bind_under(core, root, true);
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    bound.owner->add_child(sync);
    sync->set_owner(bound.owner);
    sync->set_root_path(NodePath(".."));

    core->entity_linger(bound.record, bound.owner, 3);

    NETW_CHECK_EQ(bound.owner->get_process_mode(), Node::PROCESS_MODE_DISABLED);
    CHECK_FALSE(sync->get_visibility_for(0));
    NETW_CHECK_EQ(core->settle_pending(), 1);

    core->settle_advance();
    core->settle_advance();
    NETW_CHECK_EQ(core->settle_pending(), 1);

    SUBCASE("a record with no owner lingers nothing") {
        core->settle_clear();
        core->entity_linger(bound.record, nullptr, 3);
        NETW_CHECK_EQ(core->settle_pending(), 0);
    }

    core->settle_clear();
    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a move is reported at the session's "
    "next settle, not while the reparent is still mid-propagation and the "
    "subtree is still rebuilding, and only for an owner still there to "
    "hear it"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    const Bound bound = bind_under(core, root, true);
    netw::gd::add_signal(bound.wrapper.ptr(), "reparented", 1);
    netw_test::CallLog log;
    bound.wrapper->connect("reparented", log.callable("moved"));
    Ref<netw::NetwReparentOpts> opts;
    opts.instantiate();

    core->entity_settle_reparented(bound.wrapper.ptr(), bound.owner, opts);

    NETW_CHECK_EQ(log.count("moved"), 0);
    NETW_CHECK_EQ(core->settle_pending(), 1);

    SUBCASE("two moves in one cascade are two reports") {
        core->entity_settle_reparented(bound.wrapper.ptr(), bound.owner, opts);
        NETW_CHECK_EQ(core->settle_pending(), 2);
    }

    SUBCASE(
        "a move whose owner is gone by the time the settle runs after the "
        "propagation is not reported at all, because a report naming it "
        "would hand a listener a node it cannot read"
    ) {
        core->entity_announce_reparented(bound.wrapper.ptr(), nullptr, opts);
        NETW_CHECK_EQ(log.count("moved"), 0);
    }

    core->settle_clear();
    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a property another stream already "
    "drives is governed, unless that stream is the one asking, because a "
    "stream is not competition with itself, and a property nothing drives "
    "is free to persist"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    MultiplayerSynchronizer *sync = memnew(MultiplayerSynchronizer);
    root->add_child(sync);
    sync->set_owner(root);
    sync->set_root_path(NodePath(".."));
    Ref<SceneReplicationConfig> config;
    config.instantiate();
    config->add_property(NodePath(".:name"));
    sync->set_replication_config(config);

    CHECK(core->entity_governs_property(root, NodePath(".:name"), nullptr, 0));

    CHECK_FALSE(
        core->entity_governs_property(root, NodePath(".:name"), sync, 0)
    );
    CHECK_FALSE(core->entity_governs_property(
        root,
        NodePath(".:process_mode"),
        nullptr,
        0
    ));

    SUBCASE("a session told of no replication plane resolves no binding") {
        CHECK(core->entity_derived_binding(root, 0, 7).is_null());
        NETW_CHECK_EQ(core->entity_derived_group(7).size(), 0);
    }

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a control transfer with no plane to "
    "carry it carries nothing, the ordinary case rather than an error, "
    "since every offline rig takes it on every transfer"
) {
    Ref<RefCounted> wrapper;
    wrapper.instantiate();

    NetwMultiplayerCore::entity_broadcast_control(wrapper.ptr(), nullptr, 4);
    NetwMultiplayerCore::entity_broadcast_control(nullptr, wrapper.ptr(), 4);
    NetwMultiplayerCore::entity_request_control(nullptr, nullptr, nullptr);
    CHECK(true);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] a first tree entry goes live once, and a "
    "reparent re-entry is not a second birth"
) {
    Node *owner = memnew(Node);
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    netw::gd::add_signal(wrapper.ptr(), "spawning");
    netw_test::CallLog log;
    wrapper->connect("spawning", log.callable("live"));
    Ref<netw::NetwEntityRecord> record;
    record.instantiate();
    record->set_entity_id("crate");
    REQUIRE(record->advance(int(netw::EntityStage::ARMED)));

    ERR_PRINT_OFF;
    NetwMultiplayerCore::entity_enter_tree(
        wrapper.ptr(),
        owner,
        record,
        nullptr,
        true
    );
    ERR_PRINT_ON;

    NETW_CHECK_EQ(record->get_stage(), int(netw::EntityStage::LIVE));
    NETW_CHECK_EQ(log.count("live"), 1);

    SUBCASE("a record already live is a reparent, and is born no second time") {
        ERR_PRINT_OFF;
        NetwMultiplayerCore::entity_enter_tree(
            wrapper.ptr(),
            owner,
            record,
            nullptr,
            true
        );
        ERR_PRINT_ON;
        NETW_CHECK_EQ(record->get_stage(), int(netw::EntityStage::LIVE));
        NETW_CHECK_EQ(log.count("live"), 1);
    }

    memdelete(owner);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] an entry the record classifies as "
    "inert stops there: owned by an enclosing scene and carrying no "
    "identity is an editor-placed factory, which declares itself a "
    "template and never spawns"
) {
    Node *scene = memnew(Node);
    Node *owner = memnew(Node);
    scene->add_child(owner);
    owner->set_owner(scene);
    Ref<RefCounted> wrapper;
    wrapper.instantiate();
    netw::gd::add_signal(wrapper.ptr(), "spawning");
    netw_test::CallLog log;
    wrapper->connect("spawning", log.callable("live"));
    Ref<netw::NetwEntityRecord> record;
    record.instantiate();

    ERR_PRINT_OFF;
    NetwMultiplayerCore::entity_enter_tree(
        wrapper.ptr(),
        owner,
        record,
        nullptr,
        true
    );
    ERR_PRINT_ON;

    NETW_CHECK_EQ(record->get_stage(), int(netw::EntityStage::TEMPLATE));
    NETW_CHECK_EQ(log.count("live"), 0);

    memdelete(scene);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P9 the entity a node stands for is "
    "the nearest record above it, adopted so the index holds what it "
    "answered; a node with nothing above it carrying a record has no "
    "entity to stand for, and a child inside the entity stands for the "
    "same one because the walk is to the nearest record above rather than "
    "to the node asked about"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();

    Node *bare = memnew(Node);
    NETW_CHECK_EQ(core->entity_of(bare).is_valid(), false);
    NETW_CHECK_EQ(core->entity_of(nullptr).is_valid(), false);

    Node *root = memnew(Node);
    Node *child = memnew(Node);
    root->add_child(child);
    Ref<netw::NetwEntity> entity;
    entity.instantiate();
    entity->attach_to(root);
    const RID handle = core->get_liveness_core()->entity_create();
    REQUIRE(entity->get_record().is_valid());
    entity->get_record()->adopt_handle(handle);

    const RID answered = core->entity_of(root);
    NETW_CHECK_EQ(answered == handle, true);

    NETW_CHECK_EQ(core->entity_of(child) == handle, true);

    NETW_CHECK_EQ(core->wrapper_of(answered).ptr() == entity.ptr(), true);

    memdelete(root);
    memdelete(bare);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P10 the owner index answers what the "
    "record answers, written from the record's own owner so the two reads "
    "are one answer rather than two that happen to agree; a re-adopt with "
    "no owner keeps the one it had, so an adopt that does not know the "
    "owner cannot lose the one on file; and both resolve an ObjectID, so a "
    "freed owner is nothing to either and neither hands back a dangling "
    "node"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *owner = memnew(Node);
    Ref<netw::NetwEntity> entity;
    entity.instantiate();
    entity->set_owner(owner);
    const RID handle = core->get_liveness_core()->entity_create();
    entity->get_record()->adopt_handle(handle);
    core->wrapper_adopt(handle, entity, owner);

    NETW_CHECK_EQ(core->wrapper_owner(handle) == owner, true);
    NETW_CHECK_EQ(entity->get_owner() == owner, true);

    core->wrapper_adopt(handle, entity, nullptr);
    NETW_CHECK_EQ(core->wrapper_owner(handle) == owner, true);

    memdelete(owner);

    CHECK(core->wrapper_owner(handle) == nullptr);
    CHECK(entity->get_owner() == nullptr);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P12 an entity's place in the "
    "interest engine is its ancestry, written root first: neither carries "
    "a route, so both are ordered by a minted ordinal, and the ancestor "
    "holding the first one is what proves the chain was written root "
    "first rather than merely asked about in that order; the parent link "
    "is what the clamp reads, so hiding the ancestor is what proves it "
    "was written; and syncing again does not renumber what was already "
    "minted"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    Node *child = memnew(Node);
    root->add_child(child);

    Ref<netw::NetwEntity> parent;
    parent.instantiate();
    parent->attach_to(root);
    const RID parent_handle = core->get_liveness_core()->entity_create();
    parent->get_record()->adopt_handle(parent_handle);
    REQUIRE(core->entity_of(root) == parent_handle);

    Ref<netw::NetwEntity> leaf;
    leaf.instantiate();
    leaf->attach_to(child);
    const RID leaf_handle = core->get_liveness_core()->entity_create();
    leaf->get_record()->adopt_handle(leaf_handle);
    REQUIRE(core->entity_of(child) == leaf_handle);

    core->interest_sync_record(leaf.ptr());

    netw::InterestEngine &engine = core->interest_plane();
    NETW_CHECK_EQ(engine.order_route_for(leaf_handle.get_id()), 2);
    NETW_CHECK_EQ(engine.order_route_for(parent_handle.get_id()), 1);

    const StringName near("near");
    const StringName empty("empty");
    const int bit = engine.peer_bit_for(11);
    PackedInt64Array live;
    live = netw::InterestBitSet::with_bit(live, bit, true);
    engine.set_live_peers(live);
    engine.layer_add_viewer(near, 11);
    engine.membership_add(leaf_handle.get_id(), near);
    engine.commit(engine.recompute());
    CHECK(engine.test(leaf_handle.get_id(), bit));

    engine.declare_layer(empty);
    engine.membership_add(parent_handle.get_id(), empty);
    engine.commit(engine.recompute());
    CHECK(!engine.test(leaf_handle.get_id(), bit));

    core->interest_sync_record(leaf.ptr());
    NETW_CHECK_EQ(engine.order_route_for(leaf_handle.get_id()), 2);

    core->interest_sync_record(nullptr);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P13 an entity whose owner sits below "
    "the node carrying it climbs to itself and the walk stops, which this "
    "case proves simply by returning: an entity is found by walking up "
    "from its owner to the nearest record, nothing makes the owner the "
    "node the record is attached to, so the climb can arrive back where "
    "it started, and without the guard this does not fail, it does not "
    "return"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *root = memnew(Node);
    Node *child = memnew(Node);
    root->add_child(child);

    Ref<netw::NetwEntity> entity;
    entity.instantiate();
    entity->attach_to(root);
    entity->set_owner(child);
    const RID handle = core->get_liveness_core()->entity_create();
    entity->get_record()->adopt_handle(handle);

    core->interest_sync_record(entity.ptr());

    netw::InterestEngine &engine = core->interest_plane();
    NETW_CHECK_EQ(engine.order_route_for(handle.get_id()), 1);

    memdelete(root);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P14 a scene's admission layer is "
    "named by its content root, and by its route once it has one; two "
    "live scenes built from the same content share a stem, so the stem "
    "alone is not a key, and a scene armed this frame has no route yet "
    "and is keyed by the stem until one lands, which is why both "
    "spellings exist; a container with nothing in it is no admission "
    "boundary, a different answer from one nobody has admitted anybody to"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *container = memnew(Node);
    Node *level = memnew(Node);
    level->set_name("Arena");
    container->add_child(level);

    Ref<netw::NetwEntity> scene;
    scene.instantiate();
    scene->attach_to(container);
    const RID handle = core->get_liveness_core()->entity_create();
    scene->get_record()->adopt_handle(handle);
    REQUIRE(core->entity_of(container) == handle);

    CHECK(core->scene_layer_id(handle) == StringName("scene:Arena"));

    REQUIRE(core->get_liveness_core()->bind_route(handle, 7));
    CHECK(core->scene_layer_id(handle) == StringName("scene:Arena#7"));

    Node *hollow = memnew(Node);
    Ref<netw::NetwEntity> bare;
    bare.instantiate();
    bare->attach_to(hollow);
    const RID hollow_handle = core->get_liveness_core()->entity_create();
    bare->get_record()->adopt_handle(hollow_handle);
    REQUIRE(core->entity_of(hollow) == hollow_handle);
    REQUIRE(core->wrapper_owner(hollow_handle) == hollow);

    CHECK(core->scene_layer_id(hollow_handle) == StringName());
    CHECK(core->scene_layer_id(RID()) == StringName());

    memdelete(hollow);
    memdelete(container);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P11 a scene answers to its declared "
    "label, and to its content root's name, the stem the live book is "
    "entered under, when no script named one; a declared label outranks "
    "the content root's name, because a script that named its scene named "
    "what every verb should answer it under"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Node *container = memnew(Node);
    container->set_name("Scene");
    Node *level = memnew(Node);
    level->set_name("Arena");
    container->add_child(level);

    Ref<netw::NetwEntity> entity;
    entity.instantiate();
    entity->set_owner(container);
    const RID scene = core->get_liveness_core()->entity_create();
    entity->get_record()->adopt_handle(scene);
    core->wrapper_adopt(scene, entity, container);

    CHECK(core->scene_stem(scene) == StringName("Arena"));

    entity->set_scene_label("Lobby");
    CHECK(core->scene_stem(scene) == StringName("Lobby"));

    CHECK(core->scene_stem(RID()) == StringName());

    memdelete(container);
}

TEST_CASE(
    "[Networked][Multiplayer][Hosted] P12 a packed scene answers the stem "
    "its content root is named, which is the key the live book is entered "
    "under, never the stem of the resource path, a FILE name that "
    "disagrees with it by convention"
) {
    Node *root = memnew(Node);
    root->set_name("Arena");
    Ref<PackedScene> packed;
    packed.instantiate();
    packed->pack(root);

    CHECK(NetwMultiplayerCore::scene_packed_stem(packed) == StringName("Arena"));

    packed->set_path("res://scenes/arena_level.tscn");
    CHECK(NetwMultiplayerCore::scene_packed_stem(packed) == StringName("Arena"));

    CHECK(
        NetwMultiplayerCore::scene_packed_stem(Ref<PackedScene>())
        == StringName()
    );

    memdelete(root);
}

} // namespace TestNetwMultiplayerEntityTree

namespace TestNetwClockWireCodec {

using namespace godot;

TEST_CASE(
    "[Networked][Clock][Hosted] the clock protocol writes its integers "
    "little-endian, which is the byte order a module-tier peer and an "
    "extension-tier peer have to agree on to read one another's ping"
) {
    PackedByteArray bytes;
    bytes.resize(4);
    netw::gd::encode_u32(bytes, 0, 0x12345678u);

    NETW_CHECK_EQ(int(bytes[0]), 0x78);
    NETW_CHECK_EQ(int(bytes[1]), 0x56);
    NETW_CHECK_EQ(int(bytes[2]), 0x34);
    NETW_CHECK_EQ(int(bytes[3]), 0x12);
    NETW_CHECK_EQ(int64_t(netw::gd::decode_u32(bytes, 0)), int64_t(0x12345678));
}

TEST_CASE(
    "[Networked][Clock][Hosted] a pong payload carries three independent "
    "fields at fixed offsets, so a tick never reads back as a timestamp"
) {
    PackedByteArray pong;
    pong.resize(9);
    netw::gd::encode_u32(pong, 0, 4'000'000'001u);
    netw::gd::encode_u32(pong, 4, 987u);
    netw::gd::encode_u8(pong, 8, 200u);

    NETW_CHECK_EQ(
        int64_t(netw::gd::decode_u32(pong, 0)),
        int64_t(4'000'000'001u)
    );
    NETW_CHECK_EQ(int64_t(netw::gd::decode_u32(pong, 4)), int64_t(987));
    NETW_CHECK_EQ(int(netw::gd::decode_u8(pong, 8)), 200);
}

} // namespace TestNetwClockWireCodec
