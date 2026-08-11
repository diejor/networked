#pragma once

/* A server plus N clients on a private loopback session, driven from C++.
 *
 * The session is never the process-wide one: a case that shares global state is
 * not a unit test. Time advances only when the rig advances it, so a delay is
 * counted in pumps rather than in wall clock.
 *
 * [codeblock]
 * LoopbackRig rig(2);
 * rig.conditions(0, LocalLinkConditions::wifi());
 * rig.pump(4);
 * int alice = rig.peer_id(0);
 * [/codeblock]
 *
 * Two things about it are true only while NetwMultiplayer is GDScript, and both
 * end when the shell ports.
 *
 * The rig is the ONLY place the native corpus says how an API is constructed.
 * Here that is a script load, which is why a rig case can only run in the tier
 * that can see `res://`: tag such a case `[Networked][<Family>]` and NEVER
 * `[Hosted]`, because a `[Hosted]` case that cannot construct its API in the
 * module tier reads as a pass there rather than as a failure.
 *
 * An API comes back as an `Object *` and a case reaches its verbs through
 * `call()`. When the shell goes native those call sites become ordinary method
 * calls. That sweep is mechanical and greppable, and it is the price of writing
 * cases against a real session now instead of after every family has crossed.
 */

#include "netw_test.h"

