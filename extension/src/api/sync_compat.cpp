#include "netw/api/sync_compat.hpp"

#include "godot/class_db.hpp"
#include "godot/resource.hpp"
#include "godot/time.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/record.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_CHANGED = "changed";
const char *SIG_VISIBILITY_CHANGED = "visibility_changed";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_SYNCHRONIZED = "synchronized";
const char *SIG_DELTA_SYNCHRONIZED = "delta_synchronized";

bool throttle_elapsed(int64_t p_last_usec, double p_interval, int64_t p_now) {
    if (p_interval <= 0.0 || p_last_usec < 0) {
        return true;
    }
    return p_now - p_last_usec >= int64_t(p_interval * 1000000.0);
}

Array read_path(Node *p_root, const NodePath &p_path) {
    Array out;
    Node *target = p_root;
    const NodePath names = NodePath(p_path.get_concatenated_names());
    if (!names.is_empty() && target != nullptr) {
        target = target->get_node_or_null(names);
    }
    if (target == nullptr) {
        out.push_back(false);
        out.push_back(Variant());
        return out;
    }
    out.push_back(true);
    out.push_back(
        gd::get_indexed(
            target,
            NodePath(String(":") + String(p_path.get_concatenated_subnames()))
        )
    );
    return out;
}

void write_path(Node *p_root, const NodePath &p_path, const Variant &p_value) {
    Node *target = p_root;
    const NodePath names = NodePath(p_path.get_concatenated_names());
    if (!names.is_empty() && target != nullptr) {
        target = target->get_node_or_null(names);
    }
    if (target == nullptr) {
        return;
    }
    gd::set_indexed(
        target,
        NodePath(String(":") + String(p_path.get_concatenated_subnames())),
        p_value
    );
}

} // namespace

MultiplayerSynchronizer *SyncCompat::Consumed::sync_node() const {
    return Object::cast_to<MultiplayerSynchronizer>(gd::instance_from_id(sync));
}

Node *SyncCompat::Consumed::root_node() const {
    return Object::cast_to<Node>(gd::instance_from_id(root));
}

SyncCompat::~SyncCompat() {
    for (uint32_t at = 0; at < rows.size(); ++at) {
        memdelete(rows[at]);
    }
    rows.clear();
}

NetwMultiplayer *SyncCompat::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

void SyncCompat::set_core(Object *p_core) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_core);
    core_id = gd::instance_id(plane);
    if (plane == nullptr) {
        return;
    }
    const Callable live
        = callable_mp(plane, &NetwMultiplayer::sync_compat_entity_live);
    if (!plane->is_connected(SIG_ENTITY_LIVE, live)) {
        plane->connect(SIG_ENTITY_LIVE, live);
    }
}

SyncCompat *NetwMultiplayer::sync_adapter() const {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->get_sync_compat() : nullptr;
}

void NetwMultiplayer::sync_compat_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (SyncCompat *adapter = sync_adapter()) {
        adapter->on_entity_live(p_route, p_entity);
    }
}

void NetwMultiplayer::sync_compat_visibility_changed(
    int64_t p_peer,
    Node *p_root
) {
    if (SyncCompat *adapter = sync_adapter()) {
        adapter->on_sync_visibility_changed(p_peer, p_root);
    }
}

void NetwMultiplayer::sync_compat_config_changed() {
    if (SyncCompat *adapter = sync_adapter()) {
        adapter->on_config_changed();
    }
}

Error NetwMultiplayer::sync_compat_decode_stage_body() {
    SyncCompat *adapter = sync_adapter();
    return adapter != nullptr ? adapter->decode_stage_body() : ERR_UNCONFIGURED;
}

Array NetwMultiplayer::sync_compat_gather_stage() {
    SyncCompat *adapter = sync_adapter();
    return adapter != nullptr ? adapter->gather_stage() : Array();
}

Error NetwMultiplayer::sync_compat_apply_stage(const Array &p_staged) {
    SyncCompat *adapter = sync_adapter();
    return adapter != nullptr ? adapter->apply_stage(p_staged)
                              : ERR_UNCONFIGURED;
}

void SyncCompat::set_sync_model(NetwSyncModel *p_model) {
    sync_model = p_model;
}

void SyncCompat::set_spawn_book(spawn::Book *p_book) {
    spawn_book = p_book;
}

void SyncCompat::set_stage_seams(
    const Callable &p_encode,
    const Callable &p_decode
) {
    encode_stage = p_encode;
    decode_stage = p_decode;
}

void SyncCompat::set_spawn_seams(
    const Callable &p_adopt,
    const Callable &p_sweep
) {
    adopt_seam = p_adopt;
    sweep_seam = p_sweep;
}

void SyncCompat::set_channels(int64_t p_sync, int64_t p_sync_delta) {
    channel_sync = p_sync;
    channel_sync_delta = p_sync_delta;
}

