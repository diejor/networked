#include "netw/spawn/pipeline.hpp"

#include "godot/packed_scene.hpp"
#include "godot/resource.hpp"
#include "godot/resource_uid.hpp"
#include "godot/script.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/despawn_config.hpp"
#include "netw/api/event_plane.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/colors.hpp"
#include "netw/entity/stage.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/planner.hpp"
#include "netw/subsystems.hpp"
#include "netw/synchronizers.hpp"

using namespace godot;

namespace netw::spawn {

namespace {

const char *SIG_PEER_CONNECTED = "peer_connected";
const char *SIG_ENTITY_LIVE = "entity_live";
const char *SIG_CLOCK_ON_TICK = "clock_on_tick";
const char *SIG_TREE_ENTERED = "tree_entered";
const char *SIG_TREE_EXITING = "tree_exiting";
const char *KEY_SWEEP = "spawn-visibility-sweep";
const char *KEY_FLUSH = "spawn-carrier-flush";
const double CLOCKLESS_TICKRATE = 30.0;
constexpr int BLOB_CAP = 4095;

} // namespace

NetwMultiplayer *Pipeline::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

Object *Pipeline::api() const {
    return gd::instance_from_id(api_id);
}

Object *Pipeline::stage_seam(const StringName &p_seam) const {
    Object *seam = api();
    if (seam == nullptr || seam == core() || !seam->has_method(p_seam)) {
        return nullptr;
    }
    return seam;
}

void Pipeline::set_core(Object *p_core) {
    NetwMultiplayer *plane = Object::cast_to<NetwMultiplayer>(p_core);
    core_id = gd::instance_id(plane);
    if (plane == nullptr) {
        return;
    }
    const Callable joined
        = callable_mp(plane, &NetwMultiplayer::spawn_on_peer_connected);
    if (!plane->is_connected(SIG_PEER_CONNECTED, joined)) {
        plane->connect(SIG_PEER_CONNECTED, joined);
    }
}

void Pipeline::set_api(Object *p_api) {
    api_id = gd::instance_id(p_api);
}

void Pipeline::set_adapters(SpawnerCompat *p_spawner, SyncCompat *p_sync) {
    spawner_compat = p_spawner;
    sync_compat = p_sync;
}

void Pipeline::set_channels(
    int64_t p_spawn,
    int64_t p_despawn,
    int64_t p_hide,
    int64_t p_reparent
) {
    channel_spawn = p_spawn;
    channel_despawn = p_despawn;
    channel_hide = p_hide;
    channel_reparent = p_reparent;
}

void Pipeline::set_encode_seams(const Callable &p_encode_prop_val) {
    encode_prop_val_seam = p_encode_prop_val;
}

void Pipeline::set_derived_seams(
    const Callable &p_encode_derived,
    const Callable &p_note_derived
) {
    encode_derived_seam = p_encode_derived;
    note_derived_seam = p_note_derived;
}

void Pipeline::set_repl_seams(
    const Callable &p_flush_buffers,
    const Callable &p_resolve_comp
) {
    flush_buffers_seam = p_flush_buffers;
    resolve_comp_seam = p_resolve_comp;
}

bool Pipeline::is_server_authority() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->is_server() : true;
}

bool Pipeline::has_peer() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr && plane->session_get_inner().is_valid()
        && plane->session_get_inner()->get_multiplayer_peer().is_valid();
}

PackedInt32Array Pipeline::connected_peers() const {
    NetwMultiplayer *plane = core();
    return plane != nullptr ? plane->reachable_peer_ids() : PackedInt32Array();
}

Error Pipeline::stage_verdict(
    int64_t p_stage,
    Error p_verdict,
    int64_t p_route
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return ERR_UNAVAILABLE;
    }
    return plane->finish_stage_verdict(p_stage, p_verdict, p_route);
}

void Pipeline::sink_verdict(Error p_verdict, int64_t p_route) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->sink_verdict(p_verdict, p_route);
    }
}

void Pipeline::connect_once(
    Object *p_source,
    const StringName &p_signal,
    const Callable &p_callable,
    uint32_t p_flags
) {
    if (p_source == nullptr || p_source->is_connected(p_signal, p_callable)) {
        return;
    }
    p_source->connect(p_signal, p_callable, p_flags);
}

void Pipeline::on_peer_connected(int64_t p_peer_id) {
    if (!is_server_authority()) {
        return;
    }
    sink_verdict(replay_spawn_book(p_peer_id), 0);
}

Error Pipeline::replay_spawn_book(int64_t p_peer_id) {
    NETW_ZONE_NC("Spawn replay book", colors::LIVENESS);
    NETW_ZONE_VALUE(p_peer_id);
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !has_peer()) {
        return ERR_UNAVAILABLE;
    }
    const PackedInt64Array unencodable = plane->spawn_replay_to(
        &spawn_book,
        p_peer_id,
        channel_spawn,
        callable_mp(plane, &NetwMultiplayer::spawn_encode_spawn_frame)
    );
    for (int at = 0; at < unencodable.size(); ++at) {
        sink_verdict(ERR_INVALID_DATA, unencodable[at]);
    }
    schedule_carrier_flush();
    return OK;
}

void Pipeline::schedule_visibility_sweep() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !is_server_authority()) {
        return;
    }
    plane->session_defer(
        callable_mp(plane, &NetwMultiplayer::spawn_run_visibility_sweep),
        StringName(KEY_SWEEP)
    );
}

void Pipeline::sweep_now() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !is_server_authority()) {
        return;
    }
    plane->session_cancel_deferred(StringName(KEY_SWEEP));
    run_visibility_sweep();
}

void Pipeline::run_visibility_sweep() {
    NETW_ZONE_NC("Spawn visibility sweep", colors::LIVENESS);
    NetwMultiplayer *plane = core();
    if (plane == nullptr || !has_peer()) {
        return;
    }
    if (!plane->is_online()) {
        return;
    }
    const PackedInt32Array peers = connected_peers();

    plane->spawn_refresh_anchors(
        &spawn_book,
        spawner_compat != nullptr ? &spawner_compat->get_roster() : nullptr
    );

    const Variant plan = Planner::reconcile(
        plane->spawn_reconcile_rows(&spawn_book, peers),
        peers
    );
    Error reconcile_verdict = OK;
    if (plan.get_type() == Variant::NIL) {
        reconcile_verdict = ERR_UNCONFIGURED;
    }
    if (stage_verdict(EventPlane::SPAWN_RECONCILE, reconcile_verdict, 0)
        != OK) {
        plane->interest_leave_finish_sweep();
        return;
    }
    plane->spawn_execute_plan(
        &spawn_book,
        Array(plan),
        channel_spawn,
        channel_hide,
        callable_mp(plane, &NetwMultiplayer::spawn_encode_spawn_frame)
    );
    plane->interest_leave_finish_sweep();
    schedule_carrier_flush();
}

void Pipeline::schedule_carrier_flush() {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->session_defer(
            callable_mp(plane, &NetwMultiplayer::spawn_run_carrier_flush),
            StringName(KEY_FLUSH)
        );
    }
}

void Pipeline::run_carrier_flush() {
    NETW_ZONE_NC("Spawn carrier flush", colors::LIVENESS);
    if (flush_buffers_seam.is_valid()) {
        flush_buffers_seam.call();
    }
}

Error Pipeline::declare_stage(const RID &p_handle, const Dictionary &p_facts) {
    Object *seam = stage_seam(StringName("_spawn_declare"));
    NetwMultiplayer *plane = core();
    if (seam == nullptr && plane == nullptr) {
        return OK;
    }
    const Error declared = seam != nullptr
        ? Error(int(seam->call("_spawn_declare", p_handle, p_facts)))
        : plane->spawn_declare(p_handle, p_facts);
    return stage_verdict(
        EventPlane::SPAWN_DECLARE,
        declared,
        int64_t(p_facts[StringName("route")])
    );
}

Ref<NetwEntity> Pipeline::arm_authoritative_spawn(
    Record *p_record,
    Node *p_node,
    const Ref<NetwParticipant> &p_owner
) {
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr) {
        return Ref<NetwEntity>();
    }
    const Ref<NetwEntity> entity = plane->spawn_arm_identity(
        p_record,
        p_node,
        p_owner,
        callable_mp(plane, &NetwMultiplayer::spawn_declare_stage)
    );
    if (entity.is_null()) {
        return entity;
    }
    const int64_t route = entity->get_route();
    spawn_book.arm(*p_record);
    if (entity->get_stage() == int64_t(netw::entity::Stage::UNBOUND)
        && shell != nullptr) {
        entity->arm(Object::cast_to<NetwMultiplayer>(shell));
    }
    if (p_node->is_inside_tree()) {
        entity->_go_live_if_armed();
        schedule_armed_flush(route);
    } else {
        connect_once(
            p_node,
            SIG_TREE_ENTERED,
            callable_mp(plane, &NetwMultiplayer::spawn_on_armed_tree_entered)
                .bind(route),
            Object::CONNECT_ONE_SHOT
        );
    }
    return entity;
}

