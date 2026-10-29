#include "netw/api/netw_multiplayer.hpp"

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_options.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/prediction.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/session.hpp"
#include "netw/entity/stage.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/peer_rename.hpp"
#include "netw/persist/database.hpp"
#include "netw/predict/engine.hpp"
#include "netw/scene_core.hpp"
#include "netw/script/model.hpp"
#include "netw/session/frames.hpp"
#include "netw/session_decl.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/subsystems.hpp"
#include "netw/wire/stream.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_SERVER_RELABELLED = "server_relabelled";
const char *SIG_PEER_CONNECTED = "peer_connected";
const char *SIG_PEER_DISCONNECTED = "peer_disconnected";
const char *VERB_TRANSFER_SERVER = "transfer_server";

String transport_name(const Ref<MultiplayerPeer> &p_peer) {
    return p_peer.is_valid() ? String(p_peer->get_class()) : String("null");
}

int64_t transport_label(int64_t p_label, int64_t p_s, int64_t p_f) {
    if (p_label == p_s) {
        return SERVER_PEER;
    }
    return p_label == SERVER_PEER ? p_f : p_label;
}

} // namespace

bool NetwMultiplayer::peer_relabels(const Ref<MultiplayerPeer> &p_peer) {
    return p_peer.is_valid()
        && p_peer->has_signal(StringName(SIG_SERVER_RELABELLED));
}

void NetwMultiplayer::relabel_follow(const Ref<MultiplayerPeer> &p_peer) {
    const Callable follow
        = callable_mp(this, &NetwMultiplayer::session_follow_relabel);
    const StringName relabelled(SIG_SERVER_RELABELLED);
    if (relabel_source.is_valid()
        && relabel_source->is_connected(relabelled, follow)) {
        relabel_source->disconnect(relabelled, follow);
    }
    relabel_source = peer_relabels(p_peer) ? p_peer : Ref<MultiplayerPeer>();
    if (relabel_source.is_valid()) {
        relabel_source->connect(relabelled, follow);
    }
}

void NetwMultiplayer::session_refuse_relabel(int64_t p_successor_was) {
    NETW_ERROR(
        sys::SESSION,
        "%s moved the server to peer %d, which is not in this session, so the "
        "session ended. A transport names a peer it links to as the "
        "successor.",
        transport_name(relabel_source),
        int(p_successor_was)
    );
    session_note_exit(enums::NetwSession::CONNECTION_LOST, String());
    session_teardown_online();
}

void NetwMultiplayer::session_reset_links(const PackedInt32Array &p_links) {
    for (int at = 0; at < p_links.size(); ++at) {
        session_clear_link_delivery(p_links[at]);
    }
    rpc_clear_session();
}

void NetwMultiplayer::session_reset_inner(
    const PackedInt32Array &p_links,
    int64_t p_successor_was,
    int64_t p_former_now
) {
    const Callable auth = inner->get_auth_callback();
    inner->set_auth_callback(Callable());
    const int64_t own = get_unique_id();
    for (int at = 0; at < p_links.size(); ++at) {
        effective_peer->emit_signal(
            StringName(SIG_PEER_DISCONNECTED),
            p_links[at]
        );
    }
    for (int at = 0; at < p_links.size(); ++at) {
        const int64_t next
            = transport_label(p_links[at], p_successor_was, p_former_now);
        if (next != 0 && next != own) {
            effective_peer->emit_signal(StringName(SIG_PEER_CONNECTED), next);
        }
    }
    inner->set_auth_callback(auth);
}

void NetwMultiplayer::session_rename_peers(
    const PeerRename &p_rename,
    LocalVector<Ref<NetwEntity>> &r_renamed
) {
    for (KeyValue<int64_t, PlayerRow> &row : players) {
        const int64_t renamed = p_rename.of(row.value.peer);
        if (renamed == row.value.peer) {
            continue;
        }
        row.value.peer = renamed;
        if (row.value.row.is_valid()) {
            row.value.row->rebind_peer(renamed);
        }
    }
    p_rename.rekey(player_id_of_peer);
    p_rename.rekey(peer_buckets);
    p_rename.rekey(link_handshakes);
    p_rename.rekey(unreachable_peers);
    p_rename.rekey(settled_peer_departures);
    join_roster.rename(p_rename);
    scene_membership.rename_members(p_rename);
    interest_engine.rename_peers(p_rename);
    liveness_core->rename_authors(p_rename);
    LocalVector<Ref<NetwEntity>> live;
    transfer_live_entities(live);
    for (const Ref<NetwEntity> &entity : live) {
        if (entity->rename_peers(p_rename)) {
            r_renamed.push_back(entity);
        }
    }
    interest_sync_live_peers();
}