SyncCompat::Consumed *SyncCompat::row_of(Object *p_sync) const {
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at]->sync_node() == p_sync) {
            return rows[at];
        }
    }
    return nullptr;
}

void SyncCompat::drop_row(uint32_t p_at) {
    Consumed *row = rows[p_at];
    drop_declaration(row);
    disconnect_config(row);
    watch_book.reset(row->id);
    rows.remove_at(p_at);
    memdelete(row);
}

Error SyncCompat::consume(Node *p_root, Object *p_sync) {
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(p_sync);
    NetwMultiplayer *plane = core();
    if (p_root == nullptr || sync == nullptr || plane == nullptr) {
        return ERR_INVALID_PARAMETER;
    }
    const Callable watcher
        = callable_mp(plane, &NetwMultiplayer::sync_compat_visibility_changed)
              .bind(p_root);
    if (!sync->is_connected(SIG_VISIBILITY_CHANGED, watcher)) {
        sync->connect(SIG_VISIBILITY_CHANGED, watcher);
    }
    if (row_of(sync) != nullptr) {
        return OK;
    }

    Consumed *row = memnew(Consumed);
    row->id = next_row_id++;
    row->sync = gd::instance_id(sync);
    row->root = gd::instance_id(p_root);
    refresh_row(row);
    rows.push_back(row);
    authoring::apply(p_root, sync);
    capture_declaration(row);
    refresh_row_intent(row);
    refresh_interest_intent(p_root);
    return OK;
}

Error SyncCompat::consume_remove(Node *p_root, Object *p_sync) {
    MultiplayerSynchronizer *sync
        = Object::cast_to<MultiplayerSynchronizer>(p_sync);
    NetwMultiplayer *plane = core();
    if (sync != nullptr && plane != nullptr) {
        const Callable watcher
            = callable_mp(
                  plane,
                  &NetwMultiplayer::sync_compat_visibility_changed
            )
                  .bind(p_root);
        if (sync->is_connected(SIG_VISIBILITY_CHANGED, watcher)) {
            sync->disconnect(SIG_VISIBILITY_CHANGED, watcher);
        }
    }
    for (uint32_t at = rows.size(); at > 0; --at) {
        if (rows[at - 1]->sync_node() == sync) {
            drop_row(at - 1);
        }
    }
    refresh_interest_intent(p_root);
    return OK;
}

void SyncCompat::on_sync_visibility_changed(int64_t, Node *p_root) {
    if (core() == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at]->root_node() == p_root) {
            refresh_row_intent(rows[at]);
        }
    }
    refresh_interest_intent(p_root);
    if (sweep_seam.is_valid()) {
        sweep_seam.call();
    }
}

void SyncCompat::refresh_interest_intents() {
    LocalVector<Node *> roots;
    for (uint32_t at = 0; at < rows.size(); ++at) {
        Node *root = rows[at]->root_node();
        if (root == nullptr) {
            continue;
        }
        refresh_row_intent(rows[at]);
        if (!roots.has(root)) {
            roots.push_back(root);
        }
    }
    for (uint32_t at = 0; at < roots.size(); ++at) {
        refresh_interest_intent(roots[at]);
    }
}

void SyncCompat::refresh_interest_intent(Node *p_root) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !plane->is_server() || p_root == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_root);
    if (entity.is_null()) {
        return;
    }
    Node *entity_root = entity->get_owner();
    if (entity_root == nullptr) {
        return;
    }
    const PackedInt64Array known = plane->interest_known_peers();
    PackedInt64Array admitted;
    for (int at = 0; at < known.size(); ++at) {
        if (synchronizer_verdict(known[at], entity_root)) {
            admitted.push_back(known[at]);
        }
    }
    plane->interest_set_entity_intent(entity, admitted);
}

void SyncCompat::refresh_row_intent(Consumed *p_row) {
    NetwMultiplayer *plane = core();
    Node *root = p_row->root_node();
    if (plane == nullptr || root == nullptr) {
        return;
    }
    PackedInt64Array peer_ids;
    if (plane->is_server()) {
        peer_ids = plane->interest_known_peers();
    } else if (
        plane->session_get_inner().is_valid()
        && plane->session_get_inner()->get_multiplayer_peer().is_valid()
    ) {
        const PackedInt32Array peers
            = gd::api_peer_ids(plane->session_get_inner());
        for (int at = 0; at < peers.size(); ++at) {
            peer_ids.push_back(peers[at]);
        }
    }
    p_row->intent_by_peer.clear();
    for (int at = 0; at < peer_ids.size(); ++at) {
        p_row->intent_by_peer.insert(
            peer_ids[at],
            synchronizer_verdict(peer_ids[at], root)
        );
    }
}

