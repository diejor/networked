#pragma once

#include <godot_cpp/classes/scene_multiplayer.hpp>

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"

namespace netw_test {

class SimStand {
    struct Taught {
        godot::StringName id;
        godot::Callable build;
    };

    godot::Ref<netw::LocalLoopbackSession> link;
    godot::Vector<godot::Ref<netw::NetwMultiplayer>> apis;
    godot::Vector<godot::Ref<netw::LocalMultiplayerPeer>> peers;
    godot::Vector<godot::Node *> mounts;
    godot::Vector<godot::NodePath> paths;
    godot::Vector<Taught> taught;
    int tickrate = 30;

    static godot::Ref<netw::NetwMultiplayer> make(
        const godot::Ref<netw::LocalMultiplayerPeer> &p_peer
    ) {
        godot::Ref<godot::SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(godot::NodePath("/"));
        godot::Ref<netw::NetwMultiplayer> api
            = netw::NetwMultiplayer::make(inner, godot::Ref<godot::Script>());
        if (api.is_valid()) {
            api->session_set_authority_peer(1);
            api->set("multiplayer_peer", p_peer);
        }
        return api;
    }

public:
    explicit SimStand(int p_clients) {
        link.instantiate();
        apis.push_back(make(link->get_server_peer()));
        for (int at = 0; at < p_clients; ++at) {
            const godot::Ref<netw::LocalMultiplayerPeer> peer
                = link->create_client_peer();
            peers.push_back(peer);
            apis.push_back(make(peer));
        }
        pump(1);
    }

    ~SimStand() {
        godot::SceneTree *tree = netw::gd::scene_tree();
        for (const godot::NodePath &path : paths) {
            if (tree != nullptr) {
                tree->set_multiplayer(
                    godot::Ref<godot::MultiplayerAPI>(),
                    path
                );
            }
        }
        for (godot::Node *mount : mounts) {
            mount->get_parent()->remove_child(mount);
            memdelete(mount);
        }
        link->reset();
    }

    SimStand(const SimStand &) = delete;
    SimStand &operator=(const SimStand &) = delete;

    void teach(const godot::StringName &p_id, const godot::Callable &p_build) {
        Taught row;
        row.id = p_id;
        row.build = p_build;
        taught.push_back(row);
    }

    bool ready() const {
        for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
            if (api.is_null()) {
                return false;
            }
        }
        return true;
    }

    netw::NetwMultiplayer *session(int p_client) const {
        return apis[p_client + 1].ptr();
    }

    netw::LocalLoopbackSession *loopback() const {
        return link.ptr();
    }

    int peer_id(int p_client) const {
        return p_client < 0 ? link->get_server_peer()->get_unique_id()
                            : peers[p_client]->get_unique_id();
    }

    netw::LocalMultiplayerPeer *peer(int p_client) const {
        return p_client < 0 ? link->get_server_peer().ptr()
                            : peers[p_client].ptr();
    }

    void mount() {
        godot::Node *scene = netw::gd::scene_root();
        godot::SceneTree *tree = netw::gd::scene_tree();
        for (int at = 0; at < apis.size(); ++at) {
            godot::Node *branch = memnew(godot::Node);
            branch->set_name(godot::vformat("SimStand%d", at));
            scene->add_child(branch);
            mounts.push_back(branch);
            apis[at]->session_set_root(
                godot::Callable(branch, "get_node").bind(godot::NodePath("."))
            );
            tree->set_multiplayer(
                godot::Ref<godot::MultiplayerAPI>(
                    godot::Object::cast_to<godot::MultiplayerAPI>(
                        apis[at].ptr()
                    )
                ),
                branch->get_path()
            );
            paths.push_back(branch->get_path());
        }
        pump(1);
    }

    godot::Node *arena() {
        godot::Node *host = nullptr;
        for (int at = 0; at < mounts.size(); ++at) {
            godot::Node *made = memnew(godot::Node);
            made->set_name("Arena");
            mounts[at]->add_child(made);
            if (at == 0) {
                host = made;
            }
        }
        pump(1);
        return host;
    }

    godot::Node *arena_of(int p_client) const {
        return mounts[p_client + 1]->get_node_or_null(godot::NodePath("Arena"));
    }

    void arm(int p_tickrate, int p_display_offset = 2) {
        tickrate = p_tickrate;
        for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
            godot::Ref<netw::NetwClockConfig> config;
            config.instantiate();
            config->set("tickrate", p_tickrate);
            config->set("display_offset", p_display_offset);
            api->clock_initialize(config);
            api->clock_engine().set_manual_tick(true);
            godot::Array types;
            types.push_back(int(godot::Variant::STRING));
            godot::Array quantizers;
            quantizers.push_back(godot::Variant());
            for (const Taught &row : taught) {
                api->spawn_register_constructor(
                    row.id,
                    row.build,
                    types,
                    quantizers
                );
            }
        }
    }

    void pump(int p_rounds) {
        for (int round = 0; round < p_rounds; ++round) {
            link->poll();
            for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
                api->poll();
            }
        }
    }

    void step_ticks(int p_ticks) {
        for (int tick = 0; tick < p_ticks; ++tick) {
            for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
                api->clock_engine().force_step(1);
            }
            link->advance_time(1000.0 / double(tickrate));
            for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
                api->poll();
            }
        }
    }

    void physics_frame(double p_delta, double p_client_scale = 1.0) {
        for (int at = 0; at < apis.size(); ++at) {
            const double scale = at == 0 ? 1.0 : p_client_scale;
            apis[at]->clock_physics_step(p_delta * scale);
        }
        link->advance_time(1000.0 * p_delta);
        for (const godot::Ref<netw::NetwMultiplayer> &api : apis) {
            api->poll();
        }
    }

    godot::Node *node_at(int p_client, int p_route) const {
        netw::NetwMultiplayer *api = session(p_client);
        return api->entity_get_node(api->entity_from_route(p_route));
    }
};

} // namespace netw_test
