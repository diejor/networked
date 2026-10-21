#pragma once

#include <cstdint>

#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/enums.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwMultiplayer;

enums::NetwEndpoint::Status endpoint_status(int64_t p_flags, int64_t p_error);

class NetwEndpoint : public godot::RefCounted, public enums::NetwEndpoint {
    GDCLASS(NetwEndpoint, godot::RefCounted)

    godot::ObjectID session_id;
    godot::RID row;
    godot::StringName peer_class;
    godot::String address;
    bool listed = false;
    godot::Ref<NetwServerInfo> unanswered;

    NetwMultiplayer *listed_session() const;

protected:
    static void _bind_methods();

public:
    void bind_to(
        NetwMultiplayer *p_session,
        const godot::RID &p_row,
        const godot::StringName &p_peer_class,
        const godot::String &p_address
    );
    void unlist();
    godot::RID get_row() const;

    godot::StringName get_peer_class() const;
    godot::String get_address() const;
    godot::String get_display_name() const;
    godot::String get_source() const;
    Status get_status() const;
    godot::Ref<NetwServerInfo> get_info();
    bool get_is_listed() const;
    bool get_is_invited() const;
    godot::String get_invited_by() const;
    void probe();
};

} // namespace netw
