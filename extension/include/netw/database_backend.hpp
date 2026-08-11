#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

// Abstract storage contract every NetwDatabase backend implements natively.
//
// Every verb returns Variant because a backend is allowed to be asynchronous:
// a GDScript override that awaits returns a coroutine state rather than a
// value, and a concrete return type here would silently coerce that object to
// an empty result. The caller awaits what it is handed, which yields the value
// for a synchronous backend and resumes the coroutine for an asynchronous one.
class NetwDatabaseBackend : public Resource {
    GDCLASS(NetwDatabaseBackend, Resource)

protected:
    static void _bind_methods();

public:
    virtual Variant initialize(
        const Dictionary &schema,
        const String &slot = ""
    );

    virtual Variant upsert(
        const StringName &table,
        const StringName &id,
        const Dictionary &data
    );

    virtual Variant commit(const Array &operations = Array());

    virtual Variant find_by_id(
        const StringName &table,
        const StringName &id
    );

    virtual Variant find_all(
        const StringName &table,
        const Dictionary &filter = Dictionary()
    );

    virtual Variant erase(
        const StringName &table,
        const StringName &id
    );

    virtual Variant warm(const Array &directives = Array());

    virtual Variant list_namespaces();

    virtual Variant delete_namespace(const String &slot);

    GDVIRTUAL2R(
        Variant,
        _initialize,
        const Dictionary &,
        const String &
    )
    GDVIRTUAL3R(
        Variant,
        _upsert,
        const StringName &,
        const StringName &,
        const Dictionary &
    )
    GDVIRTUAL1R(Variant, _commit, const Array &)
    GDVIRTUAL2R(
        Variant,
        _find_by_id,
        const StringName &,
        const StringName &
    )
    GDVIRTUAL2R(
        Variant,
        _find_all,
        const StringName &,
        const Dictionary &
    )
    GDVIRTUAL2R(
        Variant,
        _delete,
        const StringName &,
        const StringName &
    )
    GDVIRTUAL1R(Variant, _warm, const Array &)
    GDVIRTUAL0R(Variant, _list_namespaces)
    GDVIRTUAL1R(Variant, _delete_namespace, const String &)
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
    Variant initialize(
        const Dictionary &schema,
        const String &slot = ""
    ) override;

    Variant upsert(
        const StringName &table,
        const StringName &id,
        const Dictionary &record_data
    ) override;

    Variant find_by_id(
        const StringName &table,
        const StringName &id
    ) override;

    Variant find_all(
        const StringName &table,
        const Dictionary &filter = Dictionary()
    ) override;

    Variant erase(
        const StringName &table,
        const StringName &id
    ) override;

    Variant list_namespaces() override;

    Variant delete_namespace(const String &slot) override;
};

} // namespace netw
