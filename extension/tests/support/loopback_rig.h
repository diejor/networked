#pragma once

#include "netw_test.h"

#include "carrier.h"
#include "entity_decl.h"
#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/api/prediction_handle.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/replication_core.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/script/model.hpp"
#include "netw/spawn/book.hpp"
#include "netw/spawn/pipeline.hpp"
#include "netw/spawn/record.hpp"
#include "netw/wire/stream.hpp"
#include "world_decl.h"

#if defined(NETW_TIER_HOSTED)
#include <godot_cpp/classes/multiplayer_api.hpp>
#include <godot_cpp/classes/node2d.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_multiplayer.hpp>
#include <godot_cpp/classes/script.hpp>
#include <godot_cpp/variant/rid.hpp>
#endif

namespace netw_test {

#if defined(NETW_TIER_HOSTED)

class LoopbackRig {
    godot::Ref<netw::LocalLoopbackSession> link;
    godot::Ref<netw::NetwMultiplayer> server_api;
    godot::Vector<godot::Ref<netw::NetwMultiplayer>> client_apis;
    godot::Vector<godot::Ref<netw::LocalMultiplayerPeer>> client_peers;
    godot::Vector<godot::HashMap<godot::StringName, godot::RID>>
        client_declared;
    godot::Vector<godot::Node *> owned_nodes;
    godot::Vector<godot::Node *> mounts;
    godot::Vector<godot::NodePath> mounted_paths;
    godot::RID last_spawn;
    godot::HashMap<godot::StringName, godot::RID> declared;
    godot::HashMap<godot::StringName, godot::RID> declared_state_sets;
    godot::Vector<godot::HashMap<godot::StringName, godot::RID>>
        client_state_sets;
    double tick_period_ms = 1000.0 / 30.0;

    static godot::Ref<netw::NetwMultiplayer> make_api(
        const godot::Ref<netw::LocalMultiplayerPeer> &p_peer
    ) {
        godot::Ref<godot::SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(godot::NodePath("/"));

        godot::Ref<netw::NetwMultiplayer> api
            = netw::NetwMultiplayer::make(inner, godot::Ref<godot::Script>());
        REQUIRE_MESSAGE(api.is_valid(), "the session did not instantiate");
        if (api.is_valid()) {
            api->set("multiplayer_peer", p_peer);
        }
        return api;
    }

    static godot::Ref<godot::RefCounted> make_resource(const char *p_path) {
        godot::Ref<godot::Script> script
            = godot::ResourceLoader::get_singleton()->load(p_path);
        REQUIRE_MESSAGE(script.is_valid(), "the config script did not load");
        if (script.is_null()) {
            return godot::Ref<godot::RefCounted>();
        }
        return script->call("new");
    }

    static godot::Node2D *make_marker() {
        return memnew(godot::Marker2D);
    }

    static void install_services(
        netw::NetwMultiplayer *p_api,
        const WorldDecl &p_world
    ) {
        REQUIRE_MESSAGE(p_api != nullptr, "a service needs a session");
        if (p_world.wants_clock) {
            godot::Ref<netw::NetwClockConfig> config;
            config.instantiate();
            REQUIRE(config.is_valid());
            config->set("tickrate", p_world.clock_tickrate);
            config->set("display_offset", p_world.clock_display_offset);
            NETW_CHECK_EQ(int(p_api->clock_initialize(config)), int(godot::OK));
            p_api->clock_engine().set_manual_tick(true);
        }
        if (p_world.wants_lagcomp) {
            NETW_CHECK_EQ(
                int(p_api->lagcomp_initialize(8, 12)),
                int(godot::OK)
            );
        }
    }

    godot::RID stand_up(
        netw::NetwMultiplayer *p_api,
        const EntityDecl &p_decl,
        godot::Node *p_owner,
        bool p_grant_control
    ) const {
        netw::NetwMultiplayer *api = p_api;
        REQUIRE_MESSAGE(api != nullptr, "a declaration needs a session");
        godot::RID entity = p_decl.route() > 0
            ? api->entity_from_route(p_decl.route())
            : godot::RID();
        if (!entity.is_valid()) {
            entity = api->entity_create();
        }
        REQUIRE_MESSAGE(entity.is_valid(), "entity_create returned no handle");

        const int route = p_decl.route() > 0 ? p_decl.route()
                                             : int(api->entity_admit(entity));
        const godot::Error bound = api->entity_bind_route(entity, route);
        REQUIRE_MESSAGE(bool(bound == godot::OK), "the route did not bind");

        if (p_owner != nullptr) {
            const godot::Error owned = api->entity_bind_node(entity, p_owner);
            REQUIRE_MESSAGE(bool(owned == godot::OK), "the owner did not bind");
        }
        if (p_grant_control && p_decl.controller() != 0) {
            api->entity_grant_control(entity, p_decl.controller());
        }
        return entity;
    }