Ref<NetwEntity> Pipeline::replicate(
    Node *p_node,
    const Ref<NetwParticipant> &p_owner
) {
    NETW_ZONE_NC("Spawn replicate", colors::LIVENESS);
    if (!is_server_authority()) {
        NETW_ERR_V(
            Ref<NetwEntity>(),
            sys::SPAWN,
            "Netw.replicate is server-only; player-proposed spawns are the "
            "(future) Netw.request_replicate verb"
        );
    }
    if (p_node == nullptr) {
        NETW_ERR_V(
            Ref<NetwEntity>(),
            sys::SPAWN,
            "Netw.replicate: node is null or freed"
        );
    }
    if (p_node->is_inside_tree()) {
        NETW_ERR_V(
            Ref<NetwEntity>(),
            sys::SPAWN,
            "Netw.replicate: identity must precede tree entry: call before "
            "add_child; pre-placed nodes are the (future) Netw.adopt boundary"
        );
    }

    const Ref<NetwEntity> existing = NetwEntity::of(p_node);
    if (existing.is_valid() && existing->get_route() > 0) {
        if (spawn_book.is_recv(existing->get_route())) {
            NETW_ERR_V(
                Ref<NetwEntity>(),
                sys::SPAWN,
                "Netw.replicate: '%s' was materialized from the server; this "
                "peer cannot take authority of a remote-owned instance",
                p_node->get_name()
            );
        }
        NETW_WARN(
            sys::SPAWN,
            "Netw.replicate: '%s' is already replicated (route %d)",
            p_node->get_name(),
            existing->get_route()
        );
        return existing;
    }

    if (p_node->get_scene_file_path().is_empty()) {
        NETW_ERR_V(
            Ref<NetwEntity>(),
            sys::SPAWN,
            "Netw.replicate: node has no reconstruction recipe. Instantiate it "
            "from a scene or use Netw.spawn with a configured spawn function."
        );
    }

    Record record;
    record.set_recipe(Book::RECIPE_SCENE);
    record.set_scene_path(p_node->get_scene_file_path());
    return arm_authoritative_spawn(&record, p_node, p_owner);
}

Node *Pipeline::spawn(
    const Callable &p_fn,
    const Array &p_args,
    const Ref<NetwParticipant> &p_owner
) {
    NETW_ZONE_NC("Spawn construct", colors::LIVENESS);
    if (!is_server_authority()) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn is server-only; player-proposed spawns are the "
            "(future) Netw.request_spawn verb"
        );
    }
    Node *host = Object::cast_to<Node>(p_fn.get_object());
    if (host == nullptr) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn: the spawn function must be a method on a Node host "
            "that exists on every peer"
        );
    }
    const Ref<Script> script = host->get_script();
    const StringName method = p_fn.get_method();
    if (netw::script::model::get_spawn_config(script, method).is_null()) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn: spawn function '%s' is not registered; call "
            "Netw.configure_spawn in the host's _init()",
            method
        );
    }
    if (!netw::script::model::validate_argument_count(
            script,
            method,
            p_args.size()
        )) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn: argument count mismatch for spawn function '%s'",
            method
        );
    }

    const Variant built = p_fn.callv(p_args);
    Node *node = Object::cast_to<Node>(gd::live_object(built));
    if (node == nullptr || node->is_inside_tree()) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn: spawn function '%s' must return an orphan Node. A "
            "spawn function must not be a coroutine.",
            method
        );
    }
    const Ref<NetwEntity> existing = NetwEntity::of(node);
    if (existing.is_valid() && existing->get_route() != 0) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "Netw.spawn: the spawn function returned an already replicated node"
        );
    }

    Record record;
    record.set_recipe(Book::RECIPE_FN);
    record.bind_fn_host(host);
    record.set_fn_method(method);
    record.set_fn_args(p_args);
    arm_authoritative_spawn(&record, node, p_owner);
    NetwMultiplayer::scene_wrap_world(node);
    return node;
}

void Pipeline::register_spawn_constructor(
    const StringName &p_id,
    const Callable &p_fn,
    const Array &p_arg_types,
    const Array &p_quantizers
) {
    constructors[p_id] = p_fn;
    if (p_arg_types.is_empty() && p_quantizers.is_empty()) {
        constructor_schemas.erase(p_id);
        return;
    }
    Array schema;
    schema.push_back(p_quantizers);
    schema.push_back(p_arg_types);
    constructor_schemas[p_id] = schema;
}

Callable Pipeline::constructor_of(const StringName &p_id) const {
    const HashMap<StringName, Callable>::ConstIterator found
        = constructors.find(p_id);
    return found ? found->value : Callable();
}

Variant Pipeline::fn_registry_schema(
    const StringName &p_id,
    const Callable &p_fn
) const {
    const HashMap<StringName, Array>::ConstIterator found
        = constructor_schemas.find(p_id);
    if (found) {
        return found->value;
    }
    Object *host = p_fn.get_object();
    Ref<Script> script;
    if (host != nullptr) {
        script = host->get_script();
    }
    return fn_script_schema(script, p_fn.get_method());
}

Variant Pipeline::fn_script_schema(
    const Ref<Script> &p_script,
    const StringName &p_method
) {
    const Ref<NetwMemberConfig> cfg
        = netw::script::model::get_spawn_config(p_script, p_method);
    if (cfg.is_null()) {
        return Variant();
    }
    Array schema;
    schema.push_back(cfg->get_quantizers());
    schema.push_back(
        netw::script::model::get_method_arg_types(p_script, p_method)
    );
    return schema;
}

Node *Pipeline::spawn_registered(
    const StringName &p_id,
    const Array &p_args,
    const Ref<NetwParticipant> &p_owner
) {
    if (!is_server_authority()) {
        NETW_ERR_V(nullptr, sys::SPAWN, "spawn_registered is server-only");
    }
    const Callable fn = constructor_of(p_id);
    if (!fn.is_valid()) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "spawn_registered: no constructor registered under id '%s'",
            p_id
        );
    }
    const Variant built = fn.callv(p_args);
    Node *node = Object::cast_to<Node>(gd::live_object(built));
    if (node == nullptr || node->is_inside_tree()) {
        NETW_ERR_V(
            nullptr,
            sys::SPAWN,
            "spawn_registered: constructor '%s' must return an orphan node",
            p_id
        );
    }
    Record record;
    record.set_recipe(Book::RECIPE_FN_REGISTRY);
    record.set_fn_registry_id(p_id);
    record.set_fn_args(p_args);
    arm_authoritative_spawn(&record, node, p_owner);
    NetwMultiplayer::scene_wrap_world(node);
    return node;
}

Ref<NetwEntity> Pipeline::adopt_in_place(Node *p_root) {
    if (!is_server_authority() || p_root == nullptr
        || !p_root->is_inside_tree()) {
        return Ref<NetwEntity>();
    }
    Record record;
    record.set_recipe(Book::RECIPE_ADOPT);
    const Ref<NetwEntity> entity
        = arm_authoritative_spawn(&record, p_root, Ref<NetwParticipant>());
    if (entity.is_valid()) {
        NetwMultiplayer *plane = core();
        if (plane != nullptr) {
            plane->liveness_bind_route(record.get_route(), entity.ptr());
        }
        flush_armed_spawn(record.get_route());
    }
    return entity;
}

Ref<NetwEntity> Pipeline::arm_consumed_spawn(
    Node *p_node,
    Object *p_spawner,
    int p_scene_index,
    const Variant &p_data
) {
    Record record;
    record.set_recipe(Book::RECIPE_SPAWNER);
    record.bind_spawner(Object::cast_to<MultiplayerSpawner>(p_spawner));
    record.set_scene_index(p_scene_index);
    record.set_custom_data(p_data);
    return arm_authoritative_spawn(&record, p_node, Ref<NetwParticipant>());
}

void Pipeline::on_armed_tree_entered(int64_t p_route) {
    schedule_armed_flush(p_route);
}

void Pipeline::schedule_armed_flush(int64_t p_route) {
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->session_defer(
            callable_mp(plane, &NetwMultiplayer::spawn_flush_armed_spawn)
                .bind(p_route),
            StringName(vformat("spawn-armed-flush?%d", p_route))
        );
    }
}

void Pipeline::flush_armed_spawn(int64_t p_route) {
    NETW_ZONE_NC("Spawn flush armed", colors::LIVENESS);
    NETW_ZONE_VALUE(p_route);
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    Record *record = plane->spawn_issue_armed(&spawn_book, p_route);
    if (record == nullptr) {
        return;
    }
    Node *node = record->node();
    if (!has_peer()) {
        return;
    }
    const PackedByteArray payload = encode_spawn_frame(p_route, node);
    if (payload.is_empty()) {
        return;
    }
    plane->spawn_fan_out(
        &spawn_book,
        record,
        node,
        payload,
        connected_peers(),
        channel_spawn
    );
    schedule_carrier_flush();
}