void NetwMultiplayer::transfer_live_entities(
    LocalVector<Ref<NetwEntity>> &r_live
) {
    r_live.reserve(entity_slots.size());
    entity_slots.each([&r_live](uint64_t, const EntitySlot &p_slot) {
        if (p_slot.live.is_valid()) {
            r_live.push_back(p_slot.live);
        }
    });
}

void NetwMultiplayer::session_follow_relabel_pending(int64_t p_successor_was) {
    if (effective_peer.is_null() || !auth_is_armed()) {
        return;
    }
    if (!prepared_join.has_value() && submitted_join.has_value()) {
        prepared_join = submitted_join;
    }
    installing_transfer = true;
    const PackedInt32Array pending = get_authenticating_peers();
    for (int at = 0; at < pending.size(); ++at) {
        const int64_t peer = pending[at];
        if (peer == SERVER_PEER || peer == p_successor_was) {
            effective_peer->emit_signal(
                StringName(SIG_PEER_DISCONNECTED),
                peer
            );
            link_handshakes.erase(peer);
        }
    }
    installing_transfer = false;
    effective_peer->emit_signal(StringName(SIG_PEER_CONNECTED), SERVER_PEER);
}

void NetwMultiplayer::session_follow_relabel(
    int64_t p_successor_was,
    int64_t p_former_now
) {
    if (session_get_state() == SESSION_STATE_CONNECTING
        && !has_server_role()) {
        session_follow_relabel_pending(p_successor_was);
        return;
    }
    if (session_get_state() != SESSION_STATE_ONLINE) {
        NETW_TRACE(
            sys::SESSION,
            "A server transfer arrived while the session is in state %d, so "
            "it was ignored.",
            session_get_state()
        );
        return;
    }
    if (installing_transfer) {
        transfer_violated = true;
        NETW_ERROR(
            sys::SESSION,
            "%s moved the server again while the last move was installing, so "
            "the session ended. A transport finishes one move before it "
            "starts the next.",
            transport_name(relabel_source)
        );
        return;
    }
    const PackedInt32Array links = gd::api_peer_ids(inner);
    const bool known = p_successor_was > SERVER_PEER
        && (p_successor_was == transfer_label
            || links.has(int32_t(p_successor_was)));
    if (!known) {
        session_refuse_relabel(p_successor_was);
        return;
    }
    const bool succeeds = p_successor_was == transfer_label;
    const bool was_server = transfer_label == SERVER_PEER;
    const bool rejoins = transfer_rejoins();
    installing_transfer = true;
    PackedInt64Array promoted;
    transfer_reset_resync();

    session_reset_links(links);
    ReplicationCore *replication = get_replication_plane();
    spawn::Pipeline *spawns
        = replication != nullptr ? replication->get_spawn_pipeline() : nullptr;
    if (spawns != nullptr) {
        spawns->clear_parks();
    }
    if (succeeds) {
        for (int at = 0; at < links.size(); ++at) {
            interest_peer_disconnected(links[at]);
        }
    }
    session_reset_inner(links, p_successor_was, p_former_now);

    const PeerRename rename
        = PeerRename::of_transfer(p_successor_was, p_former_now);
    LocalVector<Ref<NetwEntity>> renamed;
    session_rename_peers(rename, renamed);
    transfer_label = rename.of(transfer_label);

    if (succeeds) {
        session_set_role(
            player_local().is_valid() ? ROLE_LISTEN_SERVER
                                      : ROLE_DEDICATED_SERVER
        );
        transfer_former = p_former_now;
        if (spawns != nullptr) {
            promoted = spawns->promote_received();
        }
        liveness_core->leave_lease();
        const PackedInt32Array linked = gd::api_peer_ids(inner);
        transfer_await_resync(linked);
        for (int at = 0; at < linked.size(); ++at) {
            interest_peer_connected(linked[at]);
        }
    } else if (was_server) {
        session_set_role(ROLE_CLIENT);
        transfer_note_dying();
        if (spawns != nullptr) {
            spawns->demote_spawned();
        }
    }

    for (const Ref<NetwEntity> &entity : renamed) {
        entity->follow_rename();
        sim_follow_steering(entity.ptr());
    }
    interest_refresh_compat_intents();

    session_core.end_tenure();
    if (!succeeds) {
        clock_engine().forget_rtt();
        if (clock_engine().get_configured()) {
            clock_request_handshake();
        }
        entity_slots.each([](uint64_t, const EntitySlot &p_slot) {
            if (p_slot.live.is_valid()) {
                p_slot.live->forget_server_claims();
            }
        });
        scene_viewers_server_generation = 0;
        scene_viewers_parked.clear();
        if (rejoins) {
            transfer_release_world();
            clock_engine().set_held(true);
            transfer_awaits_roster = true;
        }
    }
    transfer_follow_scenes(succeeds);
    transfer_follow_interest(succeeds);
    transfer_follow_prediction();
    transfer_follow_players(succeeds, was_server);
    transfer_follow_persistence(succeeds);
    transfer_generation += 1;
    resync_generation = transfer_generation;
    if (!succeeds) {
        transfer_queue_resync(was_server);
    }
    installing_transfer = false;

    transfer_installed.successor_was = p_successor_was;
    transfer_installed.former_now = p_former_now;
    transfer_installed.pending = true;
    transfer_installed.promoted = promoted;
    if (transfer_violated) {
        transfer_violated = false;
        transfer_installed = TransferInstall();
        session_note_exit(enums::NetwSession::CONNECTION_LOST, String());
        session_teardown_online();
    }
}