    static godot::RID attach_state(
        netw::NetwMultiplayer *p_api,
        const godot::RID &p_entity,
        godot::Node *p_owner,
        const EntityDecl &p_decl
    ) {
        if (p_decl.schema().is_empty()) {
            return godot::RID();
        }
        REQUIRE_MESSAGE(p_owner != nullptr, "a schema needs a carrier");
        if (p_owner == nullptr) {
            return godot::RID();
        }
        const godot::RID schema = p_api->schema_create(p_decl.schema());
        for (const godot::StringName &field : p_decl.synced_columns()) {
            const godot::Variant value = p_owner->get(field);
            REQUIRE_MESSAGE(
                value.get_type() != godot::Variant::NIL,
                "a synced field needs a carrier property"
            );
            p_api->schema_add_column(
                schema,
                field,
                column_type(value.get_type()),
                1
            );
        }
        p_api->schema_seal(schema);
        const godot::RID state = p_api->property_set_create(
            schema,
            netw::NetwMultiplayer::RECORD_KIND_STATE
        );
        for (int index = 0; index < p_decl.synced_columns().size(); ++index) {
            p_api->property_set_add_column(state, index);
        }
        p_api->property_set_seal(state);
        REQUIRE_MESSAGE(
            int(p_api->entity_add_property_set(p_entity, state, 0))
                == int(godot::OK),
            "the declared state set did not attach"
        );
        return state;
    }

    static netw::NetwMultiplayer::ColumnType column_type(
        godot::Variant::Type p_type
    ) {
        switch (p_type) {
            case godot::Variant::FLOAT:
                return netw::NetwMultiplayer::COLUMN_F64;
            case godot::Variant::INT:
                return netw::NetwMultiplayer::COLUMN_I64;
            case godot::Variant::BOOL:
                return netw::NetwMultiplayer::COLUMN_BOOL;
            case godot::Variant::VECTOR2:
                return netw::NetwMultiplayer::COLUMN_VECTOR2;
            case godot::Variant::VECTOR3:
                return netw::NetwMultiplayer::COLUMN_VECTOR3;
            case godot::Variant::VECTOR4:
                return netw::NetwMultiplayer::COLUMN_VECTOR4;
            case godot::Variant::COLOR:
                return netw::NetwMultiplayer::COLUMN_COLOR;
            case godot::Variant::QUATERNION:
                return netw::NetwMultiplayer::COLUMN_QUATERNION;
            default:
                return netw::NetwMultiplayer::COLUMN_VARIANT;
        }
    }

    static void attach_input(
        netw::NetwMultiplayer *p_api,
        const godot::RID &p_entity,
        Carrier *p_owner,
        const EntityDecl &p_decl
    ) {
        if (!p_decl.is_predicted()) {
            return;
        }
        REQUIRE_MESSAGE(p_owner != nullptr, "prediction needs a carrier");
        const godot::StringName schema_name(
            godot::String(p_decl.schema()) + "Input"
        );
        const godot::RID schema = p_api->schema_create(schema_name);
        p_api->schema_add_column(
            schema,
            godot::StringName("motion"),
            netw::NetwMultiplayer::COLUMN_VECTOR2,
            1
        );
        p_api->schema_add_column(
            schema,
            godot::StringName("bombing"),
            netw::NetwMultiplayer::COLUMN_BOOL,
            1
        );
        p_api->schema_seal(schema);
        const godot::RID input = p_api->property_set_create(
            schema,
            netw::NetwMultiplayer::RECORD_KIND_INPUT
        );
        p_api->property_set_add_column(input, 0);
        p_api->property_set_add_column(input, 1);
        p_api->property_set_seal(input);
        REQUIRE_MESSAGE(
            int(p_api->entity_add_property_set(p_entity, input, 0))
                == int(godot::OK),
            "the declared input set did not attach"
        );
    }

    static void configure_prediction(
        godot::Object *p_shell,
        const godot::RID &p_entity,
        Carrier *p_owner,
        const EntityDecl &p_decl
    ) {
        if (!p_decl.is_predicted()) {
            return;
        }
        netw::NetwMultiplayer *p_api = core_of(p_shell);
        p_api->predict_set_param(
            p_entity,
            netw::NetwMultiplayer::PREDICT_PARAM_SCHEDULE,
            int(p_decl.schedule())
        );
        p_api->predict_set_param(
            p_entity,
            netw::NetwMultiplayer::PREDICT_PARAM_DIVERGENCE_EPSILON,
            p_decl.prediction_epsilon()
        );
        p_api->predict_set_param(
            p_entity,
            netw::NetwMultiplayer::PREDICT_PARAM_MISSING_POLICY,
            int(p_decl.missing_input_policy())
        );
        p_api->predict_set_param(
            p_entity,
            netw::NetwMultiplayer::PREDICT_PARAM_REPLAY_BUFFER_DEPTH,
            p_decl.replay_buffer_depth()
        );
        p_api->predict_set_param(
            p_entity,
            netw::NetwMultiplayer::PREDICT_PARAM_CORRECTION_MODE,
            int(p_decl.correction())
        );
        if (p_decl.teleport_at() > 0.0) {
            p_api->predict_set_param(
                p_entity,
                netw::NetwMultiplayer::PREDICT_PARAM_TELEPORT_THRESHOLD,
                p_decl.teleport_at()
            );
        }
        if (p_decl.consume_lag_ticks() > 0) {
            p_api->predict_set_param(
                p_entity,
                netw::NetwMultiplayer::PREDICT_PARAM_MAX_CONSUME_LAG_TICKS,
                p_decl.consume_lag_ticks()
            );
        }
        REQUIRE_MESSAGE(
            int(p_api->predict_declare(p_entity)) == int(godot::OK),
            "the prediction declaration did not install"
        );
    }