bool Pipeline::owns_spawned_route(int64_t p_route) const {
    return spawn_book.has_spawned(p_route) || spawn_book.is_recv(p_route);
}

void Pipeline::settle_move(int64_t p_route) {
    Record *record = spawn_book.spawned_of(p_route);
    if (record == nullptr) {
        return;
    }
    Node *node = record->node();
    if (node == nullptr || !node->is_inside_tree()) {
        return;
    }
    send_reparent(record, node);
}

void Pipeline::settle_death(int64_t p_route) {
    if (!spawn_book.has_spawned(p_route)) {
        return;
    }
    despawn_tracked_route(p_route);
}

bool Pipeline::holds_received_route(int64_t p_route) const {
    return spawn_book.is_recv(p_route);
}

void Pipeline::settle_absence(int64_t p_route) {
    if (!spawn_book.is_recv(p_route)) {
        return;
    }
    spawn_book.drop_recv(p_route);
    if (NetwMultiplayer *plane = core()) {
        plane->action_gate_drop(p_route);
    }
}

void Pipeline::despawn_tracked_route(int64_t p_route) {
    NETW_ZONE_NC("Spawn despawn route", colors::LIVENESS);
    NETW_ZONE_VALUE(p_route);
    NetwMultiplayer *plane = core();
    Object *seam = stage_seam(StringName("_spawn_undeclare"));
    if (plane == nullptr) {
        return;
    }
    const bool live = has_peer() && plane->is_online();
    const Callable undeclare = seam != nullptr
        ? Callable(seam, StringName("_spawn_undeclare"))
        : callable_mp(plane, &NetwMultiplayer::spawn_undeclare);
    if (plane->spawn_despawn_route(
            &spawn_book,
            p_route,
            live ? connected_peers() : PackedInt32Array(),
            channel_despawn,
            undeclare
        )
        && live) {
        schedule_carrier_flush();
    }
}

void Pipeline::send_reparent(Record *p_record, Node *p_node) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    plane->spawn_refresh_anchor(
        p_record,
        p_node,
        spawner_compat != nullptr ? &spawner_compat->get_roster() : nullptr
    );
    if (!has_peer()) {
        return;
    }
    if (!plane->is_online()) {
        return;
    }
    if (plane->scene_leaves_route_unadmitted(p_record->get_route())) {
        moves_unadmitted += 1;
        NETW_WARN(
            sys::SPAWN,
            "route %d moved into a scene its peer is not admitted to, and "
            "parenting alone does not admit, so that peer sees nothing else "
            "there. Move it with reparent_to or scene_move, or call "
            "scene_admit",
            int(p_record->get_route())
        );
    }
    sweep_now();
    if (!plane->spawn_send_reparent(
            &spawn_book,
            p_record,
            p_node,
            connected_peers(),
            channel_reparent
        )) {
        return;
    }
    schedule_carrier_flush();
    schedule_visibility_sweep();
}

namespace {

LocalVector<call_args::Slot> slots_of_values(const Array &p_values) {
    LocalVector<call_args::Slot> out;
    out.reserve(uint32_t(p_values.size()));
    for (int at = 0; at < p_values.size(); ++at) {
        out.push_back(call_args::of_value(p_values[at]));
    }
    return out;
}

} // namespace