void NetwMultiplayer::transfer_follow_scenes(bool p_succeeds) {
    if (!p_succeeds) {
        return;
    }
    for (const KeyValue<int64_t, uint64_t> &applied : scene_viewers_applied) {
        const uint64_t *held = scene_viewers_revision.getptr(applied.key);
        if (held == nullptr || *held < applied.value) {
            scene_viewers_revision[applied.key] = applied.value;
        }
    }
    LocalVector<Ref<NetwEntity>> live;
    transfer_live_entities(live);
#if defined(NETW_TESTS)
    membership_audit_held += 1;
#endif
    for (const Ref<NetwEntity> &entity : live) {
        membership_place_entity(entity);
    }
#if defined(NETW_TESTS)
    membership_audit_held -= 1;
    membership_audit();
#endif
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); ++at) {
        scene_publish_viewers(scenes[at]);
    }
}

void NetwMultiplayer::transfer_follow_interest(bool p_succeeds) {
    for (const KeyValue<int64_t, Ref<NetwInterestLayer>> &view : layer_views) {
        const StringName layer = view.value->get_layer_id();
        const PackedInt64Array held = interest_engine.roster(layer);
        for (int at = 0; at < held.size(); ++at) {
            interest_engine.roster_remove(layer, held[at]);
            interest_engine.membership_remove(held[at], layer);
        }
    }
    interest_clear_all_perception();
    if (!p_succeeds) {
        return;
    }
    LocalVector<Ref<NetwEntity>> live;
    transfer_live_entities(live);
    for (const Ref<NetwEntity> &entity : live) {
        interest_sync_scene_membership(entity);
    }
    const TypedArray<RID> scenes = scene_list();
    for (int at = 0; at < scenes.size(); ++at) {
        const PackedInt64Array members
            = scene_membership.members_of(scenes[at]);
        for (int which = 0; which < members.size(); ++which) {
            scene_sync_viewer(scenes[at], members[which]);
        }
    }
    interest_refresh_compat_intents();
    interest_recompute();
    interest_commit();
    interest_request_flush();
}

void NetwMultiplayer::transfer_follow_prediction() {
    NetwPredictionEngine *pool = get_prediction_engine();
    const PackedInt64Array slots = pool->ordered_slots();
    for (int at = 0; at < slots.size(); ++at) {
        pool->resend_relay_subscription(slots[at]);
    }
}

