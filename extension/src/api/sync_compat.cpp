#include "netw/api/sync_compat.hpp"

#include <algorithm>

#include "godot/class_db.hpp"
#include "godot/resource.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/colors.hpp"
#include "netw/liveness_core.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/record.hpp"
#include "netw/store/validate.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"
#include "netw/sync_kernel.hpp"
#include "netw/synchronizers.hpp"
#include "netw/tree_hook.hpp"

using namespace godot;

namespace netw {

namespace {

const char *SIG_CHANGED = "changed";
const char *SIG_VISIBILITY_CHANGED = "visibility_changed";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_SYNCHRONIZED = "synchronized";
const char *SIG_DELTA_SYNCHRONIZED = "delta_synchronized";
const char *SIG_NODE_REMOVED = "node_removed";

bool throttle_elapsed(int64_t p_last_usec, double p_interval, int64_t p_now) {
    if (p_interval <= 0.0 || p_last_usec < 0) {
        return true;
    }
    return p_now - p_last_usec >= int64_t(p_interval * 1000000.0);
}

Node *target_of(Node *p_root, const NodePath &p_target) {
    if (p_target.is_empty() || p_root == nullptr) {
        return p_root;
    }
    return p_root->get_node_or_null(p_target);
}

template <typename K, typename T>
void unlink(HashMap<K, LocalVector<T *>> &r_index, const K &p_key, T *p_row) {
    LocalVector<T *> *held = r_index.getptr(p_key);
    if (held == nullptr) {
        return;
    }
    held->erase(p_row);
    if (held->is_empty()) {
        r_index.erase(p_key);
    }
}

} // namespace

MultiplayerSynchronizer *SyncCompat::Consumed::sync_node() const {
    if (resident) {
        return sync_at;
    }
    return Object::cast_to<MultiplayerSynchronizer>(gd::instance_from_id(sync));
}

Node *SyncCompat::Consumed::root_node() const {
    if (resident) {
        return root_at;
    }
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
    hears_entity_live = plane != nullptr;
}

SyncCompat *NetwMultiplayer::sync_adapter() const {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->get_sync_compat() : nullptr;
}

void NetwMultiplayer::sync_compat_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    NETW_ZONE_NC("Compat entity live", colors::WIRE);
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

void NetwMultiplayer::sync_compat_node_removed(Node *p_node) {
    if (SyncCompat *adapter = sync_adapter()) {
        adapter->on_node_removed(p_node);
    }
}

Error NetwMultiplayer::sync_compat_decode_stage_body() {
    SyncCompat *adapter = sync_adapter();
    return adapter != nullptr ? adapter->decode_stage_body() : ERR_UNCONFIGURED;
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

SyncCompat::Consumed *SyncCompat::row_of(Object *p_sync) {
    Consumed **found = rows_by_sync.getptr(uint64_t(gd::instance_id(p_sync)));
    if (found == nullptr) {
        return nullptr;
    }
    rows_examined += 1;
    return *found;
}

void SyncCompat::index_row(Consumed *p_row) {
    rows_by_sync.insert(uint64_t(p_row->sync), p_row);
    rows_by_root[uint64_t(p_row->root)].push_back(p_row);
}

void SyncCompat::set_route(Consumed *p_row, int64_t p_route) {
    if (p_row->route == p_route) {
        return;
    }
    p_row->ordinal_revision = UINT64_MAX;
    if (p_row->route != 0) {
        unlink(rows_by_route, p_row->route, p_row);
    }
    p_row->route = p_route;
    if (p_route != 0) {
        rows_by_route[p_route].push_back(p_row);
    }
}

void SyncCompat::drop_row(Consumed *p_row) {
    drop_declaration(p_row);
    disconnect_config(p_row);
    watch_book.reset(p_row->id);
    rows_by_sync.erase(uint64_t(p_row->sync));
    unlink(rows_by_root, uint64_t(p_row->root), p_row);
    rows.erase(p_row);
    memdelete(p_row);
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
    index_row(row);
    watch_tree();
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
    if (sync == nullptr) {
        prune();
    } else if (Consumed *row = row_of(sync)) {
        drop_row(row);
    }
    refresh_interest_intent(p_root);
    return OK;
}

void SyncCompat::on_sync_visibility_changed(int64_t, Node *p_root) {
    if (core() == nullptr) {
        return;
    }
    const LocalVector<Consumed *> *held
        = rows_by_root.getptr(uint64_t(gd::instance_id(p_root)));
    if (p_root != nullptr && held != nullptr) {
        for (Consumed *row : *held) {
            rows_examined += 1;
            refresh_row_intent(row);
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
    if (plane == nullptr || !plane->is_host()
        || p_root == nullptr) {
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
    PackedInt64Array candidates = plane->interest_known_peers();
    const int64_t local = local_peer_id();
    if (local != 0 && !candidates.has(local)) {
        candidates.push_back(local);
    }
    PackedInt64Array admitted;
    for (int at = 0; at < candidates.size(); ++at) {
        if (synchronizer_verdict(candidates[at], entity_root)) {
            admitted.push_back(candidates[at]);
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
    if (plane->is_host()) {
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

void SyncCompat::disconnect_config(Consumed *p_row) {
    if (!p_row->config.is_valid()) {
        return;
    }
    Object *cfg = gd::instance_from_id(p_row->config);
    const uint64_t held = uint64_t(p_row->config);
    p_row->config = ObjectID();
    int *holds = config_holds.getptr(held);
    if (holds != nullptr && --(*holds) <= 0) {
        config_holds.erase(held);
    }
    NetwMultiplayer *plane = core();
    if (cfg == nullptr || plane == nullptr || config_holds.has(held)) {
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
        config_holds[uint64_t(wanted)] += 1;
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
                    "it watches %d properties and the limit is 64. Split "
                    "them across more synchronizers",
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
    resolve_fields(p_row->sync_paths, p_row->sync_fields);
    resolve_fields(p_row->watch_paths, p_row->watch_fields);
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
        "MultiplayerSynchronizer '%s' on route %d stopped replicating, "
        "because %s.",
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
                "its replication config (%08x) differs from the server's "
                "(%08x). Give it the same config on every peer",
                p_row->schema_hash,
                declared
            )
        );
    }
}

int64_t SyncCompat::ordinal_of(Consumed *p_row) {
    if (sync_model == nullptr) {
        return -1;
    }
    const uint64_t revision = sync_model->revision();
    if (p_row->ordinal_revision == revision
        && store::validation_interval() <= 0) {
        return p_row->ordinal;
    }
    const repl::SetRow *row = sync_model->row_for(
        p_row->route,
        NetwSyncModel::KIND_CONSUMED,
        p_row->order_key,
        0
    );
    const int64_t ordinal = row != nullptr ? row->ordinal : -1;
    if (p_row->ordinal_revision == revision && ordinal != p_row->ordinal) {
        store::Miss missed;
        missed.route = p_row->route;
        missed.column = StringName("compat_ordinal");
        missed.gate = "model_revision";
        store::report_miss(missed);
    }
#if defined(NETW_TESTS)
    if (p_row->ordinal_revision == revision) {
        store::validation_audit::ledger().checks += 1;
    }
#endif
    p_row->ordinal = ordinal;
    p_row->ordinal_revision = revision;
    return ordinal;
}

SyncCompat::Consumed *SyncCompat::row_by_ordinal(
    int64_t p_route,
    int64_t p_ordinal
) {
    if (sync_model == nullptr || p_ordinal < 0) {
        return nullptr;
    }
    const repl::SetRow *row = sync_model->row(p_route, p_ordinal);
    if (row == nullptr || row->kind != NetwSyncModel::KIND_CONSUMED) {
        return nullptr;
    }
    const LocalVector<Consumed *> *held = rows_by_route.getptr(p_route);
    if (held == nullptr) {
        return nullptr;
    }
    for (Consumed *candidate : *held) {
        rows_examined += 1;
        if (candidate->order_key == row->key) {
            return candidate;
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
    set_route(p_row, route > 0 ? route : entity->get_route());
    p_row->entity = rid;
    p_row->ordinal_revision = UINT64_MAX;
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
    set_route(p_row, 0);
}

void SyncCompat::rows_under(Node *p_owner, LocalVector<Consumed *> &r_rows) {
    if (p_owner == nullptr || rows_by_root.is_empty()) {
        return;
    }
    LocalVector<Node *> walk;
    walk.push_back(p_owner);
    while (!walk.is_empty()) {
        Node *node = walk[walk.size() - 1];
        walk.resize(walk.size() - 1);
        const LocalVector<Consumed *> *held
            = rows_by_root.getptr(uint64_t(gd::instance_id(node)));
        if (held != nullptr) {
            for (Consumed *row : *held) {
                r_rows.push_back(row);
            }
        }
        const int children = node->get_child_count(true);
        for (int at = 0; at < children; ++at) {
            Node *child = node->get_child(at, true);
            if (tree::entity_object_at(child) == nullptr) {
                walk.push_back(child);
            }
        }
    }
    std::sort(
        r_rows.ptr(),
        r_rows.ptr() + r_rows.size(),
        [](const Consumed *a, const Consumed *b) { return a->id < b->id; }
    );
}

void SyncCompat::on_entity_live(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (p_entity.is_null()) {
        return;
    }
    LocalVector<Consumed *> matched;
    rows_under(p_entity->get_owner(), matched);
    const RID handle = p_entity->get_rid_handle();
    for (Consumed *row : matched) {
        rows_examined += 1;
        Node *root = row->root_node();
        if (root == nullptr) {
            continue;
        }
        const Ref<NetwEntity> found = NetwEntity::of(root);
        if (found.is_valid() && found->get_rid_handle() == handle) {
            row->entity_at.unref();
            set_route(row, p_route);
            capture_declaration(row);
        }
    }
}

void SyncCompat::prune() {
    for (uint32_t at = rows.size(); at > 0; --at) {
        Consumed *row = rows[at - 1];
        if (!row->resident && row->sync_node() == nullptr) {
            drop_row(row);
        }
    }
}

void SyncCompat::watch_tree() {
    NetwMultiplayer *plane = core();
    SceneTree *tree = gd::scene_tree();
    if (tree_watched || plane == nullptr || tree == nullptr) {
        return;
    }
    const Callable heard
        = callable_mp(plane, &NetwMultiplayer::sync_compat_node_removed);
    if (!tree->is_connected(SIG_NODE_REMOVED, heard)) {
        tree->connect(SIG_NODE_REMOVED, heard);
    }
    tree_watched = true;
}

void SyncCompat::forget_nodes(Consumed *p_row) {
    p_row->resident = false;
    p_row->sync_at = nullptr;
    p_row->root_at = nullptr;
    p_row->entity_at.unref();
}

void SyncCompat::on_node_removed(Node *p_node) {
    const uint64_t key = uint64_t(gd::instance_id(p_node));
    Consumed **by_sync = rows_by_sync.getptr(key);
    if (by_sync != nullptr) {
        forget_nodes(*by_sync);
    }
    LocalVector<Consumed *> *by_root = rows_by_root.getptr(key);
    if (by_root != nullptr) {
        for (Consumed *row : *by_root) {
            forget_nodes(row);
        }
    }
}

bool SyncCompat::locate(Consumed *p_row) {
    if (p_row->resident) {
        if (store::validation_interval() > 0) {
#if defined(NETW_TESTS)
            store::validation_audit::ledger().checks += 1;
#endif
            Object *sync = gd::instance_from_id(p_row->sync);
            Object *root = gd::instance_from_id(p_row->root);
            if (sync != p_row->sync_at || root != p_row->root_at
                || !p_row->sync_at->is_inside_tree()
                || !p_row->root_at->is_inside_tree()) {
                store::Miss missed;
                missed.route = p_row->route;
                missed.column = StringName("compat_nodes");
                missed.gate = "node_removed";
                store::report_miss(missed);
            }
        }
        return true;
    }
    forget_nodes(p_row);
    MultiplayerSynchronizer *sync = p_row->sync_node();
    Node *root = p_row->root_node();
    if (sync == nullptr || root == nullptr || !sync->is_inside_tree()) {
        return false;
    }
    p_row->sync_at = sync;
    p_row->root_at = root;
    p_row->resident = tree_watched && root->is_inside_tree();
    return true;
}

Ref<NetwEntity> SyncCompat::entity_of(Consumed *p_row) {
    if (p_row->resident && p_row->entity_at.is_valid()) {
        if (store::validation_interval() > 0) {
#if defined(NETW_TESTS)
            store::validation_audit::ledger().checks += 1;
#endif
            if (NetwEntity::of(p_row->root_at) != p_row->entity_at) {
                store::Miss missed;
                missed.route = p_row->route;
                missed.column = StringName("compat_entity");
                missed.gate = "entity_live";
                store::report_miss(missed);
            }
        }
        return p_row->entity_at;
    }
    const Ref<NetwEntity> entity = NetwEntity::of(p_row->root_at);
    if (p_row->resident) {
        p_row->entity_at = entity;
    }
    return entity;
}

int64_t SyncCompat::local_peer_id() {
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->session_get_inner().is_valid()
        && plane->session_get_inner()->get_multiplayer_peer().is_valid()) {
        return plane->get_unique_id();
    }
    return 1;
}

bool SyncCompat::synchronizer_verdict(int64_t p_peer_id, Node *p_node) {
    return synchronizers::visibility_verdict(
        p_node,
        p_peer_id,
        local_peer_id()
    );
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
    const int64_t local_id = pump_local_id;
    if (!pump_server) {
        for (int at = 0; at < pump_peers.size(); ++at) {
            if (pump_peers[at] == local_id) {
                continue;
            }
            if (!synchronizers::visibility_verdict(
                    p_root,
                    pump_peers[at],
                    local_id
                )) {
                continue;
            }
            out.push_back(pump_peers[at]);
        }
        return out;
    }
    if (spawn_book == nullptr) {
        return out;
    }
    if (spawn_book->has_armed(p_route)) {
        return out;
    }
    PackedInt32Array listed;
    const int32_t *base = nullptr;
    int count = 0;
    const spawn::Record *record = spawn_book->spawned_of(p_route);
    if (record != nullptr) {
        const RID rid
            = plane->get_liveness_core()->rid_from_route(int(p_route));
        if (plane->get_liveness_core()->state_of(rid)
            != NetwLivenessCore::STATE_LIVE) {
            return out;
        }
        base = record->recipient_list().ptr();
        count = int(record->recipient_list().size());
    } else {
        listed = plane->rpc_get_recipients(p_entity);
        base = listed.ptr();
        count = listed.size();
    }
    const bool filtered = plane->interest_entity_has_filter(p_entity);
    for (int at = 0; at < count; ++at) {
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
    NETW_ZONE_NC("Compat pass", colors::WIRE);
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
    pump_local_id = plane->get_unique_id();
    pump_server = plane->has_server_role();
    pump_peers = pump_server ? PackedInt32Array()
                             : gd::api_peer_ids(plane->session_get_inner());
    static const StringName encode_seam("_sync_encode");
    pump_encodes = plane->overrides_seam(encode_seam);

    LocalVector<Consumed *> &pass = pump_pass;
    pass.clear();
    for (uint32_t at = 0; at < rows.size(); ++at) {
        pass.push_back(rows[at]);
    }
    NETW_ZONE_VALUE(pass.size());
    for (uint32_t at = 0; at < pass.size(); ++at) {
        Consumed *row = pass[at];
        NETW_ZONE_LEAF_F(
            "Compat synchronizer",
            colors::WIRE,
            profile::SUBSYSTEM_ENTITY,
            "entity %lld",
            static_cast<long long>(row->route)
        );
        if (!locate(row)) {
            continue;
        }
        MultiplayerSynchronizer *sync = row->sync_at;
        Node *root = row->root_at;
        refresh_row(row);
        if (row->poisoned) {
            continue;
        }
        Ref<NetwEntity> entity = entity_of(row);
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
                NETW_NETWORK_PROFILE(
                    wire::ProfileSubjectScope observation(
                        plane->profiler_outgoing_subject(),
                        plane->profiler_synchronizer(
                            route,
                            row->sync,
                            row->root,
                            row->config
                        )
                    )
                );
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
    NETW_ZONE_LEAF("Compat encode sync", colors::WIRE, profile::SUBSYSTEM_WIRE);
    if (!gather(p_row, p_row->sync_fields, false)) {
        return PackedByteArray();
    }
    return sync_kernel::encode_volatile(ordinal_of(p_row), gathered);
}

void SyncCompat::poll_watchers(Consumed *p_row) {
    NETW_ZONE_LEAF(
        "Compat gather watched",
        colors::WIRE,
        profile::SUBSYSTEM_WIRE
    );
    gather(p_row, p_row->watch_fields, true);
    watch_book.poll(p_row->id, gathered, gathered_readable);
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
    NETW_NETWORK_PROFILE(
        wire::ProfileSubjectScope observation(
            plane->profiler_outgoing_subject(),
            plane->profiler_synchronizer(
                p_route,
                p_row->sync,
                p_row->root,
                p_row->config
            )
        )
    );
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
    if (encode_stage.is_valid() && pump_encodes) {
        stage_calls += 1;
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
    stage_valid = stage_retained ? sync_kernel::decode_retained(
                                       stage_payload,
                                       stage_columns,
                                       stage_decoded
                                   )
                                 : sync_kernel::decode_volatile(
                                       stage_payload,
                                       stage_columns,
                                       stage_decoded
                                   );
    return stage_valid ? OK : ERR_INVALID_DATA;
}

Error SyncCompat::run_decode_stage(
    const RID &p_entity,
    int64_t p_comp,
    int64_t p_flags,
    const PackedByteArray &p_payload
) {
    NetwMultiplayer *plane = core();
    Error verdict = OK;
    static const StringName decode_seam("_sync_decode");
    if (!decode_stage.is_valid() || plane == nullptr
        || !plane->overrides_seam(decode_seam)) {
        verdict = decode_stage_body();
    } else {
        stage_calls += 1;
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

const SyncCompat::Field *SyncCompat::field_of(const NodePath &p_path) {
    const String key = String(p_path);
    const Field *held = fields.getptr(key);
    if (held != nullptr) {
        return held;
    }
    Field field;
    field.target = NodePath(p_path.get_concatenated_names());
    field.property
        = NodePath(String(":") + String(p_path.get_concatenated_subnames()));
    fields_resolved += 1;
    return &fields.insert(key, field)->value;
}

void SyncCompat::resolve_fields(
    const LocalVector<NodePath> &p_paths,
    LocalVector<const Field *> &r_fields
) {
    r_fields.clear();
    for (const NodePath &path : p_paths) {
        r_fields.push_back(field_of(path));
    }
}

bool SyncCompat::gather(
    Consumed *p_row,
    const LocalVector<const Field *> &p_fields,
    bool p_allow_missing
) {
    Node *root = p_row->root_node();
    const int count = int(p_fields.size());
    gathered.resize(count);
    gathered_readable.resize(count);
    for (int at = 0; at < count; ++at) {
        const Field &field = *p_fields[at];
        Node *target = target_of(root, field.target);
        if (target == nullptr) {
            if (!p_allow_missing) {
                return false;
            }
            gathered.set(at, Variant());
            gathered_readable.set(at, false);
            continue;
        }
        gathered.set(at, gd::get_indexed(target, field.property));
        gathered_readable.set(at, true);
    }
    return true;
}

void SyncCompat::apply(
    Consumed *p_row,
    const LocalVector<const Field *> &p_fields,
    const Array &p_values
) {
    Node *root = p_row->root_node();
    for (uint32_t at = 0; at < p_fields.size(); ++at) {
        Node *target = target_of(root, p_fields[at]->target);
        if (target != nullptr) {
            gd::set_indexed(target, p_fields[at]->property, p_values[at]);
        }
    }
}

void SyncCompat::handle_sync(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NETW_ZONE_LEAF_F(
        "Compat apply sync",
        colors::WIRE,
        profile::SUBSYSTEM_ENTITY,
        "entity %lld",
        p_entity.is_valid() ? static_cast<long long>(p_entity->get_route())
                            : 0LL
    );
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
    NETW_NETWORK_PROFILE(plane->profiler().name_arrival(
        plane->profiler_synchronizer(route, row->sync, row->root, row->config)
    ));
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
        NETW_WARN_ONCE(
            sys::WIRE,
            "MultiplayerSynchronizer '%s' got an update with unknown flags "
            "%d, so it was dropped. Run the same Networked version on every "
            "peer.",
            sync->get_name(),
            flags
        );
        return;
    }

    stage_columns = int64_t(row->sync_paths.size());
    stage_payload = p_payload;
    stage_retained = false;
    stage_valid = false;
    const Error verdict = run_decode_stage(
        p_entity->get_rid_handle(),
        row->comp,
        flags,
        p_payload
    );
    const bool valid = stage_valid;
    const Array values = stage_decoded.values;
    stage_decoded = sync_kernel::DecodedRow();
    stage_valid = false;
    if (verdict != OK || !valid
        || values.size() != int(row->sync_paths.size())) {
        drops_sync_poisoned += 1;
        poison(
            row,
            route,
            "an update carried the wrong number of values. Give it the same "
            "replication config on every peer"
        );
        return;
    }
    apply(row, row->sync_fields, values);
    sync_frames_in += 1;
    sync->emit_signal(SIG_SYNCHRONIZED);
    feed_interpolation(row);
}

void SyncCompat::handle_sync_delta(
    const Ref<NetwEntity> &p_entity,
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    NETW_ZONE_LEAF_F(
        "Compat apply delta",
        colors::WIRE,
        profile::SUBSYSTEM_ENTITY,
        "entity %lld",
        p_entity.is_valid() ? static_cast<long long>(p_entity->get_route())
                            : 0LL
    );
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
    NETW_NETWORK_PROFILE(plane->profiler().name_arrival(
        plane->profiler_synchronizer(route, row->sync, row->root, row->config)
    ));
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
    LocalVector<const Field *> selected;
    for (uint32_t at = 0; at < row->watch_fields.size(); ++at) {
        if (head.mask & (uint64_t(1) << at)) {
            selected.push_back(row->watch_fields[at]);
        }
    }

    stage_columns = int64_t(row->watch_paths.size());
    stage_payload = p_payload;
    stage_retained = true;
    stage_valid = false;
    const Error verdict
        = run_decode_stage(p_entity->get_rid_handle(), row->comp, 0, p_payload);
    const bool valid = stage_valid;
    const Array values = stage_decoded.values;
    stage_decoded = sync_kernel::DecodedRow();
    stage_valid = false;
    if (verdict != OK || !valid || values.size() != int(selected.size())) {
        drops_sync_poisoned += 1;
        poison(
            row,
            route,
            "a delta update carried the wrong number of values. Give it the "
            "same replication config on every peer"
        );
        return;
    }
    apply(row, selected, values);
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
    p_row->feed_epoch = UINT64_MAX;
}

void SyncCompat::refresh_feed(Consumed *p_row) {
    const uint64_t epoch = script::model::interpolation_epoch();
    bool current = p_row->feed_epoch == epoch
        && p_row->feed_scripts.size() == p_row->feed_nodes.size();
    for (uint32_t at = 0; current && at < p_row->feed_nodes.size(); ++at) {
        Node *node = Object::cast_to<Node>(
            gd::instance_from_id(p_row->feed_nodes[at])
        );
        current = node != nullptr
            && repl::PropertyBinding::script_generation_of(node)
                == p_row->feed_scripts[at];
    }
    if (current && store::validation_interval() <= 0) {
        return;
    }
    LocalVector<uint64_t> scripts;
    LocalVector<uint8_t> live;
    bool any = false;
    bool same = p_row->feed_live.size() == p_row->feed_nodes.size();
    for (uint32_t at = 0; at < p_row->feed_nodes.size(); ++at) {
        Node *node = Object::cast_to<Node>(
            gd::instance_from_id(p_row->feed_nodes[at])
        );
        if (node == nullptr) {
            p_row->feed_built = false;
        }
        scripts.push_back(repl::PropertyBinding::script_generation_of(node));
        feed_lookups += current ? 0 : 1;
        const bool interpolated = node != nullptr
            && script::model::get_node_property_interpolator(
                   node,
                   p_row->feed_props[at]
            )
                   .is_valid();
        live.push_back(interpolated ? 1 : 0);
        any = any || interpolated;
        same = same && p_row->feed_live[at] == live[at];
    }
    if (current && !same) {
        store::Miss missed;
        missed.route = p_row->route;
        missed.column = StringName("compat_interpolated");
        missed.gate = "interpolation_epoch";
        store::report_miss(missed);
    }
#if defined(NETW_TESTS)
    if (current) {
        store::validation_audit::ledger().checks += 1;
    }
#endif
    p_row->feed_scripts = scripts;
    p_row->feed_live = live;
    p_row->feed_any = any;
    p_row->feed_epoch = epoch;
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
    refresh_feed(p_row);
    if (!p_row->feed_any) {
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
        if (!p_row->feed_live[at]) {
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
        rows[at]->ordinal_revision = UINT64_MAX;
    }
    rows_by_route.clear();
}

void SyncCompat::clear_route(int64_t p_route) {
    pending_schema.erase(p_route);
    const LocalVector<Consumed *> *held = rows_by_route.getptr(p_route);
    if (held == nullptr) {
        return;
    }
    for (Consumed *row : *held) {
        row->route = 0;
        row->ordinal_revision = UINT64_MAX;
    }
    rows_by_route.erase(p_route);
}

void SyncCompat::clear_peer(int64_t p_peer_id) {
    watch_book.clear_peer(p_peer_id);
}

void SyncCompat::dispose() {
    NetwMultiplayer *plane = core();
    hears_entity_live = false;
    if (plane != nullptr) {
        const Callable heard
            = callable_mp(plane, &NetwMultiplayer::sync_compat_node_removed);
        SceneTree *tree = gd::scene_tree();
        if (tree != nullptr && tree->is_connected(SIG_NODE_REMOVED, heard)) {
            tree->disconnect(SIG_NODE_REMOVED, heard);
        }
    }
    tree_watched = false;
    for (Consumed *row : rows) {
        forget_nodes(row);
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