#include "carrier.h"
#include "entity_decl.h"
#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/table/schema_core.hpp"
#include "netw/transport/loopback.hpp"
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
    static constexpr const char *API_SCRIPT
        = "res://addons/networked/replication/netw_multiplayer.gd";
    static constexpr const char *ENTITY_SCRIPT
        = "res://addons/networked/context/session/netw_entity.gd";

    godot::Ref<netw::LocalLoopbackSession> link;
    godot::Ref<godot::RefCounted> server_api;
    godot::Vector<godot::Ref<godot::RefCounted>> client_apis;
    godot::Vector<godot::Ref<netw::LocalMultiplayerPeer>> client_peers;
    godot::Vector<godot::HashMap<godot::StringName, godot::RID>>
        client_declared;
    // Every owner this rig minted, freed with it. A declaration says it wants
    // an owner and never authors one, so the lifetime has to live here.
    godot::Vector<godot::Node *> owned_nodes;
    godot::HashMap<godot::StringName, godot::RID> declared;
    godot::HashMap<godot::StringName, godot::RID> declared_state_sets;
    godot::Vector<godot::HashMap<godot::StringName, godot::RID>>
        client_state_sets;
    double tick_period_ms = 1000.0 / 30.0;

    static godot::Ref<godot::RefCounted> make_api(
        const godot::Ref<netw::LocalMultiplayerPeer> &p_peer
    ) {
        godot::Ref<godot::Script> script
            = godot::ResourceLoader::get_singleton()->load(API_SCRIPT);
        REQUIRE_MESSAGE(script.is_valid(), "the session script did not load");
        if (script.is_null()) {
            return godot::Ref<godot::RefCounted>();
        }

        godot::Ref<godot::SceneMultiplayer> inner;
        inner.instantiate();
        // A treeless session still needs a root path: poll() refuses to process
        // inbound packets without one.
        inner->set_root_path(godot::NodePath("/"));

        godot::Ref<godot::RefCounted> api = script->call("new", inner);
        REQUIRE_MESSAGE(
            api.is_valid(),
            "the session script did not instantiate"
        );
        if (api.is_valid()) {
            api->set("multiplayer_peer", p_peer);
        }
        return api;
    }

    static godot::Ref<godot::Script> entity_script() {
        godot::Ref<godot::Script> script
            = godot::ResourceLoader::get_singleton()->load(ENTITY_SCRIPT);
        REQUIRE_MESSAGE(script.is_valid(), "the record script did not load");
        return script;
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

    static void install_services(
        godot::Object *p_api,
        const WorldDecl &p_world
    ) {
        if (p_world.wants_clock) {
            godot::Ref<godot::RefCounted> config = make_resource(
                "res://addons/networked/sync/clock/netw_clock_config.gd"
            );
            REQUIRE(config.is_valid());
            config->set("tickrate", p_world.clock_tickrate);
            config->set("display_offset", p_world.clock_display_offset);
            NETW_CHECK_EQ(
                int(p_api->call("service_install", config)),
                int(godot::OK)
            );
            godot::Object *clock = p_api->get("_clock");
            REQUIRE_MESSAGE(clock != nullptr, "the clock did not install");
            if (clock != nullptr) {
                clock->set("manual_tick", true);
            }
        }
        if (p_world.wants_lagcomp) {
            godot::Ref<godot::RefCounted> config = make_resource(
                "res://addons/networked/sync/sim/"
                "netw_lag_compensation_config.gd"
            );
            REQUIRE(config.is_valid());
            NETW_CHECK_EQ(
                int(p_api->call("service_install", config)),
                int(godot::OK)
            );
        }
    }

    // Composes the flat verbs in the one order that works. The route is bound
    // before the owner because binding an owner is what promotes an already
    // routed row, and control is granted last because it needs the wrapper the
    // owner brought.
    godot::RID stand_up(
        godot::Object *p_api,
        const EntityDecl &p_decl,
        godot::Node *p_owner,
        bool p_grant_control
    ) const {
        godot::Object *api = p_api;
        REQUIRE_MESSAGE(api != nullptr, "a declaration needs a session");
        const godot::RID entity = api->call("entity_create");
        REQUIRE_MESSAGE(entity.is_valid(), "entity_create returned no handle");

        const int route = p_decl.route() > 0
            ? p_decl.route()
            : int(api->call("entity_allocate_route", entity));
        const godot::Error bound
            = godot::Error(int(api->call("entity_bind_route", entity, route)));
        REQUIRE_MESSAGE(bool(bound == godot::OK), "the route did not bind");

        if (p_owner != nullptr) {
            const godot::Error owned = godot::Error(
                int(api->call("entity_bind_node", entity, p_owner))
            );
            REQUIRE_MESSAGE(bool(owned == godot::OK), "the owner did not bind");
        }
        if (p_grant_control && p_decl.controller() != 0) {
            api->call("entity_grant_control", entity, p_decl.controller());
        }
        return entity;
    }

    static godot::RID attach_state(
        godot::Object *p_api,
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
        const godot::RID schema = p_api->call("schema_create", p_decl.schema());
        for (const godot::StringName &field : p_decl.synced_columns()) {
            const godot::Variant value = p_owner->get(field);
            REQUIRE_MESSAGE(
                value.get_type() != godot::Variant::NIL,
                "a synced field needs a carrier property"
            );
            p_api->call(
                "schema_add_column",
                schema,
                field,
                column_type(value.get_type())
            );
        }
        p_api->call("schema_seal", schema);
        const godot::RID state = p_api->call("property_set_create", schema, 1);
        for (int index = 0; index < p_decl.synced_columns().size(); ++index) {
            p_api->call("property_set_add_column", state, index);
        }
        p_api->call("property_set_seal", state);
        REQUIRE_MESSAGE(
            int(p_api->call("entity_add_property_set", p_entity, state, 0))
                == int(godot::OK),
            "the declared state set did not attach"
        );
        return state;
    }

    static int column_type(godot::Variant::Type p_type) {
        switch (p_type) {
            case godot::Variant::FLOAT:
                return int(netw::SchemaCore::F64);
            case godot::Variant::INT:
                return int(netw::SchemaCore::I64);
            case godot::Variant::BOOL:
                return int(netw::SchemaCore::BOOL);
            case godot::Variant::VECTOR2:
                return int(netw::SchemaCore::VECTOR2);
            case godot::Variant::VECTOR3:
                return int(netw::SchemaCore::VECTOR3);
            case godot::Variant::VECTOR4:
                return int(netw::SchemaCore::VECTOR4);
            case godot::Variant::COLOR:
                return int(netw::SchemaCore::COLOR);
            case godot::Variant::QUATERNION:
                return int(netw::SchemaCore::QUATERNION);
            default:
                return int(netw::SchemaCore::VARIANT);
        }
    }

    static void attach_input(
        godot::Object *p_api,
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
        const godot::RID schema = p_api->call("schema_create", schema_name);
        p_api->call(
            "schema_add_column",
            schema,
            godot::StringName("motion"),
            int(netw::SchemaCore::VECTOR2)
        );
        p_api->call(
            "schema_add_column",
            schema,
            godot::StringName("bombing"),
            int(netw::SchemaCore::BOOL)
        );
        p_api->call("schema_seal", schema);
        const godot::RID input = p_api->call("property_set_create", schema, 2);
        p_api->call("property_set_add_column", input, 0);
        p_api->call("property_set_add_column", input, 1);
        p_api->call("property_set_seal", input);
        REQUIRE_MESSAGE(
            int(p_api->call("entity_add_property_set", p_entity, input, 0))
                == int(godot::OK),
            "the declared input set did not attach"
        );
    }

    static void configure_prediction(
        godot::Object *p_api,
        const godot::RID &p_entity,
        Carrier *p_owner,
        const EntityDecl &p_decl
    ) {
        if (!p_decl.is_predicted()) {
            return;
        }
        p_api->call(
            "predict_set_param",
            p_entity,
            1,
            int(p_decl.schedule())
        );
        p_api->call(
            "predict_set_param",
            p_entity,
            7,
            p_decl.prediction_epsilon()
        );
        p_api->call(
            "predict_set_param",
            p_entity,
            2,
            int(p_decl.missing_input_policy())
        );
        p_api->call(
            "predict_set_param",
            p_entity,
            14,
            p_decl.replay_buffer_depth()
        );
        p_api->call(
            "predict_set_param",
            p_entity,
            5,
            int(p_decl.correction())
        );
        if (p_decl.consume_lag_ticks() > 0) {
            p_api->call(
                "predict_set_param",
                p_entity,
                12,
                p_decl.consume_lag_ticks()
            );
        }
        p_api->call(
            "entity_set_simulate_callback",
            p_entity,
            godot::Callable(p_owner, godot::StringName("_network_tick"))
        );
        REQUIRE_MESSAGE(
            int(p_api->call("predict_declare", p_entity)) == int(godot::OK),
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
        owned_nodes.push_back(owner);
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
        // A move parents one owner under another, so every node is detached
        // before any is freed: freeing a container first would leave the
        // pointers to its children dangling in this same list.
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

    godot::Object *server() const {
        return server_api.ptr();
    }

    godot::Object *client(int p_index) const {
        REQUIRE(p_index >= 0);
        REQUIRE(p_index < client_apis.size());
        return client_apis[p_index].ptr();
    }

    // Ids are drawn rather than counted, so this is the only way to name a
    // peer. Nothing may hardcode one. A negative index names the server, as it
    // does for [method peer].
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

    // Returns the new client's index.
    int add_client() {
        godot::Ref<netw::LocalMultiplayerPeer> peer
            = link->create_client_peer();
        client_peers.push_back(peer);
        client_apis.push_back(make_api(peer));
        client_declared.push_back({});
        client_state_sets.push_back({});
        return client_apis.size() - 1;
    }

    // The link first, then the server, then every client, so a packet a pump
    // releases is read by the poll that follows it rather than the next one.
    void pump(int p_times = 1) {
        for (int round = 0; round < p_times; ++round) {
            link->poll();
            poll_api(server_api);
            for (const godot::Ref<godot::RefCounted> &api : client_apis) {
                poll_api(api);
            }
        }
    }

    // Virtual time only. Never a wall clock.
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

    // `p_from` of 0 conditions traffic from every sender.
    void conditions(
        int p_client,
        const godot::Ref<netw::LocalLinkConditions> &p_conditions,
        int p_from = 0
    ) {
        link->set_link_conditions(peer(p_client), p_conditions, p_from);
    }

    // Stands one declaration up on the server and returns its handle. An owner
    // is minted only when the declaration asks for one, so a wrapperless row
    // costs no node.
    godot::RID declare_entity(const EntityDecl &p_decl) {
        godot::Node *owner = nullptr;
        if (p_decl.wants_owner()) {
            owner = mint_owner(p_decl);
        }
        const godot::RID entity = stand_up(server(), p_decl, owner, true);
        const godot::RID state = attach_state(server(), entity, owner, p_decl);
        Carrier *carrier = godot::Object::cast_to<Carrier>(owner);
        attach_input(server(), entity, carrier, p_decl);
        configure_prediction(server(), entity, carrier, p_decl);
        if (!p_decl.name().is_empty()) {
            declared[p_decl.name()] = entity;
            if (state.is_valid()) {
                declared_state_sets[p_decl.name()] = state;
            }
        }
        return entity;
    }

    godot::RID declare_mirror(int p_client, const EntityDecl &p_decl) {
        REQUIRE_MESSAGE(p_decl.route() > 0, "a mirror needs an admitted route");
        Carrier *owner = mint_owner(p_decl);
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
        godot::Object *api = p_client < 0 ? server() : client(p_client);
        godot::Object *object = api->call("entity_get_node", p_entity);
        return godot::Object::cast_to<godot::Node>(object);
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

    // A scene is its entity record, so a declared one is composed rather than
    // spawned. The container carries the one content child every scene verb
    // reads its stem and its layer key off, and the facet is declared before
    // the route binds, which is the rule `scene_declare` states.
    godot::RID declare_scene(
        const godot::StringName &p_name,
        const godot::StringName &p_stem = godot::StringName()
    ) {
        const godot::StringName stem = p_stem.is_empty() ? p_name : p_stem;
        godot::Object *api = server();
        const godot::RID scene = api->call("entity_create");
        REQUIRE_MESSAGE(scene.is_valid(), "entity_create returned no handle");
        NETW_CHECK_EQ(
            int(api->call("scene_declare", scene)),
            int(godot::OK)
        );

        godot::Node *container = memnew(godot::Node);
        container->set_name("Scene");
        godot::Node *level = memnew(godot::Node);
        level->set_name(stem);
        container->add_child(level);
        owned_nodes.push_back(container);

        NETW_CHECK_GT(int(api->call("entity_allocate_route", scene)), 0);
        NETW_CHECK_EQ(
            int(api->call("entity_bind_node", scene, container)),
            int(godot::OK)
        );
        CHECK(bool(api->call("scene_is_declared", scene)));
        api->call("scene_set_param", scene, 0, stem);

        declared[p_name] = scene;
        return scene;
    }

    // Seats [param p_entity] into [param p_scene] by parenting it under the
    // scene's content root, which is what membership by ancestry IS. The
    // `scene_move` verb refuses here rather than doing this: it resolves a
    // destination through the registry `tree_entered` fills, and a declared
    // container never enters a tree.
    void seat(const godot::RID &p_entity, const godot::RID &p_scene) {
        godot::Node *body = node_of(p_entity);
        godot::Node *level = content_of(p_scene);
        REQUIRE_MESSAGE(body != nullptr, "a seated entity needs a body");
        REQUIRE_MESSAGE(level != nullptr, "a scene needs a content root");
        if (body == nullptr || level == nullptr) {
            return;
        }
        const godot::Ref<godot::RefCounted> record
            = entity_script()->call("of", body);
        REQUIRE_MESSAGE(record.is_valid(), "a seated entity needs a record");
        if (record.is_valid()) {
            record->call("reparent_to", level);
        }
        server()->call("interest_flush");
    }

    // The content root every scene verb reads a stem and a layer key off,
    // which is the container's only child.
    godot::Node *content_of(const godot::RID &p_scene) const {
        godot::Node *container = node_of(p_scene);
        if (container == nullptr || container->get_child_count() == 0) {
            return nullptr;
        }
        return container->get_child(0);
    }

    // Services precede scenes, and scenes precede entities that may name one.
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
                const int route
                    = int(server()->call("entity_get_route", entity));
                declare_mirror(
                    row.player_client,
                    EntityDecl(decl).on_route(route)
                );
                server()->call(
                    "entity_grant_control",
                    entity,
                    peer_id(row.player_client)
                );
                pump();
                godot::Node *mirror_owner = node_of(
                    entity_of(decl.name(), row.player_client),
                    row.player_client
                );
                configure_prediction(
                    client(row.player_client),
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

    // A group is reconciled by the peer that predicts its owner, so every
    // member has to exist there. A player mirrored only onto its own client is
    // present nowhere the island can see it, which is what a remote the owner
    // simulates actually is.
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
            const int route
                = int(server()->call("entity_get_route", authority));
            const EntityDecl decl = EntityDecl(p_world.rows[index].decl)
                                        .on_route(route)
                                        .controlled_by(0);
            declare_mirror(seat, decl);
            pump();
            // A member the owner steps has to predict, or it is a replicated
            // row the island can see and the pool has no slot for.
            const godot::RID mirror = entity_of(member, seat);
            configure_prediction(
                client(seat),
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

    // NetwMultiplayer.IslandParam, which crosses as an int.
    enum MemberParam {
        MEMBER_PARAM_FIDELITY = 0,
    };

    // NetwPredict.Fidelity.
    enum Fidelity {
        FIDELITY_PROXY = 0,
        FIDELITY_SIMULATED = 1,
    };

    enum IslandParam {
        ISLAND_PARAM_RECONCILE = 2,
        ISLAND_PARAM_PROMOTION = 3,
        ISLAND_PARAM_PROMOTION_COUNT = 4,
        ISLAND_PARAM_PROMOTION_METERS = 5,
    };

    // Islands are seated after every entity exists, because membership names
    // entities rather than declaring them.
    //
    // A group is reconciled by the peer that PREDICTS it, so the declaration
    // reaches every peer holding all of its entities rather than the server
    // alone. A client short one member has no group to admit, and seating a
    // partial one there would make the roster peer-dependent.
    void declare_island(const WorldDecl::IslandRow &p_row) {
        seat_island(server(), p_row, -1);
        for (int index = 0; index < count(); ++index) {
            seat_island(client(index), p_row, index);
        }
    }

    void seat_island(
        godot::Object *p_api,
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
            p_api->call(
                "predict_island_add",
                owner,
                entity_of(member, p_client)
            );
        }
        for (const godot::StringName &member : p_row.simulated) {
            p_api->call(
                "predict_island_set_member_param",
                owner,
                entity_of(member, p_client),
                MEMBER_PARAM_FIDELITY,
                FIDELITY_SIMULATED
            );
        }
        p_api->call(
            "predict_island_set_param",
            owner,
            ISLAND_PARAM_RECONCILE,
            p_row.reconcile
        );
        if (p_row.promotion != 0) {
            p_api->call(
                "predict_island_set_param",
                owner,
                ISLAND_PARAM_PROMOTION,
                p_row.promotion
            );
            p_api->call(
                "predict_island_set_param",
                owner,
                ISLAND_PARAM_PROMOTION_COUNT,
                p_row.promotion_count
            );
            p_api->call(
                "predict_island_set_param",
                owner,
                ISLAND_PARAM_PROMOTION_METERS,
                p_row.promotion_meters
            );
        }
    }

    bool holds_entity(const godot::StringName &p_name, int p_client) const {
        const godot::HashMap<godot::StringName, godot::RID> &records
            = p_client < 0 ? declared : client_declared[p_client];
        return records.find(p_name) != records.end();
    }

    // What this rig declared under [param name]. Names in, handles out: an RID
    // in a failure message is a number nobody can read.
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

    // The loopback is a star, so client-to-client traffic costs at least two
    // pumps. Budgets are generous for that reason rather than by habit.
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
            godot::Object *server_clock = server()->get("_clock");
            server_clock->call("force_step", 1);
            for (int index = 0; index < count(); ++index) {
                godot::Object *clock = client(index)->get("_clock");
                clock->call("force_step", 1);
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

    // A frame the owner reaches and authority may not. A FRAME-scheduled
    // authority consumes on the frame hook rather than on the tick, so
    // withholding its ticks alone changes nothing: a queue stands ahead of it
    // only across frames authority never ran at all.
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
        godot::Object *server_clock = server()->get("_clock");
        if (p_server_present) {
            server_clock->emit_signal("before_tick_loop");
        }
        for (int index = 0; index < count(); ++index) {
            godot::Object *clock = client(index)->get("_clock");
            clock->emit_signal("before_tick_loop");
        }
        if (p_server_present && p_server_ticks > 0) {
            server_clock->call("force_step", p_server_ticks);
        }
        if (p_client_ticks > 0) {
            for (int index = 0; index < count(); ++index) {
                godot::Object *clock = client(index)->get("_clock");
                clock->call("force_step", p_client_ticks);
            }
        }
        if (p_server_present) {
            server_clock->emit_signal("after_tick_loop");
        }
        for (int index = 0; index < count(); ++index) {
            godot::Object *clock = client(index)->get("_clock");
            clock->emit_signal("after_tick_loop");
        }
        link->advance_time(tick_period_ms);
        poll_api(server_api);
        for (const godot::Ref<godot::RefCounted> &api : client_apis) {
            poll_api(api);
        }
    }

    godot::Object *prediction_handle(
        const godot::StringName &p_name,
        int p_client = -1
    ) const {
        godot::Node *owner = node_of(entity_of(p_name, p_client), p_client);
        REQUIRE_MESSAGE(owner != nullptr, "prediction needs an entity owner");
        godot::Object *wrapper = owner->get_meta("netw_entity");
        REQUIRE_MESSAGE(wrapper != nullptr, "the entity has no wrapper");
        godot::Object *handle = wrapper->get("prediction");
        REQUIRE_MESSAGE(handle != nullptr, "the entity has no prediction");
        return handle;
    }

    // The peer under one index, or the server's at any negative index. What
    // the link verbs are addressed by.
    netw::LocalMultiplayerPeer *peer(int p_client) const {
        if (p_client < 0) {
            return link->get_server_peer().ptr();
        }
        REQUIRE(p_client < client_peers.size());
        return client_peers[p_client].ptr();
    }

private:
    static void poll_api(const godot::Ref<godot::RefCounted> &p_api) {
        if (p_api.is_valid()) {
            p_api->call("poll");
        }
    }
};

#endif

} // namespace netw_test
