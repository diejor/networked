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

class NetwPersistence : public godot::RefCounted {
    GDCLASS(NetwPersistence, godot::RefCounted)

    godot::ObjectID entity_id;
    godot::RID entity;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    void bind(NetwEntity *p_entity);
    void announce_loaded(const godot::RID &p_entity, bool p_found);
    void announce_saved(const godot::RID &p_entity);

    godot::Ref<NetwPromise> load();
    godot::Ref<NetwPromise> save();
    bool get_dirty();
    godot::StringName get_record_id();
    godot::StringName get_database();
};

godot::Ref<NetwPersistence> build_persistence_handle(godot::Object *p_entity);

} // namespace netw