    Carrier *mint_owner(const EntityDecl &p_decl) {
        Carrier *owner = memnew(Carrier);
        if (!p_decl.name().is_empty()) {
            owner->set_name(p_decl.name());
        }
        if (p_decl.is_predicted()) {
            owner->define("motion", godot::Vector2());
            owner->define("bombing", false);
        }
        for (const godot::StringName &field : p_decl.synced_columns()) {
            if (field == godot::StringName("position")) {
                continue;
            }
            owner->define(field, p_decl.initial_pose());
        }
        if (p_decl.initial_pose().get_type() != godot::Variant::NIL) {
            for (const godot::StringName &field : p_decl.synced_columns()) {
                owner->set(field, p_decl.initial_pose());
            }
        }
        if (p_decl.carries()) {
            owner->set_carry_gain(p_decl.carry_gain());
            netw::script::model::bind_node_property_carry(
                owner,
                godot::StringName("position"),
                godot::Callable(owner, godot::StringName("carry_position"))
            );
        }
        if (!p_decl.is_mounted()) {
            owned_nodes.push_back(owner);
        }
        return owner;
    }

public:
    explicit LoopbackRig(int p_clients = 1) {
        link.instantiate();
        server_api = make_api(link->get_server_peer());
        for (int index = 0; index < p_clients; ++index) {
            add_client();
        }
        pump();
    }

    ~LoopbackRig() {
        for (godot::Node *mount : mounts) {
            if (mount == nullptr) {
                continue;
            }
            while (mount->get_child_count() > 0) {
                godot::Node *child = mount->get_child(0);
                mount->remove_child(child);
                memdelete(child);
            }
        }
        godot::SceneTree *tree = netw::gd::scene_tree();
        if (tree != nullptr) {
            for (const godot::NodePath &path : mounted_paths) {
                tree->set_multiplayer(
                    godot::Ref<godot::MultiplayerAPI>(),
                    path
                );
            }
        }
        for (godot::Node *node : owned_nodes) {
            if (node != nullptr && node->get_parent() != nullptr) {
                node->get_parent()->remove_child(node);
            }
        }
        for (godot::Node *node : owned_nodes) {
            if (node != nullptr) {
                memdelete(node);
            }
        }
        if (link.is_valid()) {
            link->reset();
        }
    }

    LoopbackRig(const LoopbackRig &) = delete;
    LoopbackRig &operator=(const LoopbackRig &) = delete;

    netw::NetwMultiplayer *shell() const {
        return server_api.ptr();
    }

    netw::NetwMultiplayer *shell_at(int p_index) const {
        REQUIRE(p_index >= 0);
        REQUIRE(p_index < client_apis.size());
        return client_apis[p_index].ptr();
    }

    netw::NetwMultiplayer *server() const {
        return server_api.ptr();
    }

    static netw::NetwMultiplayer *core_of(const godot::Variant &p_held) {
        return godot::Object::cast_to<netw::NetwMultiplayer>(
            netw::gd::live_object(p_held)
        );
    }

    void flush_interest() const {
        server()->interest_flush_now();
    }

    void mount_one(
        netw::NetwMultiplayer *p_api,
        godot::Node *p_scene,
        const godot::String &p_name
    ) {
        godot::Node *branch_node = memnew(godot::Node);
        branch_node->set_name(p_name);
        p_scene->add_child(branch_node);
        owned_nodes.push_back(branch_node);
        mounts.push_back(branch_node);
        REQUIRE_MESSAGE(p_api != nullptr, "a mount needs a session");
        if (p_api != nullptr) {
            p_api->session_set_root(
                godot::Callable(branch_node, "get_node")
                    .bind(godot::NodePath("."))
            );
        }
        godot::SceneTree *tree = netw::gd::scene_tree();
        REQUIRE_MESSAGE(tree != nullptr, "the rig needs a tree to register in");
        if (tree != nullptr) {
            tree->set_multiplayer(
                godot::Ref<godot::MultiplayerAPI>(
                    godot::Object::cast_to<godot::MultiplayerAPI>(p_api)
                ),
                branch_node->get_path()
            );
            mounted_paths.push_back(branch_node->get_path());
        }
    }

    void mount() {
        if (!mounts.is_empty()) {
            return;
        }
        godot::Node *scene = netw::gd::scene_root();
        REQUIRE_MESSAGE(scene != nullptr, "the runner has no tree to mount in");
        if (scene == nullptr) {
            return;
        }
        mount_one(shell(), scene, "RigServer");
        for (int index = 0; index < count(); ++index) {
            mount_one(
                shell_at(index),
                scene,
                godot::vformat("RigClient%d", index)
            );
        }
        pump();
    }