bool SyncCompat::config_still_held(
    const ObjectID &p_config,
    Consumed *p_except
) const {
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at] != p_except && rows[at]->config == p_config) {
            return true;
        }
    }
    return false;
}

void SyncCompat::disconnect_config(Consumed *p_row) {
    if (!p_row->config.is_valid()) {
        return;
    }
    Object *cfg = gd::instance_from_id(p_row->config);
    const ObjectID held = p_row->config;
    p_row->config = ObjectID();
    NetwMultiplayer *plane = core();
    if (cfg == nullptr || plane == nullptr || config_still_held(held, p_row)) {
        return;
    }
    const Callable hook
        = callable_mp(plane, &NetwMultiplayer::sync_compat_config_changed);
    if (cfg->is_connected(SIG_CHANGED, hook)) {
        cfg->disconnect(SIG_CHANGED, hook);
    }
}

void SyncCompat::on_config_changed() {
    for (uint32_t at = 0; at < rows.size(); ++at) {
        rows[at]->config_dirty = true;
    }
}

void SyncCompat::refresh_row(Consumed *p_row) {
    MultiplayerSynchronizer *sync = p_row->sync_node();
    Ref<SceneReplicationConfig> cfg;
    if (sync != nullptr) {
        cfg = sync->get_replication_config();
    }
    const ObjectID wanted = gd::instance_id(cfg.ptr());
    if (wanted == p_row->config && !p_row->config_dirty) {
        return;
    }
    disconnect_config(p_row);
    NetwMultiplayer *plane = core();
    if (cfg.is_valid() && plane != nullptr) {
        p_row->config = wanted;
        const Callable hook
            = callable_mp(plane, &NetwMultiplayer::sync_compat_config_changed);
        if (!cfg->is_connected(SIG_CHANGED, hook)) {
            cfg->connect(SIG_CHANGED, hook);
        }
    }
    p_row->config_dirty = false;
    p_row->sync_paths.clear();
    LocalVector<NodePath> watch;
    if (cfg.is_valid()) {
        const TypedArray<NodePath> paths = cfg->get_properties();
        for (int at = 0; at < paths.size(); ++at) {
            const NodePath path = paths[at];
            if (path.get_subname_count() == 0) {
                continue;
            }
            const int mode = int(cfg->property_get_replication_mode(path));
            if (mode == SceneReplicationConfig::REPLICATION_MODE_ALWAYS) {
                p_row->sync_paths.push_back(path);
            } else if (
                mode == SceneReplicationConfig::REPLICATION_MODE_ON_CHANGE
            ) {
                watch.push_back(path);
            }
        }
    }
    if (int(watch.size()) > WATCH_LIMIT) {
        if (!p_row->poisoned) {
            poison(
                p_row,
                0,
                vformat(
                    "watches %d properties, above the 64-bit delta mask limit",
                    int(watch.size())
                )
            );
        }
        watch.resize(WATCH_LIMIT);
    }
    bool same_watch = watch.size() == p_row->watch_paths.size();
    for (uint32_t at = 0; same_watch && at < watch.size(); ++at) {
        same_watch = watch[at] == p_row->watch_paths[at];
    }
    if (!same_watch) {
        p_row->watch_paths.clear();
        for (uint32_t at = 0; at < watch.size(); ++at) {
            p_row->watch_paths.push_back(watch[at]);
        }
        watch_book.reset(p_row->id);
    }
    p_row->schema_hash = schema_fingerprint(p_row);
    declare_model_row(p_row);
}

int64_t SyncCompat::schema_fingerprint(const Consumed *p_row) {
    PackedStringArray parts;
    for (uint32_t at = 0; at < p_row->sync_paths.size(); ++at) {
        parts.push_back(String("s") + String(p_row->sync_paths[at]));
    }
    for (uint32_t at = 0; at < p_row->watch_paths.size(); ++at) {
        parts.push_back(String("w") + String(p_row->watch_paths[at]));
    }
    return int64_t(uint32_t(String("|").join(parts).hash()));
}

void SyncCompat::poison(
    Consumed *p_row,
    int64_t p_route,
    const String &p_reason
) {
    p_row->poisoned = true;
    MultiplayerSynchronizer *sync = p_row->sync_node();
    NETW_ERROR(
        sys::WIRE,
        "consumed synchronizer '%s' (route %d) poisoned: %s. Its replication "
        "config must be identical on every peer.",
        sync != nullptr ? String(sync->get_name()) : String("<freed>"),
        p_route,
        p_reason
    );
}

