#include "netw/api/netw_multiplayer.hpp"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/lifecycle_request.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/entity/stage.hpp"
#include "netw/lifecycle/rule.hpp"
#include "netw/log.hpp"
#include "netw/predict/engine.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/pipeline.hpp"

using namespace godot;

namespace netw {

namespace {

struct ControllerOp {
    int64_t route = 0;
    int64_t epoch = 0;
    Dictionary anchor;
    uint64_t base = 0;
    uint64_t base_author = 0;
};

bool read_controller_op(
    const NetwMultiplayer *p_plane,
    lifecycle::Kind p_kind,
    const PackedByteArray &p_payload,
    ControllerOp &r_op
) {
    wire::ReadStream reader(p_payload);
    return NetwMultiplayer::verb_head_read(reader, r_op.route, r_op.epoch)
        && (p_kind != lifecycle::Kind::REPARENT
            || p_plane->anchor_decode(reader, r_op.anchor))
        && reader.varuint(r_op.base, 5) && reader.varuint(r_op.base_author, 5)
        && reader.align_verify() && reader.bits_remaining() == 0;
}

String error_named(Error p_code) {
    return gd::error_name(int64_t(p_code));
}

Ref<NetwEntity> entity_holding(Node *p_node) {
    for (Node *at = p_node; at != nullptr; at = at->get_parent()) {
        const Ref<NetwEntity> found = NetwEntity::of(at);
        if (found.is_valid()) {
            return found;
        }
    }
    return Ref<NetwEntity>();
}

String root_name(Node *p_root) {
    return p_root != nullptr ? String(p_root->get_name()) : String("<null>");
}

} // namespace

lifecycle::Facts NetwMultiplayer::lifecycle_entity_facts(
    NetwEntity *p_entity,
    lifecycle::Kind p_kind
) {
    lifecycle::Facts facts;
    facts.kind = p_kind;
    if (p_entity == nullptr) {
        return facts;
    }
    facts.declared
        = p_entity->get_lifecycle() == NetwEntity::LIFECYCLE_CONTROLLER;
    const Ref<NetwPredictionHandle> prediction = p_entity->get_prediction();
    facts.predicted = prediction.is_valid()
        && prediction->get_archetype() != NetwPredict::ARCHETYPE_NONE;
    const int64_t route = liveness_route_of(p_entity);
    spawn::Pipeline *spawns = spawn_plane();
    facts.native = !p_entity->synchronizers().is_empty()
        || (route > 0 && spawns != nullptr && spawns->spawner_produced(route));
    facts.player_bound = p_entity->get_peer_id() != 0;
    if (p_kind == lifecycle::Kind::SPAWN) {
        const Ref<NetwPersistenceConfig> persisted
            = script::model::get_persistence_config(p_entity->get_owner());
        facts.load_on_spawn
            = persisted.is_valid() && persisted->get_load_at_spawn();
    }
    const AnchorRevision installed
        = route > 0 ? anchor_installed(route) : AnchorRevision();
    facts.base = installed.revision;
    facts.base_author = installed.author;
    facts.revision = installed.revision;
    facts.revision_author = installed.author;
    return facts;
}

lifecycle::Facts NetwMultiplayer::lifecycle_local_facts(
    NetwEntity *p_entity,
    lifecycle::Kind p_kind
) {
    lifecycle::Facts facts = lifecycle_entity_facts(p_entity, p_kind);
    facts.peer_is_session = is_session_authority();
    facts.minting_halted = structure_halted;
    const NetwPredictionEngine *engine = get_prediction_engine();
    facts.replaying = engine != nullptr && engine->is_replaying();
    if (p_entity == nullptr) {
        return facts;
    }
    facts.peer_is_controller = p_entity->is_controller_here();
    if (!facts.peer_is_controller && p_entity->is_claim_pending()) {
        facts.peer_claim
            = p_entity->get_transfer() == NetwEntity::TRANSFER_IMMEDIATE
            ? lifecycle::Claim::IMMEDIATE
            : lifecycle::Claim::REQUESTABLE;
    }
    return facts;
}

lifecycle::Facts NetwMultiplayer::lifecycle_sender_facts(
    NetwEntity *p_entity,
    lifecycle::Kind p_kind,
    int64_t p_sender
) {
    lifecycle::Facts facts = lifecycle_entity_facts(p_entity, p_kind);
    facts.peer_is_session = p_sender == session_authority_peer();
    if (p_entity != nullptr) {
        const int64_t steering = p_entity->get_controller();
        facts.peer_is_controller = steering != 0 && steering == p_sender;
    }
    return facts;
}

lifecycle::Destination NetwMultiplayer::lifecycle_destination(
    Node *p_mover,
    Node *p_parent
) const {
    if (p_mover == nullptr || p_parent == nullptr) {
        return lifecycle::Destination::UNRESOLVABLE;
    }
    if (p_parent == p_mover || p_mover->is_ancestor_of(p_parent)) {
        return lifecycle::Destination::INSIDE_MOVER;
    }
    const Ref<NetwEntity> anchor = NetwEntity::of(p_parent);
    const bool routed
        = anchor.is_valid() && liveness_route_of(anchor.ptr()) > 0;
    Node *root = session_root();
    if (!routed
        && (root == nullptr
            || !(p_parent == root || root->is_ancestor_of(p_parent)))) {
        return lifecycle::Destination::OUTSIDE;
    }
    const Ref<NetwEntity> holder = entity_holding(p_parent);
    if (holder.is_valid() && entity::stage_is_leaving(holder->get_stage())) {
        return lifecycle::Destination::DYING;
    }
    return lifecycle::Destination::OK;
}

lifecycle::Ruling NetwMultiplayer::lifecycle_judge_verb(
    const lifecycle::Facts &p_facts,
    const String &p_verb,
    Node *p_root,
    String &r_text
) {
    const lifecycle::Ruling ruling = lifecycle::rule(p_facts);
    r_text
        = lifecycle::refusal_text(p_verb, root_name(p_root), p_facts, ruling);
    if (ruling.verdict != lifecycle::Verdict::STATIC) {
        return ruling;
    }
    if (ruling.code == ERR_BUSY) {
        NETW_DEBUG(sys::ENTITY, "%s", r_text);
    } else {
        NETW_ERROR(sys::ENTITY, "%s", r_text);
    }
    return ruling;
}

bool NetwMultiplayer::lifecycle_authors_move(NetwEntity *p_entity) {
    if (p_entity == nullptr || is_session_authority()) {
        return false;
    }
    return lifecycle::rule(
               lifecycle_local_facts(p_entity, lifecycle::Kind::REPARENT)
    )
        .admitted();
}

void NetwMultiplayer::entity_settle_captured(NetwEntity *p_entity) {
    if (p_entity == nullptr) {
        return;
    }
    const uint64_t instance = uint64_t(p_entity->get_instance_id());
    if (entity_departures.has(instance)) {
        entity_settle_departure(int64_t(instance));
    }
}

void NetwMultiplayer::lifecycle_send_move(const EntityDeparture &p_row) {
    NetwEntity *entity = p_row.wrapper.ptr();
    Node *owner = entity->get_owner();
    Node *parent = owner != nullptr ? owner->get_parent() : nullptr;
    wire::WriteStream stream;
    AnchorRevision base;
    base.revision = p_row.base;
    base.author = p_row.base_author;
    if (parent == nullptr || !verb_head_write(stream, p_row.route)
        || !anchor_encode(stream, parent) || !stream.varuint(base.revision, 5)
        || !stream.varuint(base.author, 5) || !stream.align_verify()) {
        lifecycle::Facts facts
            = lifecycle_local_facts(entity, lifecycle::Kind::REPARENT);
        facts.destination = lifecycle::Destination::OUTSIDE;
        lifecycle::Ruling refused;
        refused.verdict = lifecycle::Verdict::STATIC;
        refused.code = ERR_INVALID_PARAMETER;
        const String text = lifecycle::refusal_text(
            String("Node.reparent"),
            root_name(owner),
            facts,
            refused
        );
        lifecycle_refuse_move_here(p_row, ERR_INVALID_PARAMETER, text);
        return;
    }
    if (entity->structure_ops_outstanding() >= entity::Control::MAX_PENDING) {
        lifecycle_refuse_move_here(
            p_row,
            ERR_UNAVAILABLE,
            vformat(
                "Node.reparent: '%s' already has %d moves waiting on the "
                "session authority, the most one entity may have. Chain the "
                "next move on the promise of the last.",
                root_name(owner),
                int(entity::Control::MAX_PENDING)
            )
        );
        return;
    }
    entity->structure_op_issue(p_row.base, p_row.promise);
    lifecycle_send_to_session(
        p_row.route,
        gate_channels.reparent,
        stream.to_bytes()
    );
}

void NetwMultiplayer::lifecycle_refuse_move_here(
    const EntityDeparture &p_row,
    Error p_code,
    const String &p_text
) {
    NETW_WARN(sys::ENTITY, "%s It is moved back.", p_text);
    if (p_row.promise.is_valid()) {
        p_row.promise->reject(p_code, p_text);
    }
    AnchorRevision base;
    base.revision = p_row.base;
    base.author = p_row.base_author;
    lifecycle_undo(
        p_row.wrapper.ptr(),
        Object::cast_to<Node>(gd::object_of(p_row.parent_id)),
        base
    );
}

lifecycle::Facts NetwMultiplayer::lifecycle_frame_facts(
    int64_t p_sender,
    lifecycle::Kind p_kind,
    const PackedByteArray &p_payload
) {
    lifecycle::Facts facts;
    facts.kind = p_kind;
    facts.peer_is_session = p_sender == session_authority_peer();
    if (facts.peer_is_session || !is_session_authority()
        || p_kind == lifecycle::Kind::SPAWN) {
        return facts;
    }
    ControllerOp op;
    if (!read_controller_op(this, p_kind, p_payload, op)) {
        return facts;
    }
    const Ref<NetwEntity> entity = wrapper_for_route(op.route);
    facts = lifecycle_sender_facts(entity.ptr(), p_kind, p_sender);
    const AnchorRevision standing = anchor_standing(op.route);
    facts.revision = standing.revision;
    facts.revision_author = standing.author;
    facts.base = op.base;
    facts.base_author = op.base_author;
    if (p_kind != lifecycle::Kind::REPARENT) {
        return facts;
    }
    Node *owner = entity.is_valid() ? entity->get_owner() : nullptr;
    Node *parent = anchor_resolve(op.anchor);
    facts.destination = parent == nullptr
        ? lifecycle::Destination::UNRESOLVABLE
        : lifecycle_destination(owner, parent);
    return facts;
}

void NetwMultiplayer::lifecycle_refuse_op(
    int64_t p_sender,
    lifecycle::Kind p_kind,
    const PackedByteArray &p_payload,
    Error p_code
) {
    ControllerOp op;
    if (!read_controller_op(this, p_kind, p_payload, op) || op.route <= 0) {
        return;
    }
    if (p_kind == lifecycle::Kind::DESPAWN) {
        lifecycle_reserve(op.route, p_sender);
    }
    lifecycle_decide(op.route, p_sender, p_kind, op.base, p_code);
}

void NetwMultiplayer::lifecycle_withdraw_author(
    int64_t p_route,
    int64_t p_author
) {
    spawn::Pipeline *spawns = spawn_plane();
    Node *owner = liveness_node_of(p_route);
    if (spawns == nullptr || owner == nullptr) {
        return;
    }
    spawn::Book *book = spawns->get_spawn_book();
    const PackedInt64Array doomed = book->despawn_order(p_route);
    for (int at = 0; at < doomed.size(); ++at) {
        spawn::Record *record = book->spawned_of(doomed[at]);
        Node *node = record != nullptr ? record->node() : nullptr;
        if (node != nullptr && (node == owner || owner->is_ancestor_of(node))) {
            record->remove_recipient(int(p_author));
        }
    }
}

void NetwMultiplayer::lifecycle_reserve(int64_t p_route, int64_t p_author) {
    spawn::Pipeline *spawns = spawn_plane();
    if (spawns == nullptr) {
        return;
    }
    lifecycle_withdraw_author(p_route, p_author);
    spawns->replay_spawn_book(p_author);
}

bool NetwMultiplayer::lifecycle_filter_denies(
    lifecycle::Kind p_kind,
    int64_t p_requester,
    NetwEntity *p_entity,
    Node *p_destination,
    String &r_reason
) {
    if (p_entity == nullptr) {
        return false;
    }
    const Ref<NetwLifecycleRequest> request = NetwLifecycleRequest::create(
        p_kind,
        p_requester,
        Ref<NetwEntity>(p_entity),
        p_destination
    );
    p_entity
        ->emit_signal(StringName("lifecycle_requested"), p_requester, request);
    r_reason = request->get_reason();
    return request->get_denied();
}

void NetwMultiplayer::lifecycle_notify_refused(
    NetwEntity *p_entity,
    lifecycle::Kind p_kind,
    const String &p_reason
) {
    if (p_entity == nullptr) {
        return;
    }
    Node *owner = p_entity->get_owner();
    const Ref<NetwLifecycleRequest> request = NetwLifecycleRequest::create(
        p_kind,
        get_unique_id(),
        Ref<NetwEntity>(p_entity),
        owner != nullptr ? owner->get_parent() : nullptr
    );
    request->deny(p_reason);
    p_entity->emit_signal(StringName("lifecycle_refused"), request);
}

bool NetwMultiplayer::lifecycle_admit_despawn(
    int64_t p_sender,
    const PackedByteArray &p_payload
) {
    ControllerOp op;
    if (!read_controller_op(this, lifecycle::Kind::DESPAWN, p_payload, op)) {
        return true;
    }
    const Ref<NetwEntity> entity = wrapper_for_route(op.route);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        lifecycle_decide(
            op.route,
            p_sender,
            lifecycle::Kind::DESPAWN,
            op.base,
            ERR_UNAVAILABLE
        );
        return true;
    }
    String reason;
    if (lifecycle_filter_denies(
            lifecycle::Kind::DESPAWN,
            p_sender,
            entity.ptr(),
            nullptr,
            reason
        )) {
        lifecycle_reserve(op.route, p_sender);
        lifecycle_decide(
            op.route,
            p_sender,
            lifecycle::Kind::DESPAWN,
            op.base,
            ERR_UNAUTHORIZED,
            reason
        );
        return false;
    }
    lifecycle_decide(op.route, p_sender, lifecycle::Kind::DESPAWN, op.base, OK);
    Node *owner = entity->get_owner();
    entity->despawn(Ref<NetwDespawnOpts>());
    lifecycle_withdraw_author(op.route, p_sender);
    spawn_carry_land_out_of(owner);
    spawn_unplace_node(owner);
    return true;
}

