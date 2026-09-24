#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/entity.hpp"
#include "netw/lifecycle/rule.hpp"

namespace netw {

class NetwLifecycleRequest : public godot::RefCounted {
    GDCLASS(NetwLifecycleRequest, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    enum Kind {
        KIND_SPAWN = int(lifecycle::Kind::SPAWN),
        KIND_DESPAWN = int(lifecycle::Kind::DESPAWN),
        KIND_REPARENT = int(lifecycle::Kind::REPARENT),
    };

    static godot::Ref<NetwLifecycleRequest> create(
        lifecycle::Kind p_kind,
        int64_t p_requester,
        const godot::Ref<NetwEntity> &p_entity,
        godot::Node *p_destination
    );

    void deny(const godot::String &p_reason);
    Kind get_kind() const {
        return kind;
    }
    int64_t get_requester() const {
        return requester;
    }
    godot::Ref<NetwEntity> get_entity() const {
        return entity;
    }
    godot::Node *get_destination() const;
    bool get_denied() const {
        return denied;
    }
    void set_denied(bool p_denied);
    godot::String get_reason() const {
        return reason;
    }

private:
    Kind kind = KIND_SPAWN;
    int64_t requester = 0;
    godot::Ref<NetwEntity> entity;
    godot::ObjectID destination;
    bool denied = false;
    godot::String reason;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwLifecycleRequest::Kind);