void NetwMultiplayer::transfer_follow_persistence(bool p_succeeds) {
    if (!p_succeeds) {
        return;
    }
    bool persisted = false;
    entity_slots.each([&persisted](uint64_t, const EntitySlot &p_slot) {
        if (persisted || p_slot.live.is_null()) {
            return;
        }
        Node *owner = p_slot.live->get_owner();
        persisted = owner != nullptr
            && script::model::get_persistence_config(owner).is_valid();
    });
    if (persisted) {
        NETW_ERROR(
            sys::SESSION,
            "The server moved to this peer, and database state does not move "
            "with it. Open the database again on this peer to save entities."
        );
    }
}

void NetwMultiplayer::transfer_follow_players(
    bool p_succeeds,
    bool p_was_server
) {
    const int64_t own = get_unique_id();
    if (!p_succeeds) {
        if (!p_was_server) {
            return;
        }
        for (KeyValue<int64_t, PlayerRow> &row : players) {
            if (row.value.row.is_valid() && row.value.peer != own) {
                row.value.row->bind_account(
                    String(),
                    row.value.row->get_account_name()
                );
            }
        }
        return;
    }
    LobbyDirectory *directory = session_serving_directory();
    for (KeyValue<int64_t, PlayerRow> &row : players) {
        row.value.handled = row.value.handled || row.value.admitted;
        if (row.key >= player_incarnation_next) {
            player_incarnation_next = row.key + 1;
        }
        if (directory == nullptr || row.value.row.is_null()) {
            continue;
        }
        const String account = directory->account_of(row.value.peer);
        if (!account.is_empty()) {
            row.value.row->bind_account(
                account,
                directory->member_name(row.value.peer)
            );
        }
    }
}

void NetwMultiplayer::transfer_resubmit_join() {
    if (join_awaiting_admission && submitted_join.has_value()) {
        const JoinRequest request = *submitted_join;
        session_submit_request(request);
    }
}

void NetwMultiplayer::session_announce_transfer() {
    if (!transfer_installed.pending) {
        return;
    }
    const TransferInstall installed = transfer_installed;
    transfer_installed = TransferInstall();
    if (session_core.is_server_role()) {
        player_despawn_heard_kept();
    }
    if (session_handle.is_valid()) {
        session_handle->announce_server_transferred(
            installed.successor_was,
            installed.former_now
        );
    }
    if (session_core.is_server_role() && transfer_rejoins()) {
        transfer_run_keep(installed.promoted);
    }
    if (installed.former_now == 0) {
        session_settle_peer_loss(installed.successor_was);
    }
    transfer_receive_held_resync();
    transfer_resubmit_join();
}

void NetwMultiplayer::transfer_receive_held_resync() {
    LocalVector<HaltJoin> held;
    held.reserve(resync_before_announce.size());
    for (const HaltJoin &report : resync_before_announce) {
        held.push_back(report);
    }
    resync_before_announce.clear();
    for (const HaltJoin &report : held) {
        session_receive_resync(report.payload, report.sender);
    }
}

bool NetwMultiplayer::transfer_rejoins() const {
    return session_settings.transfer_mode
        == enums::NetwSession::TRANSFER_REJOIN;
}

void NetwMultiplayer::transfer_release_world() {
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns == nullptr) {
        return;
    }
    transfer_authored = spawns->get_spawn_book()->authored_routes();
    spawns->release_received();
    LocalVector<uint64_t> departing;
    departing.reserve(entity_departures.size());
    for (const KeyValue<uint64_t, EntityDeparture> &row : entity_departures) {
        departing.push_back(row.key);
    }
    for (const uint64_t instance : departing) {
        entity_settle_departure(int64_t(instance));
    }
}

void NetwMultiplayer::transfer_run_keep(const PackedInt64Array &p_promoted) {
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_TRANSFER);
    if (declared.state == session_decl::ABSENT) {
        return;
    }
    if (declared.state != session_decl::READY) {
        declaration_book().report_unresolved(
            this,
            session_decl::KIND_TRANSFER,
            declared.state,
            "the transfer"
        );
        return;
    }
    bool reported = false;
    const bool admitted = halt_admits_despawn;
    halt_admits_despawn = true;
    for (int at = 0; at < p_promoted.size(); ++at) {
        const Ref<NetwEntity> entity = wrapper_for_route(p_promoted[at]);
        if (entity.is_null() || entity->get_owner() == nullptr
            || entity->get_stage() >= int64_t(entity::Stage::DESPAWNING)) {
            continue;
        }
        Node *node = entity->get_owner();
        bool called = false;
        const Variant kept
            = gd::call_checked(declared.callable, Variant(node), called);
        if (called && kept.get_type() == Variant::BOOL) {
            if (!bool(kept)) {
                entity->despawn(Ref<NetwDespawnOpts>());
                if (Node *parent = node->get_parent()) {
                    parent->remove_child(node);
                }
            }
            continue;
        }
        if (!reported) {
            reported = true;
            NETW_ERROR(
                sys::SESSION,
                "The Netw.configure_transfer handler did not return a bool "
                "for '%s', so it was kept. Make it take a Node and return "
                "true to keep it or false to despawn it.",
                String(node->get_name())
            );
        }
    }
    halt_admits_despawn = admitted;
}