void NetwMultiplayer::lifecycle_send_despawn(NetwEntity *p_entity) {
    const int64_t route = liveness_route_of(p_entity);
    spawn::Pipeline *spawns = spawn_plane();
    if (route <= 0 || is_session_authority() || spawns == nullptr
        || !spawns->holds_received_route(route)) {
        return;
    }
    lifecycle_detach_ahead(p_entity);
    AnchorRevision base = anchor_installed(route);
    wire::WriteStream stream;
    if (!verb_head_write(stream, route) || !stream.varuint(base.revision, 5)
        || !stream.varuint(base.author, 5) || !stream.align_verify()) {
        return;
    }
    lifecycle_send_to_session(route, gate_channels.despawn, stream.to_bytes());
}

void NetwMultiplayer::lifecycle_send_to_session(
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    lifecycle_send_to(session_authority_peer(), p_route, p_channel, p_payload);
}

void NetwMultiplayer::lifecycle_send_to(
    int64_t p_peer,
    int64_t p_route,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    attribution_note_subject(p_route);
    send_to(p_peer, 0, p_channel, p_payload, true, 0, String(), true);
}

void NetwMultiplayer::lifecycle_seed_controller(
    NetwEntity *p_entity,
    int64_t p_controller
) {
    p_entity->record_controller(p_controller);
    p_entity->seed_control(1, 1, int64_t(entity::Control::HOLD_NONE));
}