PackedByteArray Pipeline::encode_fn_args(
    const Array &p_schema,
    const Array &p_args
) {
    NetwMultiplayer *plane = core();
    const LocalVector<call_args::Slot> slots = plane != nullptr
        ? plane->rpc_encoded_args(p_args)
        : slots_of_values(p_args);
    wire::WriteStream stream;
    if (!call_args::write(stream, slots, p_schema[0], p_schema[1])
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

LocalVector<call_args::Slot> Pipeline::read_fn_args(
    const PackedByteArray &p_bytes,
    const Array &p_schema
) {
    wire::ReadStream stream(p_bytes);
    LocalVector<call_args::Slot> slots;
    if (!call_args::read(stream, p_schema[0], p_schema[1], slots)
        || !stream.align_verify() || stream.bits_remaining() != 0) {
        return LocalVector<call_args::Slot>();
    }
    return slots;
}

Variant Pipeline::resolve_spawn_args(
    const LocalVector<call_args::Slot> &p_slots,
    const PackedByteArray &p_payload,
    int64_t p_route
) {
    NetwMultiplayer *plane = core();
    Array args;
    for (uint32_t at = 0; at < p_slots.size(); ++at) {
        const call_args::Slot &slot = p_slots[at];
        if (!slot.addresses_node) {
            args.push_back(slot.value);
            continue;
        }
        if (plane != nullptr
            && Park::anchor_parks(
                plane->liveness_route_state(slot.node.route)
            )) {
            park_spawn(p_payload, slot.node.route, p_route);
            return Variant();
        }
        Node *arg_node = nullptr;
        if (plane != nullptr && resolve_comp_seam.is_valid()) {
            const Ref<NetwEntity> arg_entity
                = plane->wrapper_for_route(slot.node.route);
            if (arg_entity.is_valid()) {
                const Variant resolved = resolve_comp_seam.call(
                    arg_entity,
                    slot.node.comp,
                    slot.node.path
                );
                arg_node = Object::cast_to<Node>(gd::live_object(resolved));
            }
        }
        args.push_back(arg_node);
    }
    return args;
}

bool Pipeline::put_scene_recipe(
    wire::WriteStream &p_stream,
    const String &p_path
) {
    const int64_t uid = gd::resource_uid_for(p_path);
    bool by_uid = uid != int64_t(ResourceUID::INVALID_ID);
    if (!p_stream.bool1(by_uid)) {
        return false;
    }
    if (by_uid) {
        uint64_t staged = uint64_t(uid);
        return p_stream.bits(staged, 64);
    }
    String path = p_path;
    return wire::string_field(p_stream, path);
}

bool Pipeline::get_scene_recipe(wire::ReadStream &p_stream, String &r_path) {
    bool by_uid = false;
    if (!p_stream.bool1(by_uid)) {
        return false;
    }
    if (by_uid) {
        uint64_t staged = 0;
        if (!p_stream.bits(staged, 64)) {
            return false;
        }
        r_path = gd::resource_uid_path(int64_t(staged));
        return true;
    }
    return wire::string_field(p_stream, r_path);
}

#ifdef NETW_TESTS
void Pipeline::arm_spawn_state(const Array &p_rows) {
    armed_spawn_state.clear();
    for (int at = 0; at < p_rows.size(); ++at) {
        armed_spawn_state.push_back(p_rows[at]);
    }
    spawn_state_is_armed = true;
}

void Pipeline::disarm_spawn_state() {
    armed_spawn_state.clear();
    spawn_state_is_armed = false;
}
#endif

TypedArray<Dictionary> Pipeline::collect_spawn_state(Node *p_root) {
    NETW_ZONE_NC("Spawn collect state", colors::LIVENESS);
#ifdef NETW_TESTS
    if (spawn_state_is_armed) {
        spawn_state_asked += 1;
        return armed_spawn_state;
    }
#endif
    TypedArray<Dictionary> out;
    if (p_root == nullptr) {
        return out;
    }
    LocalVector<Node *> stack;
    stack.push_back(p_root);
    while (!stack.is_empty()) {
        Node *node = stack[stack.size() - 1];
        stack.remove_at(stack.size() - 1);
        const Dictionary configs
            = netw::script::model::get_node_property_configs(node);
        const Array names = configs.keys();
        for (int at = 0; at < names.size(); ++at) {
            const StringName prop = names[at];
            const Ref<NetwPropertyConfig> cfg = configs[prop];
            if (cfg.is_null() || !cfg->get_is_spawn_state()
                || !gd::has_property(node, prop)) {
                continue;
            }
            Dictionary row;
            row[StringName("node")] = node;
            row[StringName("prop")] = prop;
            row[StringName("cfg")] = cfg;
            out.push_back(row);
        }
        const int children = node->get_child_count();
        for (int at = children - 1; at >= 0; --at) {
            stack.push_back(node->get_child(at));
        }
    }
    return out;
}

PackedByteArray Pipeline::encode_spawn_frame(int64_t p_route, Node *p_node) {
    NETW_ZONE_NC("Spawn encode frame", colors::LIVENESS);
    NetwMultiplayer *plane = core();
    Record *p_record = spawn_book.spawned_of(p_route);
    if (plane == nullptr || p_record == nullptr || p_node == nullptr) {
        return PackedByteArray();
    }
    wire::WriteStream stream;
    const Ref<NetwEntity> entity = NetwEntity::of(p_node);
    if (!plane->verb_head_write(stream, p_route)
        || !p_record->encode_header(stream, entity.ptr())) {
        NETW_ERROR(
            sys::SPAWN,
            "route %d carries a header no frame can hold",
            int(p_route)
        );
        return PackedByteArray();
    }

    Node *outer = NetwMultiplayer::scene_outer_of(p_node);
    Node *parent = outer->get_parent();
    const int recipe = p_record->get_recipe();
    MultiplayerSpawner *spawner
        = recipe == Book::RECIPE_SPAWNER ? p_record->spawner() : nullptr;
    bool parent_is_spawn_target = spawner != nullptr && parent != nullptr
        && spawner->get_node_or_null(spawner->get_spawn_path()) == parent;

    int64_t recipe_code = int64_t(recipe);
    if (!stream.int_range(recipe_code, 0, 4)
        || !stream.bool1(parent_is_spawn_target)) {
        NETW_ERROR(
            sys::SPAWN,
            "unknown spawn recipe %d for route %d",
            recipe,
            p_record->get_route()
        );
        return PackedByteArray();
    }
    if (!parent_is_spawn_target && !plane->anchor_encode(stream, parent)) {
        NETW_ERROR(
            sys::SPAWN,
            "parent of '%s' is outside the MultiplayerTree, the spawn cannot "
            "be addressed",
            p_node->get_name()
        );
        return PackedByteArray();
    }

    if (recipe == Book::RECIPE_ADOPT) {
    } else if (recipe == Book::RECIPE_SCENE) {
        if (!put_scene_recipe(stream, p_record->get_scene_path())) {
            return PackedByteArray();
        }
    } else if (recipe == Book::RECIPE_SPAWNER) {
        if (spawner == nullptr || !plane->anchor_encode(stream, spawner)) {
            NETW_ERROR(
                sys::SPAWN,
                "consumed spawner for route %d is gone or outside the "
                "MultiplayerTree",
                p_record->get_route()
            );
            return PackedByteArray();
        }
        uint64_t scene_index = uint64_t(p_record->get_scene_index() + 1);
        if (!stream.varuint(scene_index, 3)) {
            return PackedByteArray();
        }
        if (p_record->get_scene_index() < 0) {
            PackedByteArray bytes
                = gd::var_to_bytes(p_record->get_custom_data());
            if (!stream.bytes_capped(bytes, BLOB_CAP)) {
                return PackedByteArray();
            }
        }
    } else if (recipe == Book::RECIPE_FN_REGISTRY) {
        const StringName id = p_record->get_fn_registry_id();
        const Callable fn = constructor_of(id);
        if (!fn.is_valid()) {
            NETW_ERROR(
                sys::SPAWN,
                "no spawn constructor registered under id '%s' for route %d",
                id,
                p_record->get_route()
            );
            return PackedByteArray();
        }
        const Variant schema = fn_registry_schema(id, fn);
        if (schema.get_type() == Variant::NIL) {
            NETW_ERROR(
                sys::SPAWN,
                "spawn constructor '%s' for route %d declares no argument "
                "schema, so no receiver could decode the bytes this would "
                "write",
                id,
                p_record->get_route()
            );
            return PackedByteArray();
        }
        String name = String(id);
        PackedByteArray args = encode_fn_args(schema, p_record->get_fn_args());
        if (!wire::string_field(stream, name)
            || !stream.bytes_capped(args, BLOB_CAP)) {
            return PackedByteArray();
        }
    } else if (recipe == Book::RECIPE_FN) {
        Node *host = p_record->fn_host();
        if (host == nullptr || !plane->anchor_encode(stream, host)) {
            NETW_ERROR(
                sys::SPAWN,
                "spawn function host for route %d is gone or outside the "
                "MultiplayerTree",
                p_record->get_route()
            );
            return PackedByteArray();
        }
        const Variant schema
            = fn_script_schema(host->get_script(), p_record->get_fn_method());
        if (schema.get_type() == Variant::NIL) {
            NETW_ERROR(
                sys::SPAWN,
                "spawn function '%s' for route %d is not registered through "
                "Netw.configure_spawn, so no receiver could decode the bytes "
                "this would write",
                p_record->get_fn_method(),
                p_record->get_route()
            );
            return PackedByteArray();
        }
        String name = String(p_record->get_fn_method());
        PackedByteArray args = encode_fn_args(schema, p_record->get_fn_args());
        if (!wire::string_field(stream, name)
            || !stream.bytes_capped(args, BLOB_CAP)) {
            return PackedByteArray();
        }
    }

    const TypedArray<Dictionary> entries = collect_spawn_state(p_node);
    uint64_t state_count = uint64_t(entries.size());
    if (!stream.varuint(state_count, 2)) {
        return PackedByteArray();
    }
    for (int at = 0; at < entries.size(); ++at) {
        const Dictionary entry = entries[at];
        Node *source
            = Object::cast_to<Node>(gd::live_object(entry[StringName("node")]));
        const StringName prop = entry[StringName("prop")];
        const Ref<NetwPropertyConfig> cfg = entry[StringName("cfg")];
        bool by_path = source != p_node;
        String path = by_path ? String(p_node->get_path_to(source)) : String();
        if (!stream.bool1(by_path)
            || (by_path && !wire::string_field(stream, path))) {
            return PackedByteArray();
        }
        Variant token = prop;
        if (encode_prop_val_seam.is_valid()) {
            token = encode_prop_val_seam.call(entity, source, prop);
        }
        PackedByteArray token_bytes = netw::script::model::token_bytes(token);

        Array quantizers;
        const Array declared = cfg.is_valid() ? cfg->get_quantizers() : Array();
        quantizers.push_back(declared.is_empty() ? Variant() : declared[0]);
        Array types;
        types.push_back(
            netw::script::model::get_node_property_type(source, prop)
        );
        Array values;
        values.push_back(source->get(prop));
        wire::WriteStream value_stream;
        if (!call_args::values_write(value_stream, values, quantizers, types)
            || !value_stream.align_verify()) {
            return PackedByteArray();
        }
        PackedByteArray value_bytes = value_stream.to_bytes();

        if (!stream.bytes_capped(token_bytes, wire::STRING_CAP)
            || !stream.bytes_capped(value_bytes, BLOB_CAP)) {
            return PackedByteArray();
        }
    }

    const int64_t local_id = has_peer() ? plane->get_unique_id() : 1;
    const Array native_entries = synchronizers::spawn_state(p_node, local_id);
    uint64_t native_count = uint64_t(native_entries.size());
    if (!stream.varuint(native_count, 2)) {
        return PackedByteArray();
    }
    for (int at = 0; at < native_entries.size(); ++at) {
        const Dictionary entry = native_entries[at];
        String path = String(entry[StringName("path")]);
        PackedByteArray bytes = gd::var_to_bytes(entry[StringName("value")]);
        if (!wire::string_field(stream, path)
            || !stream.bytes_capped(bytes, BLOB_CAP)) {
            return PackedByteArray();
        }
    }

    const PackedByteArray no_descriptors
        = Record::write_descriptors(LocalVector<DescriptorRow>());
    PackedByteArray consumed_bytes = sync_compat != nullptr
        ? sync_compat->encode_descriptors(p_record->get_route())
        : no_descriptors;
    PackedByteArray derived_bytes = encode_derived_seam.is_valid()
        ? PackedByteArray(encode_derived_seam.call(p_record->get_route()))
        : no_descriptors;
    if (!stream.bytes_capped(consumed_bytes, BLOB_CAP)
        || !stream.bytes_capped(derived_bytes, BLOB_CAP)
        || !stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

Node *Pipeline::build_adopt(Object *p_parent, const String &p_name) {
    Node *parent = Object::cast_to<Node>(p_parent);
    return parent != nullptr ? parent->get_node_or_null(NodePath(p_name))
                             : nullptr;
}

Node *Pipeline::build_scene(const Variant &p_packed) {
    const Ref<PackedScene> packed = p_packed;
    return packed.is_valid() ? packed->instantiate() : nullptr;
}

Node *Pipeline::build_spawner(
    Object *p_spawner,
    int64_t p_index,
    const Variant &p_data
) {
    if (spawner_compat == nullptr) {
        return nullptr;
    }
    return spawner_compat->instantiate(p_spawner, int(p_index), p_data);
}

Node *Pipeline::build_fn(const Callable &p_fn, const Array &p_args) {
    const Variant built = p_fn.callv(p_args);
    return Object::cast_to<Node>(gd::live_object(built));
}

Node *Pipeline::build_host_fn(
    Object *p_host,
    const StringName &p_method,
    const Array &p_args
) {
    if (p_host == nullptr) {
        return nullptr;
    }
    const Variant built = Callable(p_host, p_method).callv(p_args);
    return Object::cast_to<Node>(gd::live_object(built));
}

Node *Pipeline::run_construct_stage(const Callable &p_constructor) {
    Object *seam = stage_seam(StringName("_spawn_construct"));
    NetwMultiplayer *plane = core();
    Node *node = nullptr;
    if (seam != nullptr) {
        seam->set(StringName("_spawn_constructor"), p_constructor);
        const Variant built = seam->call("_spawn_construct", RID());
        seam->set(StringName("_spawn_constructor"), Callable());
        node = Object::cast_to<Node>(gd::live_object(built));
    } else if (plane != nullptr) {
        plane->spawn_construct_arm(p_constructor);
        node = plane->spawn_construct(RID());
        plane->spawn_construct_arm(Callable());
    } else {
        const Variant built = p_constructor.call();
        node = Object::cast_to<Node>(gd::live_object(built));
    }
    if (plane != nullptr
        && plane->event_wants(EventPlane::SPAWN_CONSTRUCT, 0)) {
        Dictionary detail;
        detail[StringName("built")] = node != nullptr;
        plane->event_emit(
            EventPlane::SPAWN_CONSTRUCT,
            0,
            detail,
            StringName(),
            0,
            OK,
            Dictionary()
        );
    }
    return node;
}

bool decode_spawn_frame(
    const NetwMultiplayer *p_plane,
    const PackedByteArray &p_payload,
    SpawnFrame &r_frame
) {
    NETW_ZONE_NC("Spawn decode frame", colors::LIVENESS);
    if (p_plane == nullptr) {
        return false;
    }
    wire::ReadStream stream(p_payload);
    SpawnFrame frame;
    if (!NetwMultiplayer::verb_head_read(stream, frame.route, frame.epoch)
        || !Record::decode_header(stream, frame.header)
        || !stream.int_range(frame.recipe, 0, 4)
        || !stream.bool1(frame.parent_is_spawn_target)) {
        return false;
    }
    frame.header[StringName("route")] = frame.route;
    frame.header[StringName("epoch")] = frame.epoch;
    if (frame.parent_is_spawn_target && frame.recipe != Book::RECIPE_SPAWNER) {
        return false;
    }
    if (!frame.parent_is_spawn_target
        && !p_plane->anchor_decode(stream, frame.parent_anchor)) {
        return false;
    }

    if (frame.recipe == Book::RECIPE_SCENE) {
        if (!Pipeline::get_scene_recipe(stream, frame.scene_path)) {
            return false;
        }
    } else if (frame.recipe == Book::RECIPE_SPAWNER) {
        uint64_t staged_index = 0;
        if (!p_plane->anchor_decode(stream, frame.spawner_anchor)
            || !stream.varuint(staged_index, 3)) {
            return false;
        }
        frame.scene_index = int64_t(staged_index) - 1;
        if (frame.scene_index < 0
            && !stream.bytes_capped(frame.custom, BLOB_CAP)) {
            return false;
        }
    } else if (frame.recipe == Book::RECIPE_FN_REGISTRY) {
        if (!wire::string_field(stream, frame.fn_id)
            || !stream.bytes_capped(frame.args, BLOB_CAP)) {
            return false;
        }
    } else if (frame.recipe == Book::RECIPE_FN) {
        if (!p_plane->anchor_decode(stream, frame.host_anchor)
            || !wire::string_field(stream, frame.method)
            || !stream.bytes_capped(frame.args, BLOB_CAP)) {
            return false;
        }
    }

    uint64_t state_count = 0;
    if (!stream.varuint(state_count, 2)) {
        return false;
    }
    for (uint64_t at = 0; at < state_count; ++at) {
        SpawnFrame::StateEntry entry;
        if (!stream.bool1(entry.by_path)) {
            return false;
        }
        if (entry.by_path && !wire::string_field(stream, entry.path)) {
            return false;
        }
        if (!stream.bytes_capped(entry.token, wire::STRING_CAP)
            || !stream.bytes_capped(entry.values, BLOB_CAP)) {
            return false;
        }
        frame.state.push_back(entry);
    }

    uint64_t native_count = 0;
    if (!stream.varuint(native_count, 2)) {
        return false;
    }
    for (uint64_t at = 0; at < native_count; ++at) {
        SpawnFrame::NativeEntry entry;
        if (!wire::string_field(stream, entry.path)
            || !stream.bytes_capped(entry.value, BLOB_CAP)) {
            return false;
        }
        frame.native.push_back(entry);
    }

    if (!stream.bytes_capped(frame.consumed, BLOB_CAP)
        || !stream.bytes_capped(frame.derived, BLOB_CAP)
        || !stream.align_verify() || stream.bits_remaining() != 0) {
        return false;
    }
    r_frame = frame;
    return true;
}

bool Pipeline::frame_read(bool p_healthy, int64_t p_route) {
    if (p_healthy) {
        return true;
    }
    drops_spawn_truncated += 1;
    NetwMultiplayer *plane = core();
    if (plane != nullptr && plane->warn_verdict(ERR_INVALID_DATA, p_route)) {
        NETW_WARN(sys::SPAWN, "SPAWN for route %d ran out of bits", p_route);
    }
    return false;
}

bool Pipeline::admits_frame(
    int64_t p_sender,
    int64_t p_channel,
    const PackedByteArray &p_payload
) {
    NETW_ZONE_NC("Spawn admit frame", colors::LIVENESS);
    NETW_ZONE_VALUE(p_payload.size());
    NetwMultiplayer *plane = core();
    Object *shell = api();
    Error verdict = ERR_UNAVAILABLE;
    if (shell != nullptr && shell != plane
        && shell->has_method(StringName("_spawn_admit_frame"))) {
        verdict = Error(
            int(shell->call(
                "_spawn_admit_frame",
                p_sender,
                0,
                p_channel,
                p_payload
            ))
        );
    } else if (plane != nullptr) {
        verdict = plane->spawn_admit_frame(p_sender, 0, p_channel, p_payload);
    }
    if (stage_verdict(EventPlane::GATE_SPAWN, verdict, 0) != OK) {
        if (p_sender != 1) {
            drops_spawn_bad_sender += 1;
        }
        return false;
    }
    return true;
}

void Pipeline::handle_spawn_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!admits_frame(p_sender, channel_spawn, p_payload)) {
        return;
    }
    try_apply_spawn(p_payload);
}

void Pipeline::try_apply_spawn(const PackedByteArray &p_payload) {
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr) {
        return;
    }
    SpawnFrame frame;
    if (!decode_spawn_frame(plane, p_payload, frame)) {
        frame_read(false, 0);
        return;
    }

    const int64_t route = frame.route;
    const Dictionary &header = frame.header;
    const int64_t recipe = frame.recipe;
    const Dictionary &parent_anchor = frame.parent_anchor;

    if (!plane->liveness_epoch_admits(route, frame.epoch)) {
        drops_spawn_stale_life += 1;
        return;
    }
    if (spawn_book
            .spawn_is_duplicate(route, plane->liveness_route_state(route))) {
        drops_spawn_duplicate += 1;
        return;
    }

    const int64_t action_spawn_tick
        = int64_t(header[StringName("action_spawn_tick")]);
    const String node_name = header[StringName("node_name")];

    if (!frame.parent_is_spawn_target) {
        const int64_t parent_route
            = int64_t(parent_anchor[StringName("route")]);
        if (parent_route > 0
            && Park::anchor_parks(plane->liveness_route_state(parent_route))) {
            park_spawn(p_payload, parent_route, route);
            return;
        }
    }

    Node *node = nullptr;
    Node *elided_parent = nullptr;
    MultiplayerSpawner *recv_spawner = nullptr;
    bool adopted = false;

    if (recipe == Book::RECIPE_ADOPT) {
        Node *adopt_parent = plane->anchor_resolve(parent_anchor);
        node = run_construct_stage(
            callable_mp(plane, &NetwMultiplayer::spawn_build_adopt)
                .bind(adopt_parent, node_name)
        );
        if (node == nullptr) {
            park_spawn_for_adopt(p_payload, route);
            return;
        }
        adopted = true;
    } else if (recipe == Book::RECIPE_SCENE) {
        const String scene_path = frame.scene_path;
        Ref<PackedScene> packed;
        if (!scene_path.is_empty() && gd::resource_available(scene_path)) {
            packed = gd::load_scene(scene_path);
        }
        if (packed.is_null()) {
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d names an unknown scene '%s'",
                route,
                scene_path
            );
            drops_spawn_unresolved += 1;
            return;
        }
        node = run_construct_stage(
            callable_mp(plane, &NetwMultiplayer::spawn_build_scene).bind(packed)
        );
    } else if (recipe == Book::RECIPE_SPAWNER) {
        const Dictionary &spawner_anchor = frame.spawner_anchor;
        const int64_t spawner_route
            = int64_t(spawner_anchor[StringName("route")]);
        if (spawner_route > 0
            && Park::anchor_parks(plane->liveness_route_state(spawner_route))) {
            park_spawn(p_payload, spawner_route, route);
            return;
        }
        recv_spawner = Object::cast_to<MultiplayerSpawner>(
            plane->anchor_resolve(spawner_anchor)
        );
        if (recv_spawner == nullptr) {
            park_spawn_for_scene(p_payload, route);
            return;
        }
        const int64_t scene_index = frame.scene_index;
        Variant data;
        if (scene_index < 0) {
            data = gd::bytes_to_var(frame.custom);
        }
        if (frame.parent_is_spawn_target) {
            elided_parent = recv_spawner->get_node_or_null(
                recv_spawner->get_spawn_path()
            );
            if (elided_parent == nullptr) {
                NETW_WARN(
                    sys::SPAWN,
                    "SPAWN for route %d elides its parent anchor and this "
                    "peer's spawner names no spawn target",
                    route
                );
                drops_spawn_unresolved += 1;
                return;
            }
        }
        node = run_construct_stage(
            callable_mp(plane, &NetwMultiplayer::spawn_build_spawner)
                .bind(recv_spawner, scene_index, data)
        );
        if (node == nullptr) {
            NETW_WARN(
                sys::SPAWN,
                "consumed spawner could not reconstruct route %d",
                route
            );
            drops_spawn_unresolved += 1;
            return;
        }
    } else if (recipe == Book::RECIPE_FN_REGISTRY) {
        const StringName id = StringName(frame.fn_id);
        const Callable fn = constructor_of(id);
        if (!fn.is_valid()) {
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d names spawn constructor '%s' that is not "
                "registered",
                route,
                id
            );
            drops_spawn_unresolved += 1;
            return;
        }
        const Variant schema = fn_registry_schema(id, fn);
        if (schema.get_type() == Variant::NIL) {
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d names constructor '%s' whose arguments "
                "are not configured",
                route,
                id
            );
            drops_spawn_unresolved += 1;
            return;
        }
        const LocalVector<call_args::Slot> encoded
            = read_fn_args(frame.args, schema);
        const Variant resolved = resolve_spawn_args(encoded, p_payload, route);
        if (resolved.get_type() == Variant::NIL) {
            return;
        }
        node = run_construct_stage(
            callable_mp(plane, &NetwMultiplayer::spawn_build_fn)
                .bind(fn, Array(resolved))
        );
        if (node == nullptr) {
            NETW_WARN(
                sys::SPAWN,
                "spawn constructor '%s' did not return a Node for route %d",
                id,
                route
            );
            drops_spawn_unresolved += 1;
            return;
        }
    } else if (recipe == Book::RECIPE_FN) {
        const Dictionary &host_anchor = frame.host_anchor;
        const int64_t host_route = int64_t(host_anchor[StringName("route")]);
        if (host_route > 0
            && Park::anchor_parks(plane->liveness_route_state(host_route))) {
            park_spawn(p_payload, host_route, route);
            return;
        }
        Node *host = plane->anchor_resolve(host_anchor);
        if (host == nullptr) {
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d cannot resolve its spawn function host",
                route
            );
            drops_spawn_unresolved += 1;
            return;
        }
        const StringName method = StringName(frame.method);
        const Variant schema = fn_script_schema(host->get_script(), method);
        if (schema.get_type() == Variant::NIL) {
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d names spawn function '%s' that is not "
                "registered on '%s'",
                route,
                method,
                host->get_name()
            );
            drops_spawn_unresolved += 1;
            return;
        }
        const LocalVector<call_args::Slot> encoded
            = read_fn_args(frame.args, schema);
        const Variant resolved = resolve_spawn_args(encoded, p_payload, route);
        if (resolved.get_type() == Variant::NIL) {
            return;
        }
        node = run_construct_stage(
            callable_mp(plane, &NetwMultiplayer::spawn_build_host_fn)
                .bind(host, method, Array(resolved))
        );
        if (node == nullptr) {
            NETW_WARN(
                sys::SPAWN,
                "spawn function '%s' did not return a Node for route %d",
                method,
                route
            );
            drops_spawn_unresolved += 1;
            return;
        }
    }

    Node *parent = elided_parent != nullptr
        ? elided_parent
        : plane->anchor_resolve(parent_anchor);
    if (parent == nullptr) {
        NETW_WARN(
            sys::SPAWN,
            "SPAWN for route %d cannot resolve its parent anchor '%s'",
            route,
            String(parent_anchor[StringName("path")])
        );
        drops_spawn_unresolved += 1;
        if (!adopted && node != nullptr) {
            node->queue_free();
        }
        return;
    }

    const Ref<NetwEntity> entity = Record::stamp_header(node, header, shell);
    if (action_spawn_tick >= 0) {
        ensure_action_gate_connection();
    }

    for (uint32_t at = 0; at < frame.state.size(); ++at) {
        const SpawnFrame::StateEntry &entry = frame.state[at];
        Node *target = entry.by_path
            ? node->get_node_or_null(NodePath(entry.path))
            : node;
        const Variant prop_token
            = netw::script::model::token_of_bytes(entry.token);
        const PackedByteArray &value_bytes = entry.values;
        if (target == nullptr) {
            continue;
        }
        StringName prop;
        if (prop_token.get_type() == Variant::INT) {
            prop = netw::script::model::get_property_name_by_id(
                target->get_script(),
                int64_t(prop_token)
            );
        } else {
            prop = StringName(prop_token);
        }
        if (String(prop).is_empty() || !gd::has_property(target, prop)) {
            continue;
        }
        Array quantizers;
        Variant quantizer;
        const Dictionary configs
            = netw::script::model::get_node_property_configs(target);
        if (configs.has(prop)) {
            const Ref<NetwPropertyConfig> cfg = configs[prop];
            if (cfg.is_valid() && !cfg->get_quantizers().is_empty()) {
                quantizer = cfg->get_quantizers()[0];
            }
        }
        quantizers.push_back(quantizer);
        Array types;
        types.push_back(
            netw::script::model::get_node_property_type(target, prop)
        );
        wire::ReadStream value_stream(value_bytes);
        Array values;
        if (!call_args::values_read(value_stream, quantizers, types, values)
            || !value_stream.align_verify()
            || value_stream.bits_remaining() != 0) {
            continue;
        }
        if (!values.is_empty()) {
            target->set(prop, values[0]);
        }
    }

    for (uint32_t at = 0; at < frame.native.size(); ++at) {
        Record::apply_native_entry(
            node,
            frame.native[at].path,
            frame.native[at].value
        );
    }
    if (sync_compat != nullptr) {
        sync_compat->note_schema(
            route,
            Record::descriptors_of_bytes(frame.consumed)
        );
    }
    if (note_derived_seam.is_valid()) {
        note_derived_seam.call(
            route,
            Record::descriptors_of_bytes(frame.derived)
        );
    }

    if (adopted) {
        spawn_book.enroll_recv(route, node);
        plane->liveness_bind_route(route, entity.ptr());
        plane->liveness_adopt_epoch(route, frame.epoch);
        return;
    }

    if (!node_name.is_empty()) {
        if (parent->has_node(NodePath(node_name))) {
            node->set_name(vformat("%s@%d", node_name, route));
        } else {
            node->set_name(node_name);
        }
    }

    spawn_book.enroll_recv(route, node);
    plane->liveness_adopt_epoch(route, frame.epoch);
    if (recv_spawner != nullptr && spawner_compat != nullptr) {
        spawner_compat->note_recv(route, recv_spawner);
    }
    plane->spawn_place_node(parent, node);
    if (recv_spawner != nullptr && spawner_compat != nullptr) {
        spawner_compat->emit_spawned(recv_spawner, node);
    }
}

