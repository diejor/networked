#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/string_name.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwDatabase;
class NetwMultiplayer;
class NetwPromise;

class NetwTableHandle : public godot::RefCounted {
    GDCLASS(NetwTableHandle, godot::RefCounted)

    godot::RID table;
    godot::ObjectID session_id;

    NetwMultiplayer *session() const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwTableHandle> over(
        const godot::RID &p_table,
        NetwMultiplayer *p_session
    );
    static godot::Ref<NetwTableHandle> of(
        godot::Node *p_node,
        const godot::StringName &p_name
    );

    void announce(int64_t p_tick);

    godot::RID get_table() const;
    godot::StringName get_schema_name() const;
    bool get_is_valid() const;
    int get_wire_hash() const;
    int64_t get_tick() const;

    void set_reliable(bool p_reliable);
    bool get_reliable() const;

    godot::Error write_routes(const godot::PackedInt64Array &p_routes);
    godot::Error write_column(int p_column, const godot::Variant &p_data);
    godot::Error commit();

    godot::PackedInt64Array read_routes() const;
    godot::Variant read_column(int p_column) const;
    godot::PackedInt64Array read_births() const;
    godot::PackedInt64Array read_deaths() const;
    int row_of(int64_t p_route) const;
    godot::PackedInt32Array rows_of(
        const godot::PackedInt64Array &p_routes
    ) const;

    godot::Ref<NetwPromise> save(
        const godot::Ref<NetwDatabase> &p_database,
        const godot::StringName &p_key,
        const godot::PackedStringArray &p_ids
    );
    godot::Ref<NetwPromise> load(
        const godot::Ref<NetwDatabase> &p_database,
        const godot::StringName &p_key
    );
};

} // namespace netw