    void mount_late(int p_client) {
        godot::Node *scene = netw::gd::scene_root();
        REQUIRE_MESSAGE(!mounts.is_empty(), "mount the rig before a late seat");
        if (scene == nullptr) {
            return;
        }
        mount_one(
            shell_at(p_client),
            scene,
            godot::vformat("RigClient%d", p_client)
        );
        pump();
    }

    godot::Node *mirror_child(const godot::String &p_name) {
        godot::Node *host = nullptr;
        for (int at = 0; at < mounts.size(); ++at) {
            godot::Node *made = memnew(godot::Node);
            made->set_name(p_name);
            mounts[at]->add_child(made);
            if (at == 0) {
                host = made;
            }
        }
        pump();
        return host;
    }

    void mirror_late(int p_client, const godot::String &p_name) {
        godot::Node *made = memnew(godot::Node);
        made->set_name(p_name);
        branch(p_client)->add_child(made);
        pump();
    }

    godot::Node *branch(int p_client = -1) const {
        const int at = p_client + 1;
        REQUIRE_MESSAGE(at < mounts.size(), "that session is not mounted");
        return at < mounts.size() ? mounts[at] : nullptr;
    }

    godot::RID spawned_entity() const {
        return last_spawn;
    }

    void register_constructor(
        netw::NetwMultiplayer *p_api,
        const godot::StringName &p_id,
        const godot::Callable &p_build,
        const godot::Array &p_arg_types
    ) {
        netw::NetwMultiplayer *core = p_api;
        REQUIRE_MESSAGE(core != nullptr, "a constructor needs a session");
        godot::Array quantizers;
        for (int at = 0; at < p_arg_types.size(); ++at) {
            quantizers.push_back(godot::Variant());
        }
        if (core != nullptr) {
            core->spawn_register_constructor(
                p_id,
                p_build,
                p_arg_types,
                quantizers
            );
        }
    }

    int spawn_registered(
        const godot::StringName &p_id,
        const godot::Callable &p_build,
        const godot::Array &p_args = godot::Array(),
        const godot::Array &p_arg_types = godot::Array(),
        godot::Node *p_parent = nullptr,
        const godot::Variant &p_owner = godot::Variant(),
        bool p_pump = true
    ) {
        REQUIRE_MESSAGE(!mounts.is_empty(), "a spawn needs a mounted rig");
        register_constructor(server(), p_id, p_build, p_arg_types);
        for (int index = 0; index < count(); ++index) {
            register_constructor(client(index), p_id, p_build, p_arg_types);
        }
        const godot::RID entity = server()->spawn_registered(
            p_id,
            p_args,
            netw::gd::live_object(p_owner)
        );
        REQUIRE_MESSAGE(entity.is_valid(), "the spawn verb minted no entity");
        godot::Node *node = server()->entity_get_node(entity);
        REQUIRE_MESSAGE(node != nullptr, "the spawn verb built no node");
        if (node != nullptr) {
            godot::Node *parent = p_parent != nullptr ? p_parent : branch(-1);
            parent->add_child(node);
        }
        if (p_pump) {
            pump(6);
        }
        last_spawn = entity;
        return int(server()->entity_get_route(entity));
    }

    godot::Ref<netw::NetwParticipant> participant(int p_client) const {
        return server()->peer_get_participant(peer_id(p_client));
    }

    netw::spawn::Pipeline *spawn_plane(int p_client = -1) const {
        netw::NetwMultiplayer *core
            = p_client < 0 ? server() : client(p_client);
        netw::ReplicationCore *plane
            = core != nullptr ? core->get_replication_plane() : nullptr;
        REQUIRE_MESSAGE(plane != nullptr, "the session has no plane");
        return plane != nullptr ? plane->get_spawn_pipeline() : nullptr;
    }

    godot::PackedByteArray spawn_frame_of(int p_route) const {
        netw::spawn::Pipeline *pipeline = spawn_plane();
        REQUIRE_MESSAGE(pipeline != nullptr, "the session has no spawn half");
        if (pipeline == nullptr) {
            return godot::PackedByteArray();
        }
        netw::spawn::Book *book = pipeline->get_spawn_book();
        netw::spawn::Record *record = book->spawned_of(p_route);
        REQUIRE_MESSAGE(record != nullptr, "that route was never issued");
        if (record == nullptr) {
            return godot::PackedByteArray();
        }
        return pipeline->encode_spawn_frame(p_route, record->node());
    }

    static godot::PackedByteArray verb_head(int p_route, int p_epoch) {
        netw::wire::WriteStream stream;
        uint64_t route = uint64_t(p_route);
        uint64_t epoch = uint64_t(p_epoch);
        stream.varuint(route, 5);
        stream.varuint(epoch, 3);
        stream.align_verify();
        return stream.to_bytes();
    }

    void deliver_despawn(int p_client, int p_route, int p_epoch = 0) {
        netw::spawn::Pipeline *pipeline = spawn_plane(p_client);
        if (pipeline != nullptr) {
            pipeline->handle_despawn_frame(verb_head(p_route, p_epoch), 1);
        }
    }

    void deliver_spawn(int p_client, const godot::PackedByteArray &p_frame) {
        netw::spawn::Pipeline *pipeline = spawn_plane(p_client);
        if (pipeline != nullptr) {
            pipeline->handle_spawn_frame(p_frame, 1);
        }
    }