void Pipeline::handle_despawn_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!admits_frame(p_sender, channel_despawn, p_payload)) {
        return;
    }
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    wire::ReadStream reader(p_payload);
    int64_t route = 0;
    int64_t epoch = 0;
    if (!NetwMultiplayer::verb_head_read(reader, route, epoch)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        frame_read(false, route);
        return;
    }
    if (!plane->liveness_epoch_admits(route, epoch)) {
        drops_spawn_stale_life += 1;
        return;
    }
    plane->liveness_adopt_epoch(route, epoch);
    if (park.cancel(route)) {
        spawn_parked_cancelled += 1;
        return;
    }
    if (!spawn_book.is_recv(route)) {
        drops_despawn_unknown += 1;
        return;
    }
    spawn_book.drop_recv(route);

    const Ref<NetwEntity> entity = plane->wrapper_for_route(route);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        drops_despawn_unknown += 1;
        return;
    }
    Node *node = entity->get_owner();

    const Ref<NetwDespawnConfig> cfg
        = netw::script::model::get_despawn_config(node->get_script());
    double linger = 0.0;
    if (cfg.is_valid()) {
        const StringName hook = cfg->get_hook_method();
        if (!String(hook).is_empty() && node->has_method(hook)) {
            node->call(hook);
        }
        linger = cfg->get_linger_seconds();
    }
    entity->_remote_despawn(StringName("despawn"), linger);
    if (linger > 0.0) {
        double rate = CLOCKLESS_TICKRATE;
        const ClockEngine &clock = plane->clock_engine();
        if (clock.get_configured()) {
            rate = double(clock.get_tickrate());
        }
        plane->session_defer_after(
            callable_mp(plane, &NetwMultiplayer::spawn_free_despawned)
                .bind(route),
            StringName(vformat("spawn-free?%d", route)),
            int(ClockEngine::pumps_for(linger, rate))
        );
        return;
    }
    free_despawned(route);
}