void NetwMultiplayer::lifecycle_detach_ahead(NetwEntity *p_dying) {
    Node *owner = p_dying->get_owner();
    Node *above = owner != nullptr ? owner->get_parent() : nullptr;
    if (above == nullptr || !owner->is_inside_tree()) {
        return;
    }
    LocalVector<Node *> detaching;
    LocalVector<Node *> walk;
    for (int at = 0; at < owner->get_child_count(); ++at) {
        walk.push_back(owner->get_child(at));
    }
    while (!walk.is_empty()) {
        Node *node = walk[walk.size() - 1];
        walk.remove_at(walk.size() - 1);
        const Ref<NetwEntity> entity = NetwEntity::of(node);
        if (entity.is_valid() && liveness_route_of(entity.ptr()) > 0
            && entity->get_on_parent_despawn()
                == NetwEntity::PARENT_DESPAWN_DETACH) {
            detaching.push_back(node);
            continue;
        }
        for (int at = 0; at < node->get_child_count(); ++at) {
            walk.push_back(node->get_child(at));
        }
    }
    const int64_t route = liveness_route_of(p_dying);
    LocalVector<DetachedAhead> moved;
    for (Node *node : detaching) {
        DetachedAhead row;
        row.route = liveness_route_of(NetwEntity::of(node).ptr());
        row.from_owner = owner->get_path_to(node->get_parent());
        moved.push_back(row);
        spawn_replace_keeping_pose(node, above);
    }
    if (route > 0 && !moved.is_empty()) {
        lifecycle_detached_ahead[route] = moved;
    }
}

