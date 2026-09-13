#pragma once

#include <cstdint>

#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMultiplayer;

class NetwServerInfo : public godot::Resource {
    GDCLASS(NetwServerInfo, godot::Resource)

    godot::String motd;
    int64_t players = 0;
    int64_t max_players = 0;
    godot::StringName game_mode;
    godot::String version;
    godot::StringName app_id;
    bool is_local_listener = false;
    int64_t latency_ms = -1;
    int64_t visibility = 0;
    godot::Dictionary metadata;

protected:
    static void _bind_methods();

public:
    enum Visibility {
        VISIBILITY_PUBLIC = 0,
        VISIBILITY_FRIENDS_ONLY = 1,
        VISIBILITY_PRIVATE = 2,
    };

    godot::String get_motd() const {
        return motd;
    }
    void set_motd(const godot::String &p_motd) {
        motd = p_motd;
    }
    int64_t get_players() const {
        return players;
    }
    void set_players(int64_t p_players) {
        players = p_players;
    }
    int64_t get_max_players() const {
        return max_players;
    }
    void set_max_players(int64_t p_max) {
        max_players = p_max;
    }
    godot::StringName get_game_mode() const {
        return game_mode;
    }
    void set_game_mode(const godot::StringName &p_mode) {
        game_mode = p_mode;
    }
    godot::String get_version() const {
        return version;
    }
    void set_version(const godot::String &p_version) {
        version = p_version;
    }
    godot::StringName get_app_id() const {
        return app_id;
    }
    void set_app_id(const godot::StringName &p_app_id) {
        app_id = p_app_id;
    }
    int64_t get_latency_ms() const {
        return latency_ms;
    }
    void set_latency_ms(int64_t p_latency_ms) {
        latency_ms = p_latency_ms;
    }
    int64_t get_visibility() const {
        return visibility;
    }
    void set_visibility(int64_t p_visibility) {
        visibility = p_visibility;
    }
    bool get_is_local_listener() const {
        return is_local_listener;
    }
    void set_is_local_listener(bool p_local) {
        is_local_listener = p_local;
    }
    godot::Dictionary get_metadata() const {
        return metadata;
    }
    void set_metadata(const godot::Dictionary &p_metadata) {
        metadata = p_metadata;
    }

    void copy_values_from(const NetwServerInfo &p_source);

    static godot::Ref<NetwServerInfo> from_session(NetwMultiplayer *p_api);
    static godot::PackedByteArray to_payload(
        const godot::Ref<NetwServerInfo> &p_info
    );
    static godot::Ref<NetwServerInfo> from_payload(
        const godot::PackedByteArray &p_bytes
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwServerInfo::Visibility);