void SyncCompat::validate_schema(Consumed *p_row, int64_t p_route) {
    if (p_row->schema_checked) {
        return;
    }
    const HashMap<int64_t, Dictionary>::Iterator found
        = pending_schema.find(p_route);
    if (!found || found->value.is_empty()) {
        return;
    }
    p_row->schema_checked = true;
    const int64_t ordinal = ordinal_of(p_row);
    if (!found->value.has(ordinal)) {
        return;
    }
    const int64_t declared = int64_t(found->value[ordinal]);
    if (declared != p_row->schema_hash) {
        poison(
            p_row,
            p_route,
            vformat(
                "schema hash %08x disagrees with the spawn descriptor %08x",
                p_row->schema_hash,
                declared
            )
        );
    }
}

int64_t SyncCompat::ordinal_of(const Consumed *p_row) const {
    if (sync_model == nullptr) {
        return -1;
    }
    const repl::SetRow *row = sync_model->row_for(
        p_row->route,
        NetwSyncModel::KIND_CONSUMED,
        p_row->order_key,
        0
    );
    return row != nullptr ? row->ordinal : -1;
}

SyncCompat::Consumed *SyncCompat::row_by_ordinal(
    int64_t p_route,
    int64_t p_ordinal
) const {
    if (sync_model == nullptr || p_ordinal < 0) {
        return nullptr;
    }
    const repl::SetRow *row = sync_model->row(p_route, p_ordinal);
    if (row == nullptr || row->kind != NetwSyncModel::KIND_CONSUMED) {
        return nullptr;
    }
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at]->route == p_route && rows[at]->order_key == row->key) {
            return rows[at];
        }
    }
    return nullptr;
}

void SyncCompat::capture_declaration(Consumed *p_row) {
    NetwMultiplayer *plane = core();
    Node *root = p_row->root_node();
    MultiplayerSynchronizer *sync = p_row->sync_node();
    if (plane == nullptr || root == nullptr || sync == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(root);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        return;
    }
    const RID rid = entity->get_rid_handle();
    const int64_t route = plane->get_liveness_core()->route_of(rid);
    p_row->route = route > 0 ? route : entity->get_route();
    p_row->entity = rid;
    p_row->order_key
        = StringName(String(entity->get_owner()->get_path_to(sync)));
    p_row->comp = entity->comp_of(root);
    declare_model_row(p_row);
}

void SyncCompat::declare_model_row(const Consumed *p_row) {
    if (sync_model == nullptr || p_row->route <= 0
        || String(p_row->order_key).is_empty()) {
        return;
    }
    sync_model->declare(
        p_row->route,
        NetwSyncModel::KIND_CONSUMED,
        p_row->order_key,
        p_row->comp,
        RID(),
        0,
        p_row->schema_hash,
        0,
        0
    );
}

void SyncCompat::drop_declaration(Consumed *p_row) {
    if (sync_model != nullptr && p_row->route > 0
        && !String(p_row->order_key).is_empty()) {
        sync_model->drop(
            p_row->route,
            NetwSyncModel::KIND_CONSUMED,
            p_row->order_key,
            0
        );
    }
    p_row->route = 0;
}

void SyncCompat::on_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    for (uint32_t at = 0; at < rows.size(); ++at) {
        Node *root = rows[at]->root_node();
        if (root == nullptr) {
            continue;
        }
        const Ref<NetwEntity> found = NetwEntity::of(root);
        if (found.is_valid() && p_entity.is_valid()
            && found->get_rid_handle() == p_entity->get_rid_handle()) {
            rows[at]->route = p_route;
            capture_declaration(rows[at]);
        }
    }
}

void SyncCompat::prune() {
    for (uint32_t at = rows.size(); at > 0; --at) {
        if (rows[at - 1]->sync_node() == nullptr) {
            drop_row(at - 1);
        }
    }
}

bool SyncCompat::synchronizer_verdict(int64_t p_peer_id, Node *p_node) {
    NetwMultiplayer *plane = core();
    int64_t local_id = 1;
    if (plane != nullptr && plane->session_get_inner().is_valid()
        && plane->session_get_inner()->get_multiplayer_peer().is_valid()) {
        local_id = plane->get_unique_id();
    }
    return synchronizers::visibility_verdict(p_node, p_peer_id, local_id);
}