void NetwMultiplayer::lifecycle_restore_detached(int64_t p_route) {
    HashMap<int64_t, LocalVector<DetachedAhead>>::Iterator found
        = lifecycle_detached_ahead.find(p_route);
    if (!found) {
        return;
    }
    Node *owner = liveness_node_of(p_route);
    if (owner == nullptr) {
        liveness_when_live(
            p_route,
            callable_mp(this, &NetwMultiplayer::lifecycle_restore_detached)
                .bind(p_route),
            0,
            Callable()
        );
        return;
    }
    for (const DetachedAhead &row : found->value) {
        Node *node = liveness_node_of(row.route);
        Node *parent = owner->get_node_or_null(row.from_owner);
        if (node != nullptr && parent != nullptr
            && node->get_parent() != parent) {
            spawn_replace_keeping_pose(node, parent);
        }
        if (node != nullptr
            && row.session_revision > anchor_installed(row.route).revision) {
            entity_install_anchor(
                row.route,
                row.session_revision,
                row.session_author
            );
        }
    }
    lifecycle_detached_ahead.remove(found);
}

bool NetwMultiplayer::lifecycle_note_detached_anchor(
    int64_t p_route,
    uint64_t p_revision,
    uint64_t p_author
) {
    bool noted = false;
    for (KeyValue<int64_t, LocalVector<DetachedAhead>> &dying :
         lifecycle_detached_ahead) {
        for (DetachedAhead &row : dying.value) {
            if (row.route == p_route && p_revision > row.session_revision) {
                row.session_revision = p_revision;
                row.session_author = p_author;
                noted = true;
            }
        }
    }
    return noted;
}

