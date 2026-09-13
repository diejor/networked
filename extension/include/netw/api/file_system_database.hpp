#pragma once

#include "godot/variant.hpp"
#include "netw/api/database_backend.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class FileSystemDatabase : public NetwDatabaseBackend {
    GDCLASS(FileSystemDatabase, NetwDatabaseBackend)

private:
    godot::String base_dir = "res://saves";
    godot::String app_id;
    bool use_text_format = false;
    godot::String root;

    godot::String extension() const;
    godot::String app_dir() const;
    godot::String root_for(const godot::String &slot) const;
    godot::String active_root() const;
    godot::String path_for(
        const godot::StringName &table,
        const godot::StringName &id
    ) const;
    godot::String dir_for(const godot::StringName &table) const;

    void claim_root();
    void warn_ghost_tables(const godot::Dictionary &schema) const;

    static godot::Error remove_recursive(const godot::String &path);
    static bool matches_filter(
        const godot::Dictionary &record,
        const godot::Dictionary &filter
    );

protected:
    static void _bind_methods();

public:
    static void forget_claimed_roots();

    void set_base_dir(const godot::String &p_base_dir);
    godot::String get_base_dir() const {
        return base_dir;
    }

    void set_app_id(const godot::String &p_app_id);
    godot::String get_app_id() const {
        return app_id;
    }

    void set_use_text_format(bool p_use_text_format);
    bool get_use_text_format() const {
        return use_text_format;
    }

    godot::Ref<NetwPromise> initialize(
        const godot::Dictionary &schema,
        const godot::String &slot = ""
    ) override;

    godot::Ref<NetwPromise> upsert(
        const godot::StringName &table,
        const godot::StringName &id,
        const godot::Dictionary &data
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