void Pipeline::handle_hide_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!admits_frame(p_sender, channel_hide, p_payload)) {
        return;
    }
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    wire::ReadStream reader(p_payload);
    int64_t route = 0;
    int64_t epoch = 0;
    if (!NetwMultiplayer::verb_head_read(reader, route, epoch)
        || !reader.align_verify() || reader.bits_remaining() != 0) {
        frame_read(false, route);
        return;
    }
    if (!plane->liveness_epoch_admits(route, epoch)) {
        drops_spawn_stale_life += 1;
        return;
    }
    plane->liveness_adopt_epoch(route, epoch);
    if (park.cancel(route)) {
        spawn_parked_cancelled += 1;
        return;
    }
    if (!spawn_book.is_recv(route)) {
        drops_hide_unknown += 1;
        return;
    }
    spawn_book.drop_recv(route);

    const Ref<NetwEntity> entity = plane->wrapper_for_route(route);
    if (!plane->liveness_hide(route)) {
        drops_hide_unknown += 1;
        return;
    }
    if (entity.is_null()) {
        return;
    }
    entity->_remote_hide();
    free_route_node(route, entity->get_owner());
}

void Pipeline::handle_reparent_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (!admits_frame(p_sender, channel_reparent, p_payload)) {
        return;
    }
    NetwMultiplayer *plane = core();
    Object *shell = api();
    if (plane == nullptr) {
        return;
    }
    wire::ReadStream reader(p_payload);
    int64_t route = 0;
    int64_t epoch = 0;
    Dictionary anchor;
    if (!NetwMultiplayer::verb_head_read(reader, route, epoch)
        || !plane->anchor_decode(reader, anchor) || !reader.align_verify()
        || reader.bits_remaining() != 0) {
        frame_read(false, route);
        return;
    }
    if (!plane->liveness_epoch_admits(route, epoch)) {
        drops_spawn_stale_life += 1;
        return;
    }
    plane->liveness_adopt_epoch(route, epoch);

    const Ref<NetwEntity> entity = plane->wrapper_for_route(route);
    if (entity.is_null() || entity->get_owner() == nullptr) {
        drops_spawn_unresolved += 1;
        return;
    }
    const int64_t anchor_route = int64_t(anchor[StringName("route")]);
    if (anchor_route > 0
        && Park::anchor_parks(plane->liveness_route_state(anchor_route))) {
        spawn_deferrals += 1;
        plane->liveness_when_live(
            anchor_route,
            callable_mp(plane, &NetwMultiplayer::spawn_handle_reparent_frame)
                .bind(p_payload, int64_t(1)),
            park_timeout_ticks(),
            Callable()
        );
        return;
    }
    Node *parent = plane->anchor_resolve(anchor);
    if (parent == nullptr) {
        NETW_WARN(
            sys::SPAWN,
            "REPARENT for route %d names anchor %d, which this peer cannot "
            "resolve and can no longer be shown",
            int(route),
            int(anchor_route)
        );
        drops_spawn_unresolved += 1;
        return;
    }
    plane->spawn_reparent_node(
        entity->get_owner(),
        parent,
        callable_mp(plane, &NetwMultiplayer::scene_adopt_entity)
    );
}