    godot::Node *route_node(int p_route, int p_client = -1) const {
        netw::NetwMultiplayer *api = p_client < 0 ? server() : client(p_client);
        return api->entity_get_node(api->entity_from_route(p_route));
    }

    netw::NetwMultiplayer *client(int p_index) const {
        return shell_at(p_index);
    }

    int client_count() const {
        return client_apis.size();
    }

    netw::ClockEngine &clock_of(netw::NetwMultiplayer *p_core) const {
        REQUIRE_MESSAGE(p_core != nullptr, "a clock needs a session to be on");
        return p_core->clock_engine();
    }

    godot::RID client_entity_of(
        int p_client,
        const godot::StringName &p_name
    ) const {
        if (p_client < 0 || p_client >= client_declared.size()) {
            return godot::RID();
        }
        const godot::HashMap<godot::StringName, godot::RID>::ConstIterator found
            = client_declared[p_client].find(p_name);
        return found != client_declared[p_client].end() ? found->value
                                                        : godot::RID();
    }

    int peer_id(int p_index) const {
        if (p_index < 0) {
            return link->get_server_peer()->get_unique_id();
        }
        REQUIRE(p_index < client_peers.size());
        return client_peers[p_index]->get_unique_id();
    }

    int count() const {
        return client_apis.size();
    }

    int add_client() {
        godot::Ref<netw::LocalMultiplayerPeer> peer
            = link->create_client_peer();
        client_peers.push_back(peer);
        client_apis.push_back(make_api(peer));
        client_declared.push_back({});
        client_state_sets.push_back({});
        return client_apis.size() - 1;
    }

    void drop_client(int p_index, bool p_force = false) {
        REQUIRE(p_index >= 0);
        REQUIRE(p_index < client_peers.size());
        const int departing = client_peers[p_index]->get_unique_id();
        link->get_server_peer()->NETW_PEER_VIRTUAL(disconnect_peer)(
            departing,
            p_force
        );
    }

    int refused_sends_at_server() const {
        return link->get_server_peer()->refused_sends();
    }

    void close_link() {
        link->get_server_peer()->NETW_PEER_VIRTUAL(close)();
    }

    int delivered_sends_at_server() const {
        return link->get_server_peer()->delivered_sends();
    }

    void clear_refused_sends() {
        link->get_server_peer()->clear_refused_sends();
        for (const godot::Ref<netw::LocalMultiplayerPeer> &peer :
             client_peers) {
            peer->clear_refused_sends();
        }
    }

    void pump(int p_times = 1) {
        for (int round = 0; round < p_times; ++round) {
            link->poll();
            poll_api(server_api);
            for (const godot::Ref<godot::RefCounted> &api : client_apis) {
                poll_api(api);
            }
        }
    }

    void advance(double p_ms) {
        link->advance_time(p_ms);
        poll_api(server_api);
        for (const godot::Ref<godot::RefCounted> &api : client_apis) {
            poll_api(api);
        }
    }

    void hold(int p_client) {
        link->hold_inbound_packets(peer(p_client));
    }

    void release(int p_client) {
        link->release_inbound_packets(peer(p_client));
    }

    void conditions(
        int p_client,
        const godot::Ref<netw::LocalLinkConditions> &p_conditions,
        int p_from = 0
    ) {
        link->set_link_conditions(peer(p_client), p_conditions, p_from);
    }

    godot::RID declare_entity(const EntityDecl &p_decl) {
        godot::Node *owner = nullptr;
        if (p_decl.wants_owner()) {
            owner = mint_owner(p_decl);
        }
        if (owner != nullptr && p_decl.is_mounted()) {
            branch(-1)->add_child(owner);
        }
        const godot::RID entity = stand_up(server(), p_decl, owner, true);
        const godot::RID state = attach_state(server(), entity, owner, p_decl);
        Carrier *carrier = godot::Object::cast_to<Carrier>(owner);
        attach_input(server(), entity, carrier, p_decl);
        configure_prediction(shell(), entity, carrier, p_decl);
        if (!p_decl.name().is_empty()) {
            declared[p_decl.name()] = entity;
            if (state.is_valid()) {
                declared_state_sets[p_decl.name()] = state;
            }
        }
        return entity;
    }

    godot::Ref<netw::NetwParticipant> join(
        int p_client,
        const godot::StringName &p_username = godot::StringName(),
        const godot::Array &p_args = godot::Array()
    ) {
        netw::NetwMultiplayer *api
            = p_client < 0 ? shell() : shell_at(p_client);
        api->session_submit_join(p_username, p_args);
        pump(4);
        return godot::Object::cast_to<netw::NetwParticipant>(
            netw::gd::live_object(api->get("local_participant"))
        );
    }

    godot::Ref<netw::NetwEntity> declare_unrecorded_wrapper(
        const godot::StringName &p_name
    ) {
        Carrier *owner = memnew(Carrier);
        owner->set_name(p_name);
        owned_nodes.push_back(owner);
        return netw::NetwEntity::ensure(owner);
    }