bool NetwMultiplayer::lifecycle_admit_move(
    int64_t p_sender,
    const PackedByteArray &p_payload
) {
    ControllerOp op;
    if (!read_controller_op(this, lifecycle::Kind::REPARENT, p_payload, op)) {
        return true;
    }
    Node *owner = liveness_node_of(op.route);
    Node *parent = anchor_resolve(op.anchor);
    if (owner == nullptr || parent == nullptr) {
        lifecycle_decide(
            op.route,
            p_sender,
            lifecycle::Kind::REPARENT,
            op.base,
            ERR_INVALID_PARAMETER
        );
        return true;
    }
    String reason;
    if (lifecycle_filter_denies(
            lifecycle::Kind::REPARENT,
            p_sender,
            wrapper_for_route(op.route).ptr(),
            parent,
            reason
        )) {
        lifecycle_decide(
            op.route,
            p_sender,
            lifecycle::Kind::REPARENT,
            op.base,
            ERR_UNAUTHORIZED,
            reason
        );
        return false;
    }
    if (spawn_reparent_node(
            owner,
            parent,
            Callable(),
            op.route,
            op.base + 1,
            uint64_t(p_sender),
            p_sender
        )) {
        return true;
    }
    if (spawn::Pipeline *spawns = spawn_plane()) {
        spawns->settle_move(op.route, p_sender);
    }
    lifecycle_decide(
        op.route,
        p_sender,
        lifecycle::Kind::REPARENT,
        op.base,
        OK
    );
    return true;
}