PackedInt32Array SyncCompat::recipients_for(
    Consumed *p_row,
    const Ref<NetwEntity> &p_entity,
    int64_t p_route,
    Node *p_root
) {
    PackedInt32Array out;
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return out;
    }
    const int64_t local_id = plane->get_unique_id();
    if (!plane->has_server_role()) {
        const PackedInt32Array peers
            = gd::api_peer_ids(plane->session_get_inner());
        for (int at = 0; at < peers.size(); ++at) {
            if (peers[at] == local_id) {
                continue;
            }
            if (!synchronizer_verdict(peers[at], p_root)) {
                continue;
            }
            out.push_back(peers[at]);
        }
        return out;
    }
    if (spawn_book == nullptr) {
        return out;
    }
    if (spawn_book->has_armed(p_route)) {
        return out;
    }
    PackedInt32Array base;
    const spawn::Record *record = spawn_book->spawned_of(p_route);
    if (record != nullptr) {
        const RID rid
            = plane->get_liveness_core()->rid_from_route(int(p_route));
        if (plane->get_liveness_core()->state_of(rid)
            != NetwLivenessCore::STATE_LIVE) {
            return out;
        }
        base = record->recipients();
    } else {
        base = plane->rpc_get_recipients(p_entity);
    }
    const bool filtered = plane->interest_entity_has_filter(p_entity);
    for (int at = 0; at < base.size(); ++at) {
        const int64_t peer_id = base[at];
        if (peer_id == local_id) {
            continue;
        }
        if (filtered && !plane->interest_wire_admits(peer_id, p_entity)) {
            continue;
        }
        const HashMap<int64_t, bool>::ConstIterator intent
            = p_row->intent_by_peer.find(peer_id);
        if (!intent || !intent->value) {
            continue;
        }
        out.push_back(int32_t(peer_id));
    }
    return out;
}

void SyncCompat::pump() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->session_get_inner().is_null()
        || plane->session_get_inner()->get_multiplayer_peer().is_null()) {
        return;
    }
    const Time *reading = Time::get_singleton();
    const int64_t now
        = reading != nullptr ? int64_t(reading->get_ticks_usec()) : 0;

    prune();

    if (plane->session_get_role() == NetwMultiplayer::ROLE_NONE) {
        return;
    }
    const bool host = plane->is_host();

    LocalVector<Consumed *> pass;
    for (uint32_t at = 0; at < rows.size(); ++at) {
        pass.push_back(rows[at]);
    }
    for (uint32_t at = 0; at < pass.size(); ++at) {
        Consumed *row = pass[at];
        MultiplayerSynchronizer *sync = row->sync_node();
        Node *root = row->root_node();
        if (sync == nullptr || root == nullptr || !sync->is_inside_tree()) {
            continue;
        }
        refresh_row(row);
        if (row->poisoned) {
            continue;
        }
        Ref<NetwEntity> entity = NetwEntity::of(root);
        if (entity.is_null() && host && !row->adopt_attempted) {
            if (adopt_seam.is_valid()) {
                adopt_seam.call(root);
            }
            row->adopt_attempted = NetwEntity::of(root).is_valid();
            continue;
        }
        if (entity.is_null()) {
            continue;
        }
        if (row->sync_paths.is_empty() && row->watch_paths.is_empty()) {
            continue;
        }
        if (!sync->is_multiplayer_authority()) {
            continue;
        }
        if (row->route <= 0) {
            capture_declaration(row);
        }
        const int64_t route = row->route;
        if (route <= 0) {
            continue;
        }
        const PackedInt32Array recipients
            = recipients_for(row, entity, route, root);
        if (recipients.is_empty()) {
            continue;
        }

        if (!row->sync_paths.is_empty()
            && throttle_elapsed(
                row->last_sync_usec,
                sync->get_replication_interval(),
                now
            )) {
            const PackedByteArray stock = encode_sync_frame(row, route);
            if (!stock.is_empty()) {
                row->last_sync_usec = now;
                for (int peer = 0; peer < recipients.size(); ++peer) {
                    const PackedByteArray payload
                        = run_encode_stage(recipients[peer], stock);
                    if (payload.is_empty()) {
                        continue;
                    }
                    plane->send_to(
                        recipients[peer],
                        route,
                        channel_sync,
                        payload,
                        false,
                        0,
                        String(),
                        true
                    );
                    sync_frames_out += 1;
                }
            }
        }

        if (!row->watch_paths.is_empty()
            && throttle_elapsed(
                row->last_watch_usec,
                sync->get_delta_interval(),
                now
            )) {
            row->last_watch_usec = now;
            poll_watchers(row);
            send_deltas(row, route, recipients);
        }

        watch_book.retain_baselines(row->id, recipients);
    }
}

PackedByteArray SyncCompat::encode_sync_frame(
    Consumed *p_row,
    int64_t p_route
) {
    Array readable;
    const Array values
        = gather_paths(p_row, p_row->sync_paths, false, readable);
    if (values.size() != int(p_row->sync_paths.size())) {
        return PackedByteArray();
    }
    return sync_kernel::encode_volatile(ordinal_of(p_row), values);
}

void SyncCompat::poll_watchers(Consumed *p_row) {
    Array readable;
    const Array values
        = gather_paths(p_row, p_row->watch_paths, true, readable);
    watch_book.poll(p_row->id, values, readable);
}