void NetwMultiplayer::transfer_readmit(int64_t p_survivor) {
    sink_verdict(interest_flush_now(), 0);
    spawn_run_visibility_sweep();
    spawn_run_carrier_flush();
    send_to(
        p_survivor,
        0,
        session_roster_channel,
        join_roster.roster_frame(p_survivor),
        true,
        0,
        String(),
        false
    );
}

void NetwMultiplayer::transfer_readmitted() {
    spawn::Pipeline *spawns = spawn_plane();
    if (transfer_awaits_roster && spawns != nullptr) {
        const PackedInt64Array held = spawns->get_spawn_book()->recv_routes();
        for (int at = 0; at < held.size(); ++at) {
            liveness_core->follow_wire_epoch(int(held[at]));
        }
    }
    transfer_awaits_roster = false;
    clock_engine().set_held(false);
    const PackedInt64Array authored = transfer_authored;
    transfer_authored = PackedInt64Array();
    for (int at = 0; at < authored.size(); ++at) {
        const Ref<NetwEntity> entity = wrapper_for_route(authored[at]);
        if (entity.is_valid() && entity->get_owner() != nullptr) {
            entity->despawn(Ref<NetwDespawnOpts>());
        }
    }
}

void NetwMultiplayer::transfer_reset_resync() {
    resync_outbox.clear();
    resync_sent = 0;
    resync_raises.clear();
    resync_awaited.clear();
    resync_intake.clear();
    resync_ticks_waited = 0;
    halt_joins.clear();
    halt_leases.clear();
    resync_before_announce.clear();
    if (resync_holds_halt) {
        resync_holds_halt = false;
        structure_halted = false;
    }
}

void NetwMultiplayer::transfer_await_resync(
    const PackedInt32Array &p_survivors
) {
    for (int at = 0; at < p_survivors.size(); ++at) {
        resync_awaited.insert(int64_t(p_survivors[at]));
    }
    resync_holds_halt = true;
    resync_ticks_waited = 0;
    resync_tick_seen = receive_tick();
    structure_halt_enter();
    transfer_settle_resync();
}

void NetwMultiplayer::transfer_note_death(int64_t p_route) {
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns != nullptr && spawns->holds_spawned_route(p_route)) {
        transfer_gone.push_back(p_route);
    }
}

void NetwMultiplayer::transfer_note_dying() {
    transfer_called = false;
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns == nullptr) {
        return;
    }
    const PackedInt64Array issued = spawns->get_spawn_book()->spawned_routes();
    for (int at = 0; at < issued.size(); ++at) {
        const Ref<NetwEntity> entity = wrapper_for_route(issued[at]);
        if (entity.is_valid() && entity::stage_is_leaving(entity->get_stage())
            && !transfer_gone.has(issued[at])) {
            transfer_gone.push_back(issued[at]);
        }
    }
}

void NetwMultiplayer::transfer_despawn_gone(int64_t p_route) {
    const Ref<NetwEntity> entity = wrapper_for_route(p_route);
    if (entity.is_null() || entity->get_owner() == nullptr
        || entity::stage_is_leaving(entity->get_stage())) {
        return;
    }
    const bool admitted = halt_admits_despawn;
    halt_admits_despawn = true;
    entity->despawn(Ref<NetwDespawnOpts>());
    halt_admits_despawn = admitted;
}