    godot::RID declare_mirror(int p_client, const EntityDecl &p_decl) {
        REQUIRE_MESSAGE(p_decl.route() > 0, "a mirror needs an admitted route");
        Carrier *owner = mint_owner(p_decl);
        if (p_decl.is_mounted()) {
            branch(p_client)->add_child(owner);
        }
        const godot::RID entity
            = stand_up(client(p_client), p_decl, owner, false);
        const godot::RID state
            = attach_state(client(p_client), entity, owner, p_decl);
        attach_input(client(p_client), entity, owner, p_decl);
        if (!p_decl.name().is_empty()) {
            client_declared.ptrw()[p_client][p_decl.name()] = entity;
            if (state.is_valid()) {
                client_state_sets.ptrw()[p_client][p_decl.name()] = state;
            }
        }
        return entity;
    }

    godot::Node *node_of(const godot::RID &p_entity, int p_client = -1) const {
        netw::NetwMultiplayer *api = p_client < 0 ? server() : client(p_client);
        return api->entity_get_node(p_entity);
    }

    godot::RID state_set_of(
        const godot::StringName &p_name,
        int p_client = -1
    ) const {
        const godot::HashMap<godot::StringName, godot::RID> &sets
            = p_client < 0 ? declared_state_sets : client_state_sets[p_client];
        const auto found = sets.find(p_name);
        return found != sets.end() ? found->value : godot::RID();
    }

    godot::RID declare_scene(
        const godot::StringName &p_name,
        const godot::StringName &p_stem = godot::StringName()
    ) {
        const godot::StringName stem = p_stem.is_empty() ? p_name : p_stem;
        netw::NetwMultiplayer *api = server();
        const godot::RID scene = api->entity_create();
        REQUIRE_MESSAGE(scene.is_valid(), "entity_create returned no handle");
        NETW_CHECK_EQ(int(api->scene_declare(scene)), int(godot::OK));

        godot::Node2D *level = memnew(godot::Node2D);
        level->set_name(stem);
        godot::Node2D *marker = make_marker();
        REQUIRE_MESSAGE(
            marker != nullptr,
            "the scene marker did not instantiate"
        );
        if (marker == nullptr) {
            return godot::RID();
        }
        marker->set_name("Marker");
        level->add_child(marker);
        owned_nodes.push_back(level);

        NETW_CHECK_GT(int(api->entity_admit(scene)), 0);
        NETW_CHECK_EQ(int(api->entity_bind_node(scene, level)), int(godot::OK));
        CHECK(api->scene_is_declared(scene));
        api->scene_set_param(
            scene,
            netw::NetwMultiplayer::SCENE_PARAM_LABEL,
            stem
        );

        declared[p_name] = scene;
        return scene;
    }

    netw::NetwMultiplayer *enter_scene(const godot::StringName &p_name) {
        netw::NetwMultiplayer *scenes = server();
        REQUIRE_MESSAGE(scenes != nullptr, "the session has no scene core");
        godot::Node *root = node_of(entity_of(p_name));
        REQUIRE_MESSAGE(root != nullptr, "the scene has no root");
        scenes->scene_root_online(root);
        pump();
        return scenes;
    }

    godot::RID mirror_scene(int p_client, const godot::StringName &p_name) {
        const godot::RID origin = entity_of(p_name);
        REQUIRE_MESSAGE(origin.is_valid(), "the scene was never declared");
        const int route = int(server()->entity_get_route(origin));
        REQUIRE_MESSAGE(route > 0, "the scene holds no route to mirror");

        netw::NetwMultiplayer *api = client(p_client);
        godot::RID mirror = api->entity_from_route(route);
        if (!mirror.is_valid()) {
            mirror = api->entity_create();
        }
        REQUIRE_MESSAGE(mirror.is_valid(), "entity_create returned no handle");
        NETW_CHECK_EQ(
            int(api->entity_bind_route(mirror, route)),
            int(godot::OK)
        );
        NETW_CHECK_EQ(int(api->scene_declare(mirror)), int(godot::OK));

        godot::Node *level = memnew(godot::Node);
        level->set_name(
            godot::String(
                server()->scene_get_param(
                    origin,
                    netw::NetwMultiplayer::SCENE_PARAM_LABEL
                )
            )
        );
        owned_nodes.push_back(level);
        NETW_CHECK_EQ(
            int(api->entity_bind_node(mirror, level)),
            int(godot::OK)
        );

        REQUIRE_MESSAGE(api != nullptr, "the client has no scene core");
        api->scene_root_online(level);
        pump();
        client_declared.ptrw()[p_client][p_name] = mirror;
        return mirror;
    }

    void seat(const godot::RID &p_entity, const godot::RID &p_scene) {
        godot::Node *body = node_of(p_entity);
        godot::Node *level = content_of(p_scene);
        REQUIRE_MESSAGE(body != nullptr, "a seated entity needs a body");
        REQUIRE_MESSAGE(level != nullptr, "a scene needs a content root");
        if (body == nullptr || level == nullptr) {
            return;
        }
        const godot::Ref<netw::NetwEntity> record = netw::NetwEntity::of(body);
        REQUIRE_MESSAGE(record.is_valid(), "a seated entity needs a record");
        if (record.is_valid()) {
            record->reparent_to(level, godot::Ref<netw::NetwReparentOpts>());
        }
        flush_interest();
    }