void SyncCompat::send_deltas(
    Consumed *p_row,
    int64_t p_route,
    const PackedInt32Array &p_recipients
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !watch_book.is_inited(p_row->id)) {
        return;
    }
    const int64_t ordinal = ordinal_of(p_row);
    for (int at = 0; at < p_recipients.size(); ++at) {
        const int64_t peer_id = p_recipients[at];
        uint64_t mask = 0;
        Array values;
        watch_book.mask_for(p_row->id, peer_id, mask, values);
        watch_book.commit(p_row->id, peer_id);
        if (mask == 0) {
            continue;
        }
        const PackedByteArray stock
            = sync_kernel::encode_retained(ordinal, int64_t(mask), values);
        const PackedByteArray bytes = run_encode_stage(peer_id, stock);
        if (bytes.is_empty()) {
            continue;
        }
        plane->send_to(
            peer_id,
            p_route,
            channel_sync_delta,
            bytes,
            true,
            0,
            String(),
            true
        );
        delta_frames_out += 1;
    }
}

PackedByteArray SyncCompat::run_encode_stage(
    int64_t p_peer,
    const PackedByteArray &p_stock
) {
    PackedByteArray bytes = p_stock;
    if (encode_stage.is_valid()) {
        bytes = encode_stage.call(p_peer, p_stock, int64_t(-1));
    }
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->event_wants(EventPlane::SYNC_ENCODE, 0)) {
        Dictionary detail;
        detail[StringName("bytes")] = bytes.size();
        plane->event_emit(
            EventPlane::SYNC_ENCODE,
            0,
            detail,
            StringName(),
            p_peer,
            OK,
            Dictionary()
        );
    }
    return bytes;
}

Error SyncCompat::decode_stage_body() {
    stage_decoded = stage_retained
        ? sync_kernel::decode_retained(stage_payload, stage_keys)
        : sync_kernel::decode_volatile(stage_payload, stage_keys);
    return stage_decoded.is_valid() ? OK : ERR_INVALID_DATA;
}

Error SyncCompat::run_decode_stage(
    const RID &p_entity,
    int64_t p_comp,
    int64_t p_flags,
    const PackedByteArray &p_payload
) {
    NetwMultiplayer *plane = core();
    Error verdict = OK;
    if (!decode_stage.is_valid()) {
        verdict = decode_stage_body();
    } else {
        verdict = Error(
            int(decode_stage.call(
                p_entity,
                p_comp,
                p_flags,
                -1,
                p_payload,
                callable_mp(
                    plane,
                    &NetwMultiplayer::sync_compat_decode_stage_body
                )
            ))
        );
    }
    if (plane != nullptr) {
        const int64_t route = plane->get_liveness_core()->route_of(p_entity);
        if (plane->event_wants(EventPlane::SYNC_DECODE, route)) {
            Dictionary detail;
            detail[StringName("comp")] = p_comp;
            plane->event_emit(
                EventPlane::SYNC_DECODE,
                route,
                detail,
                StringName(),
                0,
                verdict,
                Dictionary()
            );
        }
    }
    return verdict;
}

Array SyncCompat::gather_stage() {
    Array values;
    if (stage_paths == nullptr) {
        return values;
    }
    for (uint32_t at = 0; at < stage_paths->size(); ++at) {
        const Array read = read_path(stage_root, (*stage_paths)[at]);
        const bool resolved = read[0];
        stage_readable.push_back(resolved);
        if (!resolved && !stage_allow_missing) {
            return Array();
        }
        values.push_back(resolved ? read[1] : Variant());
    }
    return values;
}

Array SyncCompat::gather_paths(
    Consumed *p_row,
    const LocalVector<NodePath> &p_paths,
    bool p_allow_missing,
    Array &r_readable
) {
    NetwMultiplayer *plane = core();
    stage_root = p_row->root_node();
    stage_paths = &p_paths;
    stage_allow_missing = p_allow_missing;
    stage_readable = Array();
    Array values;
    if (plane != nullptr) {
        values = plane->run_gather_set(
            p_row->entity,
            p_row->comp,
            callable_mp(plane, &NetwMultiplayer::sync_compat_gather_stage)
        );
    } else {
        values = gather_stage();
    }
    r_readable = stage_readable;
    stage_paths = nullptr;
    stage_root = nullptr;
    if (r_readable.size() != int(p_paths.size())
        && values.size() == int(p_paths.size())) {
        r_readable.clear();
        for (uint32_t at = 0; at < p_paths.size(); ++at) {
            r_readable.push_back(true);
        }
    }
    return values;
}

Error SyncCompat::apply_stage(const Array &p_staged) {
    if (stage_paths == nullptr || p_staged.size() != int(stage_paths->size())) {
        return ERR_INVALID_DATA;
    }
    for (uint32_t at = 0; at < stage_paths->size(); ++at) {
        write_path(stage_root, (*stage_paths)[at], p_staged[at]);
    }
    return OK;
}

