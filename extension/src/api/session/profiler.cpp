#include "netw/api/netw_multiplayer.hpp"

#if defined(DEBUG_ENABLED)

#include "godot/engine_debugger.hpp"
#include "godot/time.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/script/model.hpp"

namespace netw {

using namespace godot;

namespace {

constexpr uint32_t SET_LABEL_COLUMNS = 3;

String set_label(const repl::PropertyBinding *p_binding, int64_t p_record) {
    const String role = p_record == repl::PropertySet::RECORD_INPUT
        ? String(" input")
        : p_record == repl::PropertySet::RECORD_BROADCAST
        ? String(" broadcast")
        : String();
    const repl::PropertySet *set
        = p_binding != nullptr ? p_binding->get_set() : nullptr;
    if (set == nullptr || set->columns.is_empty()) {
        return role.is_empty() ? String(" state") : role;
    }
    String names;
    const uint32_t shown = MIN(set->columns.size(), SET_LABEL_COLUMNS);
    for (uint32_t at = 0; at < shown; ++at) {
        if (at > 0) {
            names += String(", ");
        }
        names += String(set->columns[at].get_key());
    }
    if (set->columns.size() > shown) {
        names += String(" +")
            + String::num_int64(set->columns.size() - shown);
    }
    return String(":") + names + role;
}

String member_label(
    const String &p_path,
    const wire::ChannelDecl *p_channel,
    const StringName &p_member
) {
    const StringName name
        = p_channel != nullptr ? p_channel->name : StringName();
    const String member = String(p_member);
    if (name == StringName("CALL")) {
        return p_path + String(".") + member + String("()");
    }
    if (name == StringName("SIGNAL")) {
        return p_path + String(".") + member + String(" signal");
    }
    if (name == StringName("ACTION")) {
        return p_path + String(".") + member + String(" action");
    }
    return p_path + String(":") + member;
}

} // namespace

String NetwMultiplayer::profiler_path(Node *p_node) const {
    if (!p_node->is_inside_tree()) {
        return String(p_node->get_name());
    }
    const String path = String(p_node->get_path());
    const String root = String(get_root_path());
    if (root.is_empty() || root == String("/root")) {
        return path.trim_prefix("/root/");
    }
    if (path == root) {
        return String(p_node->get_name());
    }
    return path.trim_prefix(root + String("/"));
}

void NetwMultiplayer::profiler_poll() {
    static const StringName rpc("multiplayer:rpc");
    static const StringName sync("multiplayer:replication");
    network_profiler.activate(
        gd::debugger_profiling(rpc),
        gd::debugger_profiling(sync)
    );
    if (network_profiler.active() && network_profiler.row_count() == 0) {
        network_profiler.prepare(channels);
    }
}

void NetwMultiplayer::profiler_publish() {
    if (!network_profiler.active()) {
        return;
    }
    const uint64_t now = Time::get_singleton()->get_ticks_msec();
    if (now - profiler_last_publish < 100) {
        return;
    }
    profiler_last_publish = now;
    network_profiler.collect(profiler_rpc_payload, profiler_sync_payload);
    if (profiler_rpc_payload.size() > 1) {
        gd::debugger_send("multiplayer:rpc", profiler_rpc_payload);
    }
    if (profiler_sync_payload.size() > 1) {
        gd::debugger_send("multiplayer:syncs", profiler_sync_payload);
    }
}

void NetwMultiplayer::profiler_node_changed(int64_t p_node) {
    network_profiler.invalidate_node(uint64_t(p_node));
}

void NetwMultiplayer::profiler_watch_node(Node *p_node) {
    for (Node *parent = p_node; parent != nullptr;
         parent = parent->get_parent()) {
        const uint64_t id = uint64_t(gd::instance_id(parent));
        if (!network_profiler.watch_node(id)) {
            continue;
        }
        const Callable changed
            = callable_mp(this, &NetwMultiplayer::profiler_node_changed)
                  .bind(int64_t(id));
        parent->connect("renamed", changed);
        parent->connect("tree_entered", changed);
    }
}

uint32_t NetwMultiplayer::profiler_member(
    int64_t p_route,
    int64_t p_channel,
    Node *p_node,
    const StringName &p_member
) {
    if (!network_profiler.records_rpc()) {
        return wire::NetworkProfiler::NONE;
    }
    if (p_node == nullptr) {
        return network_profiler.aggregate(uint8_t(p_channel));
    }
    const uint64_t node = uint64_t(gd::instance_id(p_node));
    wire::ProfileKey key;
    key.route = p_route;
    key.owner = node;
    key.version = network_profiler.path_revision();
    key.channel = uint16_t(p_channel);
    key.member = p_member;
    const uint32_t found = network_profiler.find(key);
    if (found != wire::NetworkProfiler::NONE) {
        return found;
    }
    if (network_profiler.subject_count()
        >= wire::NetworkProfiler::SUBJECT_LIMIT) {
        return network_profiler.aggregate(uint8_t(p_channel));
    }
    profiler_watch_node(p_node);
    const wire::ChannelDecl *channel
        = channels.find_channel(uint8_t(p_channel));
    return network_profiler.admit(
        key, member_label(profiler_path(p_node), channel, p_member)
    );
}

uint32_t NetwMultiplayer::profiler_frame_subject(
    int64_t p_route,
    int64_t p_comp,
    int64_t p_channel,
    const String &p_path
) {
    if (!network_profiler.records_rpc()) {
        return wire::NetworkProfiler::NONE;
    }
    if (p_comp == NetwCompTable::FALLBACK_COMP) {
        return network_profiler.aggregate(uint8_t(p_channel));
    }
    ReplicationCore *replication = get_replication_plane();
    NetwSyncModel *model
        = replication != nullptr ? replication->get_sync_model() : nullptr;
    const bool addresses_set
        = model != nullptr && replication->channel_addresses_a_set(p_channel);
    wire::ProfileKey key;
    key.route = p_route;
    key.owner = uint64_t(p_comp);
    key.version = network_profiler.path_revision();
    key.channel = addresses_set ? wire::NetworkProfiler::SET_CHANNEL
                                : uint16_t(p_channel);
    const uint32_t found = network_profiler.find(key);
    if (found != wire::NetworkProfiler::NONE) {
        return found;
    }
    if (network_profiler.subject_count()
        >= wire::NetworkProfiler::SUBJECT_LIMIT) {
        return network_profiler.aggregate(uint8_t(p_channel));
    }
    Node *node = nullptr;
    const repl::SetRow *set = nullptr;
    const repl::PropertyBinding *binding = nullptr;
    if (addresses_set) {
        set = model->row(p_route, p_comp);
        if (set != nullptr) {
            binding = model->binding_of(p_route, p_comp);
            node = binding != nullptr ? binding->node() : nullptr;
        }
    }
    if (node == nullptr && p_route != 0) {
        node = repl_comp_node(p_route, p_comp, p_path);
        if (node == nullptr) {
            const Ref<NetwEntity> entity = wrapper_for_route(p_route);
            node = entity.is_valid() ? entity->get_owner() : nullptr;
        }
    }
    if (p_route != 0 && (node == nullptr || !node->is_inside_tree())) {
        return network_profiler.aggregate(uint8_t(p_channel));
    }
    const wire::ChannelDecl *channel
        = channels.find_channel(uint8_t(p_channel));
    const String words
        = wire::NetworkProfiler::channel_words(channel, p_channel);
    String label;
    if (node == nullptr) {
        label = String("(session) ") + words;
    } else if (set != nullptr) {
        label = profiler_path(node) + set_label(binding, set->record);
    } else {
        label = profiler_path(node) + String(" ") + words;
    }
    profiler_watch_node(node);
    return network_profiler.admit(key, label);
}

uint32_t NetwMultiplayer::profiler_synchronizer(
    int64_t p_route,
    ObjectID p_sync,
    ObjectID p_root,
    ObjectID p_config
) {
    if (!network_profiler.records_sync()) {
        return wire::NetworkProfiler::SKIP;
    }
    const uint32_t subject
        = network_profiler.synchronizer(p_route, p_sync, p_root, p_config);
    if (subject != wire::NetworkProfiler::NONE
        || !network_profiler.records_sync()) {
        return subject;
    }
    return profiler_frame_subject(p_route, 0, wire::builtin_channel("SYNC"),
                                  String());
}

} // namespace netw

#endif