void NetwMultiplayer::lifecycle_decide(
    int64_t p_route,
    int64_t p_author,
    lifecycle::Kind p_kind,
    uint64_t p_base,
    Error p_code,
    const String &p_reason
) {
    if (p_author <= 0 || p_author == get_unique_id()) {
        return;
    }
    AnchorRevision standing = anchor_standing(p_route);
    uint64_t kind = uint64_t(p_kind);
    bool accepted = p_code == OK;
    uint64_t base = p_base;
    wire::WriteStream stream;
    bool written = verb_head_write(stream, p_route) && stream.bits(kind, 2)
        && stream.bool1(accepted) && stream.varuint(base, 5)
        && stream.varuint(standing.revision, 5)
        && stream.varuint(standing.author, 5);
    if (written && !accepted) {
        uint64_t reason = uint64_t(p_code);
        Node *held = spawn_carry_pending_parent(p_route);
        if (held == nullptr) {
            Node *owner = liveness_node_of(p_route);
            held = owner != nullptr ? owner->get_parent() : nullptr;
        }
        wire::WriteStream probe;
        bool anchored = held != nullptr && anchor_encode(probe, held);
        String told = p_reason;
        written = stream.varuint(reason, 2) && stream.bool1(anchored)
            && (!anchored || anchor_encode(stream, held))
            && wire::string_field(stream, told);
    }
    if (!written || !stream.align_verify()) {
        return;
    }
    attribution_note_subject(p_route);
    send_to(
        p_author,
        0,
        lifecycle_decision_channel,
        stream.to_bytes(),
        true,
        0,
        String(),
        true
    );
}