Error SyncCompat::apply_paths(
    Consumed *p_row,
    const LocalVector<NodePath> &p_paths,
    const Array &p_values
) {
    NetwMultiplayer *plane = core();
    stage_root = p_row->root_node();
    stage_paths = &p_paths;
    Error verdict = OK;
    if (plane != nullptr) {
        verdict = plane->run_apply_set(
            p_row->entity,
            p_row->comp,
            p_values,
            callable_mp(plane, &NetwMultiplayer::sync_compat_apply_stage)
        );
    } else {
        verdict = apply_stage(p_values);
    }
    stage_paths = nullptr;
    stage_root = nullptr;
    return verdict;
}

void SyncCompat::handle_sync(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_entity.is_null()) {
        return;
    }
    const int64_t route = plane->liveness_route_of(p_entity.ptr());
    wire::ReadStream reader(p_payload);
    sync_kernel::VolatileHead head;
    if (!sync_kernel::VolatileHead::wire.run(reader, head)) {
        drops_sync_no_set += 1;
        return;
    }
    Consumed *row = row_by_ordinal(route, int64_t(head.ordinal));
    if (row == nullptr) {
        drops_sync_no_set += 1;
        return;
    }
    refresh_row(row);
    validate_schema(row, route);
    if (row->poisoned) {
        drops_sync_poisoned += 1;
        return;
    }
    MultiplayerSynchronizer *sync = row->sync_node();
    if (sync == nullptr || p_sender != sync->get_multiplayer_authority()) {
        drops_sync_bad_sender += 1;
        return;
    }
    const int64_t flags = int64_t(head.flags);
    if (flags != sync_kernel::RESERVED) {
        drops_sync_unknown_flag += 1;
        NETW_WARN(
            sys::WIRE,
            "consumed synchronizer '%s' SYNC flags %d unimplemented, frame "
            "dropped. Peers must run the same Networked version.",
            sync->get_name(),
            flags
        );
        return;
    }

    stage_keys = Array();
    for (uint32_t at = 0; at < row->sync_paths.size(); ++at) {
        stage_keys.push_back(StringName(String(row->sync_paths[at])));
    }
    stage_payload = p_payload;
    stage_retained = false;
    stage_decoded = StagedWrites();
    const Error verdict = run_decode_stage(
        p_entity->get_rid_handle(),
        row->comp,
        flags,
        p_payload
    );
    const StagedWrites staged = stage_decoded;
    stage_decoded = StagedWrites();
    if (verdict != OK || !staged.is_valid()
        || staged.values.size() != int(row->sync_paths.size())) {
        drops_sync_poisoned += 1;
        poison(row, route, "SYNC row size mismatch");
        return;
    }
    if (apply_paths(row, row->sync_paths, staged.values) != OK) {
        return;
    }
    sync_frames_in += 1;
    sync->emit_signal(SIG_SYNCHRONIZED);
    feed_interpolation(row);
}

void SyncCompat::handle_sync_delta(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || p_entity.is_null()) {
        return;
    }
    const int64_t route = plane->liveness_route_of(p_entity.ptr());
    wire::ReadStream reader(p_payload);
    sync_kernel::RetainedHead head;
    if (!sync_kernel::RetainedHead::wire.run(reader, head)) {
        drops_sync_no_set += 1;
        return;
    }
    Consumed *row = row_by_ordinal(route, int64_t(head.ordinal));
    if (row == nullptr) {
        drops_sync_no_set += 1;
        return;
    }
    refresh_row(row);
    validate_schema(row, route);
    if (row->poisoned) {
        drops_sync_poisoned += 1;
        return;
    }
    MultiplayerSynchronizer *sync = row->sync_node();
    if (sync == nullptr || p_sender != sync->get_multiplayer_authority()) {
        drops_sync_bad_sender += 1;
        return;
    }
    LocalVector<NodePath> selected;
    for (uint32_t at = 0; at < row->watch_paths.size(); ++at) {
        if (head.mask & (uint64_t(1) << at)) {
            selected.push_back(row->watch_paths[at]);
        }
    }

    stage_keys = Array();
    for (uint32_t at = 0; at < row->watch_paths.size(); ++at) {
        stage_keys.push_back(StringName(String(row->watch_paths[at])));
    }
    stage_payload = p_payload;
    stage_retained = true;
    stage_decoded = StagedWrites();
    const Error verdict
        = run_decode_stage(p_entity->get_rid_handle(), row->comp, 0, p_payload);
    const StagedWrites staged = stage_decoded;
    stage_decoded = StagedWrites();
    if (verdict != OK || !staged.is_valid()
        || staged.values.size() != int(selected.size())) {
        drops_sync_poisoned += 1;
        poison(row, route, "SYNC_DELTA mask and value count disagree");
        return;
    }
    if (apply_paths(row, selected, staged.values) != OK) {
        return;
    }
    delta_frames_in += 1;
    sync->emit_signal(SIG_DELTA_SYNCHRONIZED);
    feed_interpolation(row);
}

