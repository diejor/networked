#pragma once

#include "godot/variant.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class DatabaseBackendDict : public NetwDatabaseBackend {
private:
    godot::Dictionary data;
    godot::String ns;

    godot::Dictionary get_ns();
    bool matches_filter(
        const godot::Dictionary &record,
        const godot::Dictionary &filter
    );

public:
    godot::Ref<NetwPromise> initialize(
        const godot::Dictionary &schema,
        const godot::String &slot = ""
    ) override;

    godot::Ref<NetwPromise> upsert(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &record_data
    ) override;

    godot::Ref<NetwPromise> find_by_id(
        const godot::StringName &table,
        const godot::StringName &id
    ) override;

    godot::Ref<NetwPromise> find_all(
        const godot::StringName &table,
        const godot::Dictionary &filter = godot::Dictionary()
    ) override;

    godot::Ref<NetwPromise> erase(
        const godot::StringName &table,
        const godot::StringName &id
    ) override;

    godot::Ref<NetwPromise> list_namespaces() override;

    godot::Ref<NetwPromise> delete_namespace(
        const godot::String &slot
    ) override;
};

} // namespace netw