void NetwMultiplayer::transfer_queue_resync(bool p_former) {
    session::ResyncHead head;
    head.route_floor = uint64_t(MAX(liveness_core->route_floor(), 0));
    for (const KeyValue<int64_t, PlayerRow> &row : players) {
        head.player_high = MAX(head.player_high, uint64_t(MAX(row.key, 0)));
    }
    LocalVector<session::RouteLease> leases;
    const PackedInt32Array spans = liveness_core->lease_spans();
    for (int at = 0; at + 1 < spans.size()
         && uint64_t(leases.size()) < session::RESYNC_LEASES_MAX;
         at += 2) {
        session::RouteLease lease;
        lease.base = uint64_t(spans[at]);
        lease.count = uint64_t(spans[at + 1]);
        leases.push_back(lease);
    }
    spawn::Pipeline *spawns = spawn_plane();
    PackedInt64Array held = spawns != nullptr
        ? spawns->get_spawn_book()->recv_routes()
        : PackedInt64Array();
    const PackedInt64Array gone = p_former ? transfer_gone : PackedInt64Array();
    transfer_gone = PackedInt64Array();
    held.append_array(gone);
    held.sort();
    LocalVector<session::ResyncRoute> rows;
    rows.reserve(uint32_t(held.size()));
    int64_t previous = 0;
    for (int at = 0; at < held.size(); ++at) {
        const int64_t route = held[at];
        if (route <= previous) {
            continue;
        }
        const AnchorRevision anchor = anchor_installed(route);
        const Ref<NetwEntity> entity = wrapper_for_route(route);
        session::ResyncRoute row;
        row.gone = gone.has(route);
        row.route_delta = uint64_t(route - previous);
        row.anchor = anchor.revision;
        row.anchor_author = anchor.author;
        row.control_revision
            = entity.is_valid() ? entity->get_control_revision() : 0;
        row.epoch = uint64_t(MAX(liveness_route_wire_life(route), int64_t(0)));
        rows.push_back(row);
        previous = route;
    }
    session::resync_chunks(
        head,
        leases,
        rows,
        datagram_budget(),
        resync_outbox
    );
#if defined(NETW_TESTS)
    if (resync_tamper != nullptr) {
        resync_tamper(resync_outbox, datagram_budget());
    }
#endif
}

void NetwMultiplayer::transfer_send_resync() {
    uint32_t sent = 0;
    while (resync_sent < resync_outbox.size()
           && sent < RESYNC_CHUNKS_PER_POLL) {
        send_to(
            SERVER_PEER,
            0,
            session_resync_channel,
            resync_outbox[resync_sent],
            true,
            0,
            String(),
            false
        );
        resync_sent += 1;
        sent += 1;
    }
    if (!resync_outbox.is_empty() && resync_sent >= resync_outbox.size()) {
        resync_outbox.clear();
        resync_sent = 0;
    }
}

