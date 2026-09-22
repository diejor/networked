#pragma once

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwEntity;
class NetwMultiplayer;

class NetwPersistenceHandle : public godot::RefCounted {
    GDCLASS(NetwPersistenceHandle, godot::RefCounted)

    godot::ObjectID entity_id;
    godot::RID entity;
    godot::ObjectID relay_session_id;

    NetwMultiplayer *session() const;
    void relay_loaded(const godot::RID &p_entity, bool p_found);
    void relay_saved(const godot::RID &p_entity);

protected:
    static void _bind_methods();

public:
    void bind(NetwEntity *p_entity);
    void bind_session(NetwMultiplayer *p_session);

    godot::Ref<NetwPromise> load();
    godot::Ref<NetwPromise> save();
    bool get_dirty();
    godot::StringName get_record_id();
    godot::StringName get_database();
};

godot::Ref<NetwPersistenceHandle> build_persistence_handle(
    godot::Object *p_entity
);

} // namespace netw
