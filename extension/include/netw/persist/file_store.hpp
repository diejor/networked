#pragma once

#include "godot/variant.hpp"
#include "netw/api/database_connection.hpp"
#include "netw/persist/record_file.hpp"

namespace netw::persist {

class FileStore {
    godot::String root;
    RecordFormat format = RecordFormat::BINARY;

    godot::String path_in(
        const godot::String &p_slot,
        const godot::Dictionary &p_address,
        RecordFormat p_format
    ) const;

public:
    explicit FileStore(
        const godot::String &p_root,
        RecordFormat p_format = RecordFormat::BINARY
    );

    const godot::String &root_path() const;
    godot::String slot_path(const godot::String &p_slot) const;
    godot::String path_of(
        const godot::String &p_slot,
        const godot::Dictionary &p_address
    ) const;
    godot::String staging_path_of(
        const godot::String &p_slot,
        const godot::Dictionary &p_address
    ) const;

    bool has_slot(const godot::String &p_slot) const;
    godot::Error open_slot(const godot::String &p_slot);
    bool erase_slot(const godot::String &p_slot);
    godot::PackedStringArray slot_names() const;

    godot::Error read(
        const godot::String &p_slot,
        const godot::Dictionary &p_address,
        godot::Dictionary &r_envelope,
        bool &r_found
    ) const;
    godot::Error replace(
        const godot::String &p_slot,
        const godot::Dictionary &p_address,
        const godot::Dictionary &p_envelope
    );
    godot::Error erase(
        const godot::String &p_slot,
        const godot::Dictionary &p_address
    );
    godot::Array page(
        const godot::String &p_slot,
        const godot::String &p_schema_name,
        int p_kind,
        const godot::String &p_cursor,
        int p_limit,
        godot::String &r_next
    ) const;
};

class FileConnection : public NetwDatabaseConnection {
    godot::String root;
    godot::String slot;
    RecordFormat format = RecordFormat::BINARY;

public:
    static godot::Ref<FileConnection> opened(
        const godot::String &p_root,
        const godot::String &p_slot,
        RecordFormat p_format = RecordFormat::BINARY
    );

    FileStore store() const;
    const godot::String &slot_name() const;

    godot::Ref<NetwPromise> read(const godot::Dictionary &p_address) override;
    godot::Ref<NetwPromise> scan(const godot::Dictionary &p_request) override;
    godot::Ref<NetwPromise> write_batch(
        const godot::Array &p_operations
    ) override;
    godot::Ref<NetwPromise> close() override;
};

} // namespace netw::persist