void NetwMultiplayer::session_receive_resync(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (transfer_installed.pending) {
        HaltJoin held;
        held.payload = p_payload;
        held.sender = p_sender;
        resync_before_announce.push_back(held);
        return;
    }
    if (!is_host() || !resync_holds_halt
        || resync_generation != transfer_generation
        || !resync_awaited.has(p_sender)) {
        NETW_TRACE(
            sys::SESSION,
            "A world report from peer %d arrived when none was awaited, so it "
            "was ignored.",
            int(p_sender)
        );
        return;
    }
    session::Resync chunk;
    if (!session::resync_read(p_payload, chunk)) {
        NETW_TRACE(
            sys::SESSION,
            "A world report from peer %d did not decode, so it was ignored.",
            int(p_sender)
        );
        return;
    }
    ResyncIntake &intake = resync_intake[p_sender];
    const char *refused
        = transfer_check_report(chunk, p_payload.size(), intake);
    if (refused != nullptr) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d sent a world report %s, so it was disconnected. Run the "
            "same Networked version on every peer.",
            int(p_sender),
            String(refused)
        );
        transfer_drop_reporter(p_sender);
        return;
    }
    for (const session::RouteLease &lease : chunk.leases) {
        liveness_core->restore_grant(
            int(p_sender),
            int(MIN(lease.base, 0x7fffffffULL)),
            int(lease.count)
        );
    }
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns != nullptr) {
        spawn::Book *book = spawns->get_spawn_book();
        uint64_t route = 0;
        for (const session::ResyncRoute &row : chunk.routes) {
            route += row.route_delta;
            if (row.gone) {
                if (p_sender == transfer_former) {
                    transfer_despawn_gone(int64_t(route));
                }
                continue;
            }
            spawn::Record *record = book->spawned_of(int64_t(route));
            Node *node = record != nullptr ? record->node() : nullptr;
            if (node == nullptr || !node->is_inside_tree()) {
                spawn::VerbHead head;
                head.route = route;
                head.epoch = row.epoch;
                wire::WriteStream stream;
                if (spawn::VerbHead::wire.run(stream, head)
                    && stream.align_verify()) {
                    attribution_note_subject(int64_t(route));
                    send_to(
                        p_sender,
                        0,
                        spawns->channel_despawn,
                        stream.to_bytes(),
                        true,
                        0,
                        String(),
                        true
                    );
                }
                continue;
            }
            record->add_recipient(int(p_sender));
            store_world.stale_route(int64_t(route));
            if (liveness_core->adopt_epoch(int(route), int(row.epoch))) {
                liveness_core->raise_epoch_to_wire(int(route));
            }
            const AnchorRevision standing = anchor_installed(int64_t(route));
            const bool moved = row.anchor > standing.revision
                || (row.anchor == standing.revision
                    && row.anchor_author != standing.author);
            const Ref<NetwEntity> entity = NetwEntity::of(node);
            const bool steered = entity.is_valid()
                && row.control_revision > entity->get_control_revision();
            if (moved || steered) {
                ResyncRaise &raise = resync_raises[int64_t(route)];
                raise.anchor
                    = moved ? MAX(raise.anchor, row.anchor) : raise.anchor;
                raise.revision = steered
                    ? MAX(raise.revision, row.control_revision)
                    : raise.revision;
            }
        }
        spawns->schedule_carrier_flush();
    }
    if (!chunk.head.last) {
        return;
    }
    const uint64_t floor = MAX(intake.route_floor, intake.lease_high);
    liveness_core->mint_above(int(MIN(floor, 0x7fffffffULL)));
    player_incarnation_next
        = MAX(player_incarnation_next, int64_t(intake.player_high) + 1);
    resync_intake.erase(p_sender);
    resync_awaited.erase(p_sender);
    transfer_readmit(p_sender);
    transfer_settle_resync();
}

const char *NetwMultiplayer::transfer_check_report(
    const session::Resync &p_chunk,
    int64_t p_bytes,
    ResyncIntake &r_intake
) {
    static const char *TOO_LARGE = "larger than this session";
    static const char *TOO_HIGH = "with ids far above this session's";
    spawn::Pipeline *spawns = spawn_plane();
    const uint64_t held = spawns != nullptr
        ? uint64_t(spawns->get_spawn_book()->spawned_routes().size())
        : 0;
    const uint64_t most_routes
        = RESYNC_ROUTES_PER_HELD * MAX(held, RESYNC_ROUTES_HELD_MIN);
    r_intake.chunks += 1;
    r_intake.bytes += uint64_t(MAX(p_bytes, int64_t(0)));
    r_intake.routes += uint64_t(p_chunk.routes.size());
    if (r_intake.routes > most_routes
        || r_intake.bytes
            > uint64_t(MAX(datagram_budget(), int64_t(1))) * (most_routes + 1)
        || (p_chunk.routes.is_empty() && !p_chunk.head.last)
        || (r_intake.chunks > 1 && !p_chunk.leases.is_empty())) {
        return TOO_LARGE;
    }
    uint64_t route = 0;
    for (const session::ResyncRoute &row : p_chunk.routes) {
        route += row.route_delta;
    }
    r_intake.route_high = MAX(r_intake.route_high, route);
    const uint64_t floor = uint64_t(MAX(liveness_core->route_floor(), 0));
    const uint64_t route_bound
        = MAX(floor, r_intake.route_high) + RESYNC_RAISE_REACH;
    for (const session::RouteLease &lease : p_chunk.leases) {
        if (lease.count > uint64_t(ROUTE_LEASE_BLOCK)
            || lease.base + lease.count > route_bound + 1) {
            return TOO_HIGH;
        }
        if (lease.base > 0 && lease.count > 0) {
            r_intake.lease_high
                = MAX(r_intake.lease_high, lease.base + lease.count - 1);
        }
    }
    r_intake.route_floor = MAX(r_intake.route_floor, p_chunk.head.route_floor);
    r_intake.player_high = MAX(r_intake.player_high, p_chunk.head.player_high);
    if (!p_chunk.head.last) {
        return nullptr;
    }
    const uint64_t player_bound
        = uint64_t(MAX(player_incarnation_next - 1, int64_t(0)))
        + RESYNC_RAISE_REACH;
    if (r_intake.route_floor > route_bound
        || r_intake.player_high > player_bound) {
        return TOO_HIGH;
    }
    return nullptr;
}

