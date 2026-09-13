#pragma once

#include "godot/net_peers.hpp"
#include "godot/variant.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/api/server_info.hpp"

namespace netw_test {

class StubDirectory : public netw::LobbyDirectory {
public:
    enum Answering {
        ANSWER_NOTHING,
        ANSWER_PEER,
        ANSWER_REFUSAL,
    };

    Answering answering = ANSWER_PEER;
    godot::Error refusal = godot::ERR_CANT_CONNECT;
    godot::PackedStringArray rows;

    godot::Dictionary hosted_with;
    godot::String joined_address;
    int host_calls = 0;
    int join_calls = 0;
    int browse_calls = 0;
    int leave_calls = 0;

    godot::StringName peer_class() override {
        return godot::StringName("StubLobbyPeer");
    }

    godot::String display_name() override {
        return godot::String("Stub Lobbies");
    }

    int64_t capabilities() override {
        return CAPABILITY_BROWSE;
    }

    godot::Dictionary host_settings() override {
        godot::Dictionary settings = client_settings();
        settings["max_players"] = int64_t(4);
        return settings;
    }

    godot::Dictionary client_settings() override {
        godot::Dictionary settings;
        settings["region"] = godot::String("eu");
        return settings;
    }

    void host_lobby(const godot::Dictionary &p_settings) override {
        host_calls += 1;
        hosted_with = p_settings.duplicate();
        answer();
    }

    void join_lobby(const godot::String &p_address) override {
        join_calls += 1;
        joined_address = p_address;
        answer();
    }

    void list_lobbies() override {
        browse_calls += 1;
        godot::PackedStringArray names;
        godot::TypedArray<netw::NetwServerInfo> infos;
        for (int at = 0; at < rows.size(); at++) {
            godot::Ref<netw::NetwServerInfo> info;
            info.instantiate();
            info->set_players(1);
            info->set_max_players(4);
            names.push_back(godot::String("lobby ") + rows[at]);
            infos.push_back(info);
        }
        publish_lobbies(rows, names, infos);
    }

    void leave_lobby() override {
        leave_calls += 1;
    }

private:
    void answer() {
        if (answering == ANSWER_PEER) {
            godot::Ref<netw::LocalMultiplayerPeer> peer;
            peer.instantiate();
            peer->create_client(11);
            deliver(peer);
        } else if (answering == ANSWER_REFUSAL) {
            fail(refusal, godot::String("the stub directory refused."));
        }
    }
};

class ProbeDirectory : public netw::LobbyDirectory {
public:
    int leaves = 0;
    int listings = 0;

    godot::StringName peer_class() override {
        return godot::StringName("ProbeDirectoryPeer");
    }

    godot::String display_name() override {
        return godot::String("Probe Directory");
    }

    void join_lobby(const godot::String &p_address) override {
        (void)p_address;
    }

    void host_lobby(const godot::Dictionary &p_settings) override {
        (void)p_settings;
    }

    void list_lobbies() override {
        listings += 1;
    }

    void leave_lobby() override {
        leaves += 1;
    }
};

} // namespace netw_test
