#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/object.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwDatabaseBackend : public godot::Resource {
    GDCLASS(NetwDatabaseBackend, godot::Resource)

protected:
    static void _bind_methods();

public:
    godot::Ref<NetwPromise> open(
        godot::Object *p_session,
        const godot::StringName &p_slot
    );
    godot::Ref<NetwPromise> list_slots(godot::Object *p_session);
    godot::Ref<NetwPromise> delete_slot(
        godot::Object *p_session,
        const godot::StringName &p_slot
    );

    virtual godot::Ref<NetwPromise> open_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    );
    virtual godot::Ref<NetwPromise> list_slots_default(
        godot::Object *p_session
    );
    virtual godot::Ref<NetwPromise> delete_slot_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    );

    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _open,
        godot::Object *,
        const godot::StringName &
    )
    GDVIRTUAL1R(godot::Ref<NetwPromise>, _list_slots, godot::Object *)
    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _delete_slot,
        godot::Object *,
        const godot::StringName &
    )
};

class MemoryDatabase : public NetwDatabaseBackend {
    GDCLASS(MemoryDatabase, NetwDatabaseBackend)

    godot::StringName store;

protected:
    static void _bind_methods();

public:
    void set_store(const godot::StringName &p_store);
    godot::StringName get_store() const {
        return store;
    }

    godot::Ref<NetwPromise> open_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    ) override;
    godot::Ref<NetwPromise> list_slots_default(godot::Object *p_session
    ) override;
    godot::Ref<NetwPromise> delete_slot_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    ) override;
};

class FileSystemDatabase : public NetwDatabaseBackend {
    GDCLASS(FileSystemDatabase, NetwDatabaseBackend)

    godot::String root = "user://saves";

protected:
    static void _bind_methods();

public:
    void set_root(const godot::String &p_root);
    godot::String get_root() const {
        return root;
    }

    godot::Ref<NetwPromise> open_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    ) override;
    godot::Ref<NetwPromise> list_slots_default(godot::Object *p_session
    ) override;
    godot::Ref<NetwPromise> delete_slot_default(
        godot::Object *p_session,
        const godot::StringName &p_slot
    ) override;
};

} // namespace netw