void NetwMultiplayer::transfer_drop_reporter(int64_t p_sender) {
    resync_intake.erase(p_sender);
    liveness_core->abandon_lease(int(p_sender));
    disconnect_peer(p_sender);
    halt_forget_peer(p_sender);
}

void NetwMultiplayer::transfer_watch_resync() {
    if (!resync_holds_halt || resync_awaited.is_empty()) {
        return;
    }
    const int64_t now = receive_tick();
    if (now > resync_tick_seen) {
        resync_ticks_waited += now - resync_tick_seen;
    }
    resync_tick_seen = now;
    const int64_t deadline = session_settings.transfer_deadline_ticks;
    if (deadline <= 0 || resync_ticks_waited < deadline) {
        return;
    }
    LocalVector<int64_t> silent;
    silent.reserve(resync_awaited.size());
    for (const int64_t peer : resync_awaited) {
        silent.push_back(peer);
    }
    for (const int64_t peer : silent) {
        NETW_WARN(
            sys::SESSION,
            "Peer %d did not finish its world report within %d ticks of the "
            "server moving, so it was disconnected. Run the same Networked "
            "version on every peer.",
            int(peer),
            int(deadline)
        );
        transfer_drop_reporter(peer);
    }
}

void NetwMultiplayer::transfer_settle_resync() {
    if (!resync_holds_halt || !resync_awaited.is_empty()) {
        return;
    }
    resync_holds_halt = false;
    transfer_raise_reported();
    structure_halt_leave();
}

void NetwMultiplayer::transfer_raise_reported() {
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns == nullptr) {
        resync_raises.clear();
        return;
    }
    spawn::Book *book = spawns->get_spawn_book();
    const PackedInt32Array peers = spawn_peer_ids();
    for (const KeyValue<int64_t, ResyncRaise> &noted : resync_raises) {
        spawn::Record *record = book->spawned_of(noted.key);
        Node *node = record != nullptr ? record->node() : nullptr;
        if (node == nullptr || !node->is_inside_tree()) {
            continue;
        }
        if (noted.value.anchor > 0) {
            const AnchorRevision standing = anchor_installed(noted.key);
            entity_install_anchor(
                noted.key,
                MAX(standing.revision, noted.value.anchor) + 1,
                uint64_t(get_unique_id())
            );
            spawn_send_reparent(
                book,
                record,
                node,
                peers,
                spawns->channel_reparent,
                0
            );
        }
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        if (noted.value.revision > 0 && entity.is_valid()) {
            entity->raise_control(noted.value.revision);
        }
    }
    resync_raises.clear();
    spawns->schedule_carrier_flush();
}

Error NetwMultiplayer::session_transfer_server(int64_t p_peer) {
    NETW_ERR_COND_V(
        !is_online() || !session_core.is_server_role(),
        ERR_UNAUTHORIZED,
        sys::SESSION,
        "NetwSession.transfer_server runs only on the server."
    );
    const Ref<MultiplayerPeer> transport = assigned_peer;
    if (!peer_relabels(transport)
        || !transport->has_method(StringName(VERB_TRANSFER_SERVER))) {
        return ERR_UNAVAILABLE;
    }
    NETW_ERR_COND_V(
        p_peer == SERVER_PEER || !gd::api_peer_ids(inner).has(int32_t(p_peer)),
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "NetwSession.transfer_server got peer %d, which is not a client of "
        "this session. Pass the peer id of a connected client.",
        int(p_peer)
    );
    if (databases.holds_writes()) {
        return ERR_BUSY;
    }
    player_despawn_kept();
    transfer_called = true;
    const Error answered
        = Error(int(transport->call(StringName(VERB_TRANSFER_SERVER), p_peer)));
    if (answered != OK) {
        transfer_called = false;
    }
    return answered;
}

} // namespace netw