void Pipeline::free_despawned(int64_t p_route) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Ref<NetwEntity> entity = plane->wrapper_for_route(p_route);
    if (entity.is_null()) {
        return;
    }
    free_route_node(p_route, entity->get_owner());
}

void Pipeline::free_route_node(int64_t p_route, Node *p_node) {
    if (p_node == nullptr) {
        return;
    }
    Node *parent = p_node->get_parent();
    if (parent != nullptr) {
        parent->remove_child(p_node);
    }
    if (spawner_compat != nullptr) {
        spawner_compat->emit_despawned(p_route, p_node);
    }
    p_node->queue_free();
}

int64_t Pipeline::park_timeout_ticks() const {
    NetwMultiplayer *plane = core();
    double tickrate = CLOCKLESS_TICKRATE;
    if (plane != nullptr) {
        const ClockEngine &clock = plane->clock_engine();
        if (clock.get_configured()) {
            tickrate = double(clock.get_tickrate());
        }
    }
    return ClockEngine::pumps_for(park_timeout_seconds, tickrate);
}

void Pipeline::park_spawn(
    const PackedByteArray &p_payload,
    int64_t p_dep_route,
    int64_t p_route
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    spawn_deferrals += 1;
    park.park(p_route, p_payload, Park::WAIT_ROUTE, 0);
    plane->liveness_when_live(
        p_dep_route,
        callable_mp(plane, &NetwMultiplayer::spawn_retry_parked).bind(p_route),
        park_timeout_ticks(),
        callable_mp(plane, &NetwMultiplayer::spawn_expire_parked).bind(p_route)
    );
}

void Pipeline::retry_parked(int64_t p_route) {
    const PackedByteArray parked = park.take(p_route);
    if (!parked.is_empty()) {
        try_apply_spawn(parked);
    }
}

void Pipeline::expire_parked(int64_t p_route) {
    if (park.cancel(p_route)) {
        spawn_park_expired += 1;
    }
}

void Pipeline::park_spawn_for_scene(
    const PackedByteArray &p_payload,
    int64_t p_route
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        drops_spawn_unresolved += 1;
        return;
    }
    const Time *reading = Time::get_singleton();
    const int64_t now
        = reading != nullptr ? int64_t(reading->get_ticks_msec()) : 0;
    spawn_deferrals += 1;
    park.park(
        p_route,
        p_payload,
        Park::WAIT_SCENE,
        now + int64_t(park_timeout_seconds * 1000.0)
    );
    connect_once(
        plane,
        SIG_ENTITY_LIVE,
        callable_mp(plane, &NetwMultiplayer::spawn_retry_scene_parked_spawns),
        0
    );
}

void Pipeline::retry_scene_parked_spawns(int64_t, const Ref<NetwEntity> &) {
    const Time *reading = Time::get_singleton();
    const int64_t now
        = reading != nullptr ? int64_t(reading->get_ticks_msec()) : 0;
    const PackedInt64Array waiting = park.waiting_on(Park::WAIT_SCENE);
    for (int at = 0; at < waiting.size(); ++at) {
        const int64_t parked_route = waiting[at];
        const bool expired = park.is_expired(parked_route, now);
        const PackedByteArray parked = park.take(parked_route);
        if (expired) {
            spawn_park_expired += 1;
            NETW_WARN(
                sys::SPAWN,
                "SPAWN for route %d gave up waiting for its consumed "
                "spawner's scene",
                parked_route
            );
            continue;
        }
        try_apply_spawn(parked);
    }
    if (park.waiting_on(Park::WAIT_SCENE).is_empty()) {
        drop_scene_park_retry();
    }
}

void Pipeline::park_spawn_for_adopt(
    const PackedByteArray &p_payload,
    int64_t p_route
) {
    const Time *reading = Time::get_singleton();
    const int64_t now
        = reading != nullptr ? int64_t(reading->get_ticks_msec()) : 0;
    if (park.park(
            p_route,
            p_payload,
            Park::WAIT_ADOPT,
            now + int64_t(park_timeout_seconds * 1000.0)
        )) {
        spawn_deferrals += 1;
    }
}

void Pipeline::retry_adopt_parked() {
    const PackedInt64Array waiting = park.waiting_on(Park::WAIT_ADOPT);
    if (waiting.is_empty()) {
        return;
    }
    const Time *reading = Time::get_singleton();
    const int64_t now
        = reading != nullptr ? int64_t(reading->get_ticks_msec()) : 0;
    for (int at = 0; at < waiting.size(); ++at) {
        const int64_t parked_route = waiting[at];
        if (park.is_expired(parked_route, now)) {
            park.cancel(parked_route);
            spawn_park_expired += 1;
            drops_spawn_unresolved += 1;
            NETW_WARN(
                sys::SPAWN,
                "ADOPT for route %d gave up waiting for a node named by the "
                "authority; the peers' structure must match",
                parked_route
            );
            continue;
        }
        try_apply_spawn(park.peek(parked_route));
        if (spawn_book.is_recv(parked_route)) {
            park.cancel(parked_route);
        }
    }
}

