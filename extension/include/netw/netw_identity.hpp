#pragma once

#include "godot/node.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwIdentity : public RefCounted {
    GDCLASS(NetwIdentity, RefCounted)

    StringName username;
    String external_id;
    StringName service;
    Dictionary metadata;

protected:
    static void _bind_methods();

public:
    StringName get_username() const;
    void set_username(const StringName &value);
    String get_external_id() const;
    void set_external_id(const String &value);
    StringName get_service() const;
    void set_service(const StringName &value);
    Dictionary get_metadata() const;
    void set_metadata(const Dictionary &value);

    static String username_of(Object *p_node);
    static Variant stable_id_of(Object *p_node);
};

} // namespace netw
