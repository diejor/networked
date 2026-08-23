#pragma once

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwIdentity : public godot::RefCounted {
    GDCLASS(NetwIdentity, godot::RefCounted)

    godot::StringName username;
    godot::String external_id;
    godot::StringName service;
    godot::Dictionary metadata;

protected:
    static void _bind_methods();

public:
    godot::StringName get_username() const;
    void set_username(const godot::StringName &value);
    godot::String get_external_id() const;
    void set_external_id(const godot::String &value);
    godot::StringName get_service() const;
    void set_service(const godot::StringName &value);
    godot::Dictionary get_metadata() const;
    void set_metadata(const godot::Dictionary &value);

    static godot::String username_of(godot::Object *p_node);
    static godot::Variant stable_id_of(godot::Object *p_node);
};

} // namespace netw