void Pipeline::drop_scene_park_retry() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const Callable retry
        = callable_mp(plane, &NetwMultiplayer::spawn_retry_scene_parked_spawns);
    if (plane->is_connected(SIG_ENTITY_LIVE, retry)) {
        plane->disconnect(SIG_ENTITY_LIVE, retry);
    }
}

int64_t Pipeline::gate_display_tick() const {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return -1;
    }
    const ClockEngine &clock = plane->clock_engine();
    if (clock.get_configured()) {
        return clock.display_tick();
    }
    return -1;
}

void Pipeline::apply_action_gate(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    const int64_t display_tick = gate_display_tick();
    if (display_tick < 0) {
        return;
    }
    if (plane->action_gate_arm(p_route, p_entity, display_tick)) {
        connect_once(
            plane,
            SIG_CLOCK_ON_TICK,
            callable_mp(plane, &NetwMultiplayer::spawn_on_action_reveal_tick),
            0
        );
    }
}

void Pipeline::on_action_reveal_tick(double, int64_t p_tick) {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    int64_t reached = p_tick;
    const ClockEngine &clock = plane->clock_engine();
    if (clock.get_configured()) {
        const int64_t shifted = p_tick - clock.get_display_offset();
        reached = shifted > 0 ? shifted : 0;
    }
    plane->action_gate_sweep(reached);
    disconnect_action_reveal_if_idle();
}

void Pipeline::disconnect_action_reveal_if_idle() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr || plane->action_gate_count() > 0) {
        return;
    }
    const Callable ticked
        = callable_mp(plane, &NetwMultiplayer::spawn_on_action_reveal_tick);
    if (plane->is_connected(SIG_CLOCK_ON_TICK, ticked)) {
        plane->disconnect(SIG_CLOCK_ON_TICK, ticked);
    }
}

void Pipeline::ensure_action_gate_connection() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    connect_once(
        plane,
        SIG_ENTITY_LIVE,
        callable_mp(plane, &NetwMultiplayer::spawn_apply_action_gate),
        0
    );
}

void Pipeline::clear_action_gates() {
    NetwMultiplayer *plane = core();
    if (plane == nullptr) {
        return;
    }
    plane->action_gate_clear();
    disconnect_action_reveal_if_idle();
    const Callable gate
        = callable_mp(plane, &NetwMultiplayer::spawn_apply_action_gate);
    if (plane->is_connected(SIG_ENTITY_LIVE, gate)) {
        plane->disconnect(SIG_ENTITY_LIVE, gate);
    }
}

void Pipeline::clear_session() {
    drop_scene_park_retry();
    park.clear();
    spawn_book.clear();
    clear_action_gates();
}

void Pipeline::clear_route(int64_t p_route) {
    spawn_book.drop_armed(p_route);
    spawn_book.drop_recv(p_route);
    NetwMultiplayer *plane = core();
    if (plane != nullptr) {
        plane->action_gate_drop(p_route);
        disconnect_action_reveal_if_idle();
    }
}

Dictionary Pipeline::counters() const {
    Dictionary out;
    out[StringName("drops_spawn_bad_sender")] = drops_spawn_bad_sender;
    out[StringName("drops_spawn_duplicate")] = drops_spawn_duplicate;
    out[StringName("drops_spawn_unresolved")] = drops_spawn_unresolved;
    out[StringName("drops_spawn_truncated")] = drops_spawn_truncated;
    out[StringName("drops_spawn_stale_life")] = drops_spawn_stale_life;
    out[StringName("drops_despawn_unknown")] = drops_despawn_unknown;
    out[StringName("drops_hide_unknown")] = drops_hide_unknown;
    out[StringName("spawn_deferrals")] = spawn_deferrals;
    out[StringName("moves_unadmitted")] = moves_unadmitted;
    out[StringName("spawn_parked_cancelled")] = spawn_parked_cancelled;
    out[StringName("spawn_park_expired")] = spawn_park_expired;
    out[StringName("spawn_book_armed")] = spawn_book.armed_count();
    out[StringName("spawn_book_spawned")] = spawn_book.spawned_count();
    out[StringName("spawn_book_recv")] = spawn_book.recv_count();
    return out;
}

} // namespace netw::spawn

namespace netw {

spawn::Pipeline *NetwMultiplayer::spawn_plane() const {
    ReplicationCore *plane = get_replication_plane();
    return plane != nullptr ? plane->get_spawn_pipeline() : nullptr;
}

void NetwMultiplayer::spawn_on_peer_connected(int64_t p_peer_id) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->on_peer_connected(p_peer_id);
    }
}

PackedByteArray NetwMultiplayer::spawn_encode_spawn_frame(
    int64_t p_route,
    Node *p_node
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr ? pipeline->encode_spawn_frame(p_route, p_node)
                               : PackedByteArray();
}

void NetwMultiplayer::spawn_run_visibility_sweep() {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->run_visibility_sweep();
    }
}

void NetwMultiplayer::spawn_run_carrier_flush() {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->run_carrier_flush();
    }
}

Error NetwMultiplayer::spawn_declare_stage(
    const RID &p_handle,
    const Dictionary &p_facts
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr ? pipeline->declare_stage(p_handle, p_facts)
                               : ERR_UNAVAILABLE;
}

void NetwMultiplayer::spawn_on_armed_tree_entered(int64_t p_route) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->on_armed_tree_entered(p_route);
    }
}

void NetwMultiplayer::spawn_flush_armed_spawn(int64_t p_route) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->flush_armed_spawn(p_route);
    }
}

Node *NetwMultiplayer::spawn_build_adopt(
    Object *p_parent,
    const String &p_name
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr ? pipeline->build_adopt(p_parent, p_name)
                               : nullptr;
}

Node *NetwMultiplayer::spawn_build_scene(const Variant &p_packed) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr ? pipeline->build_scene(p_packed) : nullptr;
}

Node *NetwMultiplayer::spawn_build_spawner(
    Object *p_spawner,
    int64_t p_index,
    const Variant &p_data
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr
        ? pipeline->build_spawner(p_spawner, p_index, p_data)
        : nullptr;
}

Node *NetwMultiplayer::spawn_build_fn(
    const Callable &p_fn,
    const Array &p_args
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr ? pipeline->build_fn(p_fn, p_args) : nullptr;
}

Node *NetwMultiplayer::spawn_build_host_fn(
    Object *p_host,
    const StringName &p_method,
    const Array &p_args
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr
        ? pipeline->build_host_fn(p_host, p_method, p_args)
        : nullptr;
}

void NetwMultiplayer::spawn_free_despawned(int64_t p_route) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->free_despawned(p_route);
    }
}

void NetwMultiplayer::spawn_handle_spawn_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->handle_spawn_frame(p_payload, p_sender);
    }
}

void NetwMultiplayer::spawn_handle_despawn_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->handle_despawn_frame(p_payload, p_sender);
    }
}

void NetwMultiplayer::spawn_handle_hide_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->handle_hide_frame(p_payload, p_sender);
    }
}

void NetwMultiplayer::spawn_handle_reparent_frame(
    const PackedByteArray &p_payload,
    int64_t p_sender
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->handle_reparent_frame(p_payload, p_sender);
    }
}

void NetwMultiplayer::spawn_retry_parked(int64_t p_route) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->retry_parked(p_route);
    }
}

void NetwMultiplayer::spawn_expire_parked(int64_t p_route) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->expire_parked(p_route);
    }
}

void NetwMultiplayer::spawn_retry_scene_parked_spawns(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->retry_scene_parked_spawns(p_route, p_entity);
    }
}

void NetwMultiplayer::spawn_on_action_reveal_tick(
    double p_delta,
    int64_t p_tick
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->on_action_reveal_tick(p_delta, p_tick);
    }
}

void NetwMultiplayer::spawn_apply_action_gate(
    int64_t p_route,
    const Ref<NetwEntity> &p_entity
) {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->apply_action_gate(p_route, p_entity);
    }
}

void NetwMultiplayer::spawn_schedule_visibility_sweep() {
    if (spawn::Pipeline *pipeline = spawn_plane()) {
        pipeline->schedule_visibility_sweep();
    }
}

bool NetwMultiplayer::spawn_books_node(Node *p_node) const {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr && pipeline->books_node(p_node);
}

Ref<NetwEntity> NetwMultiplayer::spawn_arm_consumed(
    Node *p_node,
    Object *p_spawner,
    int p_scene_index,
    const Variant &p_data
) {
    spawn::Pipeline *pipeline = spawn_plane();
    return pipeline != nullptr
        ? pipeline->arm_consumed_spawn(p_node, p_spawner, p_scene_index, p_data)
        : Ref<NetwEntity>();
}

} // namespace netw