void NetwMultiplayer::lifecycle_receive_decision(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (p_sender != session_authority_peer() || is_session_authority()) {
        return;
    }
    wire::ReadStream reader(p_payload);
    int64_t route = 0;
    int64_t epoch = 0;
    uint64_t kind = 0;
    bool accepted = false;
    uint64_t base = 0;
    AnchorRevision standing;
    uint64_t code_named = 0;
    bool anchored = false;
    Dictionary anchor;
    String reason;
    if (!verb_head_read(reader, route, epoch) || !reader.bits(kind, 2)
        || !reader.bool1(accepted) || !reader.varuint(base, 5)
        || !reader.varuint(standing.revision, 5)
        || !reader.varuint(standing.author, 5)) {
        return;
    }
    if (!accepted
        && (!reader.varuint(code_named, 2) || !reader.bool1(anchored)
            || (anchored && !anchor_decode(reader, anchor))
            || !wire::string_field(reader, reason))) {
        return;
    }
    if (!reader.align_verify() || reader.bits_remaining() != 0) {
        return;
    }
    const lifecycle::Kind decided = lifecycle::Kind(kind);
    if (decided == lifecycle::Kind::DESPAWN) {
        if (accepted) {
            lifecycle_detached_ahead.erase(route);
        } else {
            lifecycle_restore_detached(route);
        }
    }
    const Ref<NetwEntity> entity = wrapper_for_route(route);
    if (entity.is_null()) {
        return;
    }
    if (accepted) {
        entity->structure_ops_accept(standing.revision);
        return;
    }
    if (decided != lifecycle::Kind::REPARENT) {
        lifecycle_notify_refused(entity.ptr(), decided, reason);
        return;
    }
    const Error code = Error(int(code_named));
    const String detail = !reason.is_empty()
        ? reason
        : vformat(
              "Netw.reparent: the session "
              "authority refused the move "
              "of '%s' (%s), so it holds "
              "the session's structure",
              root_name(entity->get_owner()),
              error_named(code)
          );
    if (!entity->structure_ops_refuse(base, code, detail)) {
        return;
    }
    lifecycle_notify_refused(entity.ptr(), decided, reason);
    lifecycle_undo(
        entity.ptr(),
        anchored ? anchor_resolve(anchor) : nullptr,
        standing
    );
}

void NetwMultiplayer::entity_settle_foreign_move(const EntityDeparture &p_row) {
    if (!p_row.received || p_row.applied || p_row.wrapper.is_null()) {
        return;
    }
    if (p_row.minted) {
        lifecycle_send_move(p_row);
        return;
    }
    Node *owner = p_row.wrapper->get_owner();
    Node *parent = owner != nullptr ? owner->get_parent() : nullptr;
    if (parent == nullptr || gd::instance_id(parent) == p_row.parent_id) {
        return;
    }
    lifecycle::Facts facts
        = lifecycle_local_facts(p_row.wrapper.ptr(), lifecycle::Kind::REPARENT);
    facts.destination = lifecycle_destination(owner, parent);
    lifecycle::Ruling ruling = lifecycle::rule(facts);
    if (ruling.admitted()) {
        lifecycle_undo_ungranted_move(p_row, facts);
        return;
    }
    String text;
    if (ruling.verdict == lifecycle::Verdict::STATIC) {
        text = lifecycle::refusal_text(
            String("Node.reparent"),
            root_name(owner),
            facts,
            ruling
        );
    }
    if (text.is_empty() || !p_row.wrapper->note_raw_move_warning()) {
        return;
    }
    NETW_WARN(
        sys::ENTITY,
        "%s The move stays on this peer, and the session keeps its copy "
        "where it was.",
        text
    );
}

void NetwMultiplayer::lifecycle_undo_ungranted_move(
    const EntityDeparture &p_row,
    lifecycle::Facts p_facts
) {
    p_facts.peer_is_controller = false;
    p_facts.peer_claim = lifecycle::Claim::REQUESTABLE;
    lifecycle::Ruling refused;
    refused.verdict = lifecycle::Verdict::STATIC;
    refused.code = ERR_UNAUTHORIZED;
    const String text = lifecycle::refusal_text(
        String("Node.reparent"),
        root_name(p_row.wrapper->get_owner()),
        p_facts,
        refused
    );
    NETW_WARN(
        sys::ENTITY,
        "%s It moved before the grant reached this peer, so it is moved back.",
        text
    );
    lifecycle_undo(
        p_row.wrapper.ptr(),
        Object::cast_to<Node>(gd::object_of(p_row.parent_id)),
        anchor_installed(p_row.route)
    );
}

} // namespace netw