    void move_scene(
        const godot::RID &p_entity,
        const godot::RID &p_destination
    ) {
        const godot::Ref<netw::NetwPromise> settled = server()->scene_move(
            p_entity,
            p_destination,
            godot::Ref<netw::NetwReparentOpts>()
        );
        REQUIRE_MESSAGE(settled.is_valid(), "scene_move returned no promise");
        NETW_CHECK_EQ(int(settled.is_valid() && settled->get_is_settled()), 1);
        NETW_CHECK_EQ(settled.is_valid() ? settled->get_code() : -1, 0);
        flush_interest();
    }

    godot::Node *content_of(const godot::RID &p_scene) const {
        return node_of(p_scene);
    }

    void declare_world(const WorldDecl &p_world) {
        if (p_world.wants_clock) {
            tick_period_ms = 1000.0 / double(p_world.clock_tickrate);
        }
        install_services(server(), p_world);
        for (int index = 0; index < count(); ++index) {
            install_services(client(index), p_world);
        }
        for (const WorldDecl::SceneRow &scene : p_world.scenes) {
            declare_scene(scene.name, scene.stem);
        }
        for (const WorldDecl::Row &row : p_world.rows) {
            EntityDecl decl = row.decl;
            if (row.player_client >= 0) {
                decl.controlled_by(peer_id(row.player_client));
            } else if (row.hosted) {
                decl.controlled_by(peer_id(-1));
            }
            const godot::RID entity = declare_entity(decl);
            if (row.player_client >= 0) {
                const int route = int(server()->entity_get_route(entity));
                declare_mirror(
                    row.player_client,
                    EntityDecl(decl).on_route(route)
                );
                server()->entity_grant_control(
                    entity,
                    peer_id(row.player_client)
                );
                pump();
                godot::Node *mirror_owner = node_of(
                    entity_of(decl.name(), row.player_client),
                    row.player_client
                );
                configure_prediction(
                    shell_at(row.player_client),
                    entity_of(decl.name(), row.player_client),
                    godot::Object::cast_to<Carrier>(mirror_owner),
                    decl
                );
            }
            if (!row.scene.is_empty()) {
                seat(entity, entity_of(row.scene));
            }
        }
        for (const WorldDecl::IslandRow &row : p_world.islands) {
            mirror_island(p_world, row);
            declare_island(row);
        }
    }

    void mirror_island(
        const WorldDecl &p_world,
        const WorldDecl::IslandRow &p_row
    ) {
        const int seat = client_of(p_world, p_row.owner);
        if (seat < 0) {
            return;
        }
        for (const godot::StringName &member : p_row.members) {
            if (holds_entity(member, seat)) {
                continue;
            }
            const int index = row_of(p_world, member);
            if (index < 0) {
                continue;
            }
            const godot::RID authority = entity_of(member);
            const int route = int(server()->entity_get_route(authority));
            const EntityDecl decl = EntityDecl(p_world.rows[index].decl)
                                        .on_route(route)
                                        .controlled_by(0);
            declare_mirror(seat, decl);
            pump();
            const godot::RID mirror = entity_of(member, seat);
            configure_prediction(
                shell_at(seat),
                mirror,
                godot::Object::cast_to<Carrier>(node_of(mirror, seat)),
                decl
            );
        }
    }

    static int row_of(
        const WorldDecl &p_world,
        const godot::StringName &p_name
    ) {
        for (int index = 0; index < p_world.rows.size(); ++index) {
            if (p_world.rows[index].decl.name() == p_name) {
                return index;
            }
        }
        return -1;
    }

    static int client_of(
        const WorldDecl &p_world,
        const godot::StringName &p_name
    ) {
        const int index = row_of(p_world, p_name);
        return index < 0 ? -1 : p_world.rows[index].player_client;
    }

    enum Fidelity {
        FIDELITY_PROXY = 0,
        FIDELITY_SIMULATED = 1,
    };

    void declare_island(const WorldDecl::IslandRow &p_row) {
        seat_island(server(), p_row, -1);
        for (int index = 0; index < count(); ++index) {
            seat_island(client(index), p_row, index);
        }
    }

    void seat_island(
        netw::NetwMultiplayer *p_api,
        const WorldDecl::IslandRow &p_row,
        int p_client
    ) {
        if (!holds_entity(p_row.owner, p_client)) {
            return;
        }
        for (const godot::StringName &member : p_row.members) {
            if (!holds_entity(member, p_client)) {
                return;
            }
        }
        const godot::RID owner = entity_of(p_row.owner, p_client);
        for (const godot::StringName &member : p_row.members) {
            p_api->predict_island_add(owner, entity_of(member, p_client));
        }
        for (const godot::StringName &member : p_row.simulated) {
            p_api->predict_island_set_member_param(
                owner,
                entity_of(member, p_client),
                netw::NetwMultiplayer::MEMBER_PARAM_FIDELITY,
                FIDELITY_SIMULATED
            );
        }
        p_api->predict_island_set_param(
            owner,
            netw::NetwMultiplayer::ISLAND_PARAM_RECONCILE,
            p_row.reconcile
        );
        if (p_row.promotion != 0) {
            p_api->predict_island_set_param(
                owner,
                netw::NetwMultiplayer::ISLAND_PARAM_PROMOTION,
                p_row.promotion
            );
            p_api->predict_island_set_param(
                owner,
                netw::NetwMultiplayer::ISLAND_PARAM_PROMOTION_COUNT,
                p_row.promotion_count
            );
            p_api->predict_island_set_param(
                owner,
                netw::NetwMultiplayer::ISLAND_PARAM_PROMOTION_METERS,
                p_row.promotion_meters
            );
        }
    }