void SyncCompat::build_feed(Consumed *p_row, MultiplayerSynchronizer *p_sync) {
    p_row->feed_nodes.clear();
    p_row->feed_props.clear();
    Node *root = p_row->root_node();
    if (root != nullptr) {
        const Array found = synchronizers::display_bindings(p_sync, root);
        for (int at = 0; at < found.size(); ++at) {
            const Array entry = found[at];
            if (entry.size() < 3) {
                continue;
            }
            const Variant held = entry[1];
            p_row->feed_nodes.push_back(gd::instance_id(gd::live_object(held)));
            p_row->feed_props.push_back(StringName(entry[2]));
        }
    }
    p_row->feed_built = true;
}

void SyncCompat::feed_interpolation(Consumed *p_row) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    MultiplayerSynchronizer *sync = p_row->sync_node();
    if (sync == nullptr || sync->get_replication_config().is_null()
        || !sync->is_visibility_public()) {
        return;
    }
    if (!p_row->feed_built) {
        build_feed(p_row, sync);
    }
    if (p_row->feed_nodes.is_empty()) {
        return;
    }
    const ClockEngine &clock = plane->clock_engine();
    const int64_t tick = clock.get_configured() ? clock.get_tick() : 0;
    for (uint32_t at = 0; at < p_row->feed_nodes.size(); ++at) {
        Node *node = Object::cast_to<Node>(
            gd::instance_from_id(p_row->feed_nodes[at])
        );
        if (node == nullptr) {
            p_row->feed_built = false;
            continue;
        }
        const StringName prop = p_row->feed_props[at];
        const Ref<NetwInterpolate> spec
            = netw::script::model::get_node_property_interpolator(node, prop);
        if (spec.is_valid()) {
            plane->display_record(
                node,
                prop,
                node->get(prop),
                tick,
                spec,
                false
            );
        }
    }
}

PackedByteArray SyncCompat::encode_descriptors(int64_t p_route) {
    LocalVector<spawn::DescriptorRow> consumed_rows;
    const LocalVector<repl::SetRow> *found
        = sync_model != nullptr ? sync_model->route_rows(p_route) : nullptr;
    if (found != nullptr) {
        for (const repl::SetRow &row : *found) {
            if (row.kind == NetwSyncModel::KIND_CONSUMED) {
                spawn::DescriptorRow entry;
                entry.ordinal = uint64_t(row.ordinal);
                entry.schema_hash = uint64_t(uint32_t(row.schema_hash));
                consumed_rows.push_back(entry);
            }
        }
    }
    return spawn::Record::write_descriptors(consumed_rows);
}

void SyncCompat::note_schema(int64_t p_route, const Dictionary &p_descriptors) {
    if (p_descriptors.is_empty()) {
        pending_schema.erase(p_route);
    } else {
        pending_schema[p_route] = p_descriptors;
    }
}

void SyncCompat::clear_session() {
    pending_schema.clear();
    for (uint32_t at = 0; at < rows.size(); ++at) {
        watch_book.clear_baselines(rows[at]->id);
        rows[at]->schema_checked = false;
        rows[at]->poisoned = false;
        rows[at]->last_sync_usec = -1;
        rows[at]->last_watch_usec = -1;
        rows[at]->route = 0;
    }
}

void SyncCompat::clear_route(int64_t p_route) {
    pending_schema.erase(p_route);
    for (uint32_t at = 0; at < rows.size(); ++at) {
        if (rows[at]->route == p_route) {
            rows[at]->route = 0;
        }
    }
}

void SyncCompat::clear_peer(int64_t p_peer_id) {
    watch_book.clear_peer(p_peer_id);
}

void SyncCompat::dispose() {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        const Callable live
            = callable_mp(plane, &NetwMultiplayer::sync_compat_entity_live);
        if (plane->is_connected(SIG_ENTITY_LIVE, live)) {
            plane->disconnect(SIG_ENTITY_LIVE, live);
        }
    }
    for (uint32_t at = 0; at < rows.size(); ++at) {
        disconnect_config(rows[at]);
    }
}

Dictionary SyncCompat::counters() const {
    Dictionary out;
    out[StringName("sync_frames_out")] = sync_frames_out;
    out[StringName("sync_frames_in")] = sync_frames_in;
    out[StringName("delta_frames_out")] = delta_frames_out;
    out[StringName("delta_frames_in")] = delta_frames_in;
    out[StringName("sync_sets_active")] = int64_t(rows.size());
    out[StringName("drops_sync_no_set")] = drops_sync_no_set;
    out[StringName("drops_sync_bad_sender")] = drops_sync_bad_sender;
    out[StringName("drops_sync_poisoned")] = drops_sync_poisoned;
    out[StringName("drops_sync_unknown_flag")] = drops_sync_unknown_flag;
    return out;
}

} // namespace netw
