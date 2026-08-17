#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/promise.hpp"

namespace netw {

using namespace godot;

class NetwDatabaseBackend : public Resource {
    GDCLASS(NetwDatabaseBackend, Resource)

protected:
    static void _bind_methods();

public:
    virtual Ref<NetwPromise> initialize(
        const Dictionary &schema,
        const String &slot = ""
    );

    virtual Ref<NetwPromise> upsert(
        const StringName &table,
        const StringName &id,
        const Dictionary &data
    );

    virtual Ref<NetwPromise> commit(const Array &operations = Array());

    virtual Ref<NetwPromise> find_by_id(
        const StringName &table,
        const StringName &id
    );

    virtual Ref<NetwPromise> find_all(
        const StringName &table,
        const Dictionary &filter = Dictionary()
    );

    virtual Ref<NetwPromise> erase(
        const StringName &table,
        const StringName &id
    );

    virtual Ref<NetwPromise> warm(const Array &directives = Array());

    virtual Ref<NetwPromise> list_namespaces();

    virtual Ref<NetwPromise> delete_namespace(const String &slot);

    GDVIRTUAL2R(
        Ref<NetwPromise>,
        _initialize,
        const Dictionary &,
        const String &
    )
    GDVIRTUAL3R(
        Ref<NetwPromise>,
        _upsert,
        const StringName &,
        const StringName &,
        const Dictionary &
    )
    GDVIRTUAL1R(Ref<NetwPromise>, _commit, const Array &)
    GDVIRTUAL2R(
        Ref<NetwPromise>,
        _find_by_id,
        const StringName &,
        const StringName &
    )
    GDVIRTUAL2R(
        Ref<NetwPromise>,
        _find_all,
        const StringName &,
        const Dictionary &
    )
    GDVIRTUAL2R(
        Ref<NetwPromise>,
        _delete,
        const StringName &,
        const StringName &
    )
    GDVIRTUAL1R(Ref<NetwPromise>, _warm, const Array &)
    GDVIRTUAL0R(Ref<NetwPromise>, _list_namespaces)
    GDVIRTUAL1R(Ref<NetwPromise>, _delete_namespace, const String &)
};

// In-memory test backend implementation.
class NetwDatabaseBackendDict : public NetwDatabaseBackend {
    GDCLASS(NetwDatabaseBackendDict, NetwDatabaseBackend)

private:
    Dictionary data;
    String ns;

    Dictionary get_ns();
    bool matches_filter(
        const Dictionary &record,
        const Dictionary &filter
    );

protected:
    static void _bind_methods();

public:
    Ref<NetwPromise> initialize(
        const Dictionary &schema,
        const String &slot = ""
    ) override;

    Ref<NetwPromise> upsert(
        const StringName &table,
        const StringName &id,
        const Dictionary &record_data
    ) override;

    Ref<NetwPromise> find_by_id(
        const StringName &table,
        const StringName &id
    ) override;

    Ref<NetwPromise> find_all(
        const StringName &table,
        const Dictionary &filter = Dictionary()
    ) override;

    Ref<NetwPromise> erase(
        const StringName &table,
        const StringName &id
    ) override;

    Ref<NetwPromise> list_namespaces() override;

    Ref<NetwPromise> delete_namespace(const String &slot) override;
};

} // namespace netw