    bool holds_entity(const godot::StringName &p_name, int p_client) const {
        const godot::HashMap<godot::StringName, godot::RID> &records
            = p_client < 0 ? declared : client_declared[p_client];
        return records.find(p_name) != records.end();
    }

    godot::RID entity_of(
        const godot::StringName &p_name,
        int p_client = -1
    ) const {
        const godot::HashMap<godot::StringName, godot::RID> &records
            = p_client < 0 ? declared : client_declared[p_client];
        const godot::HashMap<godot::StringName, godot::RID>::ConstIterator found
            = records.find(p_name);
        REQUIRE_MESSAGE(
            found != records.end(),
            "nothing was declared by that name"
        );
        return found != records.end() ? found->value : godot::RID();
    }

    bool pump_until(const godot::Callable &p_condition, int p_budget = 120) {
        for (int spent = 0; spent < p_budget; ++spent) {
            if (bool(p_condition.callv(godot::Array()))) {
                return true;
            }
            pump();
        }
        return bool(p_condition.callv(godot::Array()));
    }

    netw::LocalLoopbackSession *session() const {
        return link.ptr();
    }

    void step_ticks(int p_ticks) {
        REQUIRE_MESSAGE(p_ticks >= 0, "a tick count cannot be negative");
        for (int step = 0; step < p_ticks; ++step) {
            clock_of(server()).force_step(1);
            for (int index = 0; index < count(); ++index) {
                clock_of(client(index)).force_step(1);
            }
            link->advance_time(tick_period_ms);
            poll_api(server_api);
            for (const godot::Ref<godot::RefCounted> &api : client_apis) {
                poll_api(api);
            }
        }
    }

    void step_frame(int p_ticks) {
        step_split_frame(p_ticks, p_ticks, true);
    }

    void step_split_frame(int p_client_ticks, int p_server_ticks) {
        step_split_frame(p_client_ticks, p_server_ticks, p_server_ticks > 0);
    }

    void step_split_frame(
        int p_client_ticks,
        int p_server_ticks,
        bool p_server_present
    ) {
        REQUIRE_MESSAGE(
            p_client_ticks >= 0,
            "a frame tick count cannot be negative"
        );
        REQUIRE_MESSAGE(
            p_server_ticks >= 0,
            "a frame tick count cannot be negative"
        );
        netw::ClockEngine &server_clock = clock_of(server());
        if (p_server_present) {
            server_clock.begin_tick_loop();
        }
        for (int index = 0; index < count(); ++index) {
            clock_of(client(index)).begin_tick_loop();
        }
        if (p_server_present && p_server_ticks > 0) {
            server_clock.force_step(p_server_ticks);
        }
        if (p_client_ticks > 0) {
            for (int index = 0; index < count(); ++index) {
                clock_of(client(index)).force_step(p_client_ticks);
            }
        }
        if (p_server_present) {
            server_clock.end_tick_loop();
        }
        for (int index = 0; index < count(); ++index) {
            clock_of(client(index)).end_tick_loop();
        }
        link->advance_time(tick_period_ms);
        poll_api(server_api);
        for (const godot::Ref<godot::RefCounted> &api : client_apis) {
            poll_api(api);
        }
    }

    netw::NetwPredictionHandle *prediction_handle(
        const godot::StringName &p_name,
        int p_client = -1
    ) const {
        godot::Node *owner = node_of(entity_of(p_name, p_client), p_client);
        REQUIRE_MESSAGE(owner != nullptr, "prediction needs an entity owner");
        godot::Object *wrapper = owner->get_meta("netw_entity");
        REQUIRE_MESSAGE(wrapper != nullptr, "the entity has no wrapper");
        netw::NetwPredictionHandle *handle
            = godot::Object::cast_to<netw::NetwPredictionHandle>(
                netw::gd::live_object(wrapper->get("prediction"))
            );
        REQUIRE_MESSAGE(handle != nullptr, "the entity has no prediction");
        return handle;
    }

    netw::LocalMultiplayerPeer *peer(int p_client) const {
        if (p_client < 0) {
            return link->get_server_peer().ptr();
        }
        REQUIRE(p_client < client_peers.size());
        return client_peers[p_client].ptr();
    }

private:
    static void poll_api(const godot::Ref<netw::NetwMultiplayer> &p_api) {
        if (p_api.is_valid()) {
            p_api->poll();
        }
    }
};

#endif

} // namespace netw_test
