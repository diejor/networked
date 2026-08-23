#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwDatabaseBackend : public godot::Resource {
    GDCLASS(NetwDatabaseBackend, godot::Resource)

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwDatabaseBackend> in_memory();

    virtual godot::Ref<NetwPromise> initialize(
        const godot::Dictionary &schema,
        const godot::String &slot = ""
    );

    virtual godot::Ref<NetwPromise> upsert(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &data
    );

    virtual godot::Ref<NetwPromise> commit(
        const godot::Array &operations = godot::Array()
    );

    virtual godot::Ref<NetwPromise> find_by_id(
        const godot::StringName &table,
        const godot::StringName &id
    );

    virtual godot::Ref<NetwPromise> find_all(
        const godot::StringName &table,
        const godot::Dictionary &filter = godot::Dictionary()
    );

    virtual godot::Ref<NetwPromise> erase(
        const godot::StringName &table,
        const godot::StringName &id
    );

    virtual godot::Ref<NetwPromise> warm(
        const godot::Array &directives = godot::Array()
    );

    virtual godot::Ref<NetwPromise> list_namespaces();

    virtual godot::Ref<NetwPromise> delete_namespace(
        const godot::String &slot
    );

    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _initialize,
        const godot::Dictionary &,
        const godot::String &
    )
    GDVIRTUAL3R(
        godot::Ref<NetwPromise>,
        _upsert,
        const godot::StringName &,
        const godot::StringName &,
        const godot::Dictionary &
    )
    GDVIRTUAL1R(godot::Ref<NetwPromise>, _commit, const godot::Array &)
    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _find_by_id,
        const godot::StringName &,
        const godot::StringName &
    )
    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _find_all,
        const godot::StringName &,
        const godot::Dictionary &
    )
    GDVIRTUAL2R(
        godot::Ref<NetwPromise>,
        _delete,
        const godot::StringName &,
        const godot::StringName &
    )
    GDVIRTUAL1R(godot::Ref<NetwPromise>, _warm, const godot::Array &)
    GDVIRTUAL0R(godot::Ref<NetwPromise>, _list_namespaces)
    GDVIRTUAL1R(
        godot::Ref<NetwPromise>,
        _delete_namespace,
        const godot::String &
    )
};

} // namespace netw
