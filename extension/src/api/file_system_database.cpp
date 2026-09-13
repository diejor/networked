#include "netw/api/file_system_database.hpp"

#include "godot/class_db.hpp"
#include "godot/file_system.hpp"
#include "godot/hash_map.hpp"
#include "godot/object.hpp"
#include "godot/os.hpp"
#include "godot/utility.hpp"
#include "netw/api/record.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr const char *TEXT_EXTENSION = ".tres";
constexpr const char *BIN_EXTENSION = ".res";
constexpr const char *RECORD_TYPE = "DictionaryRecord";

godot::HashMap<String, ObjectID> &claimed_roots() {
    static godot::HashMap<String, ObjectID> held;
    return held;
}

} // namespace

void FileSystemDatabase::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_base_dir", "base_dir"),
        &FileSystemDatabase::set_base_dir
    );
    ClassDB::bind_method(
        D_METHOD("get_base_dir"),
        &FileSystemDatabase::get_base_dir
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "base_dir", PROPERTY_HINT_DIR),
        "set_base_dir",
        "get_base_dir"
    );

    ClassDB::bind_method(
        D_METHOD("set_app_id", "app_id"),
        &FileSystemDatabase::set_app_id
    );
    ClassDB::bind_method(
        D_METHOD("get_app_id"),
        &FileSystemDatabase::get_app_id
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "app_id"),
        "set_app_id",
        "get_app_id"
    );

    ClassDB::bind_method(
        D_METHOD("set_use_text_format", "use_text_format"),
        &FileSystemDatabase::set_use_text_format
    );
    ClassDB::bind_method(
        D_METHOD("get_use_text_format"),
        &FileSystemDatabase::get_use_text_format
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "use_text_format"),
        "set_use_text_format",
        "get_use_text_format"
    );
}

void FileSystemDatabase::forget_claimed_roots() {
    claimed_roots().clear();
}

void FileSystemDatabase::set_base_dir(const String &p_base_dir) {
    base_dir = p_base_dir;
}

void FileSystemDatabase::set_app_id(const String &p_app_id) {
    app_id = p_app_id;
}

void FileSystemDatabase::set_use_text_format(bool p_use_text_format) {
    use_text_format = p_use_text_format;
}

String FileSystemDatabase::extension() const {
    return use_text_format ? String(TEXT_EXTENSION) : String(BIN_EXTENSION);
}

String FileSystemDatabase::app_dir() const {
    return app_id.is_empty() ? base_dir : base_dir.path_join(app_id);
}

String FileSystemDatabase::root_for(const String &p_slot) const {
    return p_slot.is_empty() ? app_dir() : app_dir().path_join(p_slot);
}

String FileSystemDatabase::active_root() const {
    return root.is_empty() ? base_dir : root;
}

String FileSystemDatabase::path_for(
    const StringName &p_table,
    const StringName &p_id
) const {
    return dir_for(p_table).path_join(String(p_id) + extension());
}

String FileSystemDatabase::dir_for(const StringName &p_table) const {
    return active_root().path_join(String(p_table));
}

void FileSystemDatabase::claim_root() {
    godot::HashMap<String, ObjectID> &held = claimed_roots();
    LocalVector<String> stale;
    for (const KeyValue<String, ObjectID> &row : held) {
        if (gd::object_of(row.value) == nullptr) {
            stale.push_back(row.key);
        }
    }
    for (const String &path : stale) {
        held.erase(path);
    }

    const String global = gd::globalized(root);
    const godot::HashMap<String, ObjectID>::ConstIterator claimed
        = held.find(global);
    if (claimed && gd::object_of(claimed->value) != this
        && gd::object_of(claimed->value) != nullptr) {
        NETW_ERROR(
            sys::TABLE,
            "two file-system databases point at the slot root '%s', which "
            "corrupts data and reports every table as a ghost. Share one "
            "NetwDatabase, or give them different base_dir",
            root
        );
    }
    held[global] = get_instance_id();
}

void FileSystemDatabase::warn_ghost_tables(const Dictionary &p_schema) const {
    const Ref<DirAccess> dir = DirAccess::open(active_root());
    if (dir.is_null()) {
        return;
    }
    dir->list_dir_begin();
    String entry = dir->get_next();
    while (!entry.is_empty()) {
        if (dir->current_is_dir() && !entry.begins_with(".")
            && !p_schema.has(StringName(entry))) {
            NETW_WARN(
                sys::TABLE,
                "ghost table '%s' found at '%s'. It is not in the current "
                "schema. Run a manual migration, or delete the directory if "
                "it is no longer needed",
                entry,
                active_root().path_join(entry)
            );
        }
        entry = dir->get_next();
    }
    dir->list_dir_end();
}

Ref<NetwPromise> FileSystemDatabase::initialize(
    const Dictionary &p_schema,
    const String &p_slot
) {
    OS *os = OS::get_singleton();
    if (os != nullptr && !os->has_feature("editor")
        && base_dir.begins_with("res://")) {
        base_dir = base_dir.replace("res://", "user://");
    }

    root = root_for(p_slot);
    claim_root();

    if (!DirAccess::dir_exists_absolute(root)) {
        const Error made = DirAccess::make_dir_recursive_absolute(root);
        if (made != OK) {
            NETW_ERROR(
                sys::TABLE,
                "could not create the slot root '%s': %s",
                root,
                gd::error_name(made)
            );
            return NetwPromise::resolved(int64_t(made));
        }
    }

    const Array tables = p_schema.keys();
    for (int at = 0; at < tables.size(); at++) {
        const String table_dir = dir_for(StringName(tables[at]));
        if (!DirAccess::dir_exists_absolute(table_dir)) {
            DirAccess::make_dir_recursive_absolute(table_dir);
        }
    }

    warn_ghost_tables(p_schema);
    return NetwPromise::resolved(int64_t(OK));
}

Ref<NetwPromise> FileSystemDatabase::upsert(
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_data
) {
    const String table_dir = dir_for(p_table);
    if (!DirAccess::dir_exists_absolute(table_dir)) {
        DirAccess::make_dir_recursive_absolute(table_dir);
    }

    const String path = path_for(p_table, p_id);
    Ref<DictionaryRecord> record;
    record.instantiate();
    if (gd::resource_exists(path)) {
        const Ref<DictionaryRecord> stored = gd::load_fresh(path, RECORD_TYPE);
        if (stored.is_valid()) {
            record->set_data(stored->get_data().duplicate());
        }
    }

    Dictionary merged = record->get_data();
    const Array keys = p_data.keys();
    for (int at = 0; at < keys.size(); at++) {
        merged[keys[at]] = p_data[keys[at]];
    }
    record->set_data(merged);

    return NetwPromise::resolved(int64_t(gd::save_resource(record, path)));
}

Ref<NetwPromise> FileSystemDatabase::find_by_id(
    const StringName &p_table,
    const StringName &p_id
) {
    const String path = path_for(p_table, p_id);
    if (!gd::resource_exists(path)) {
        return NetwPromise::resolved(Dictionary());
    }
    const Ref<DictionaryRecord> stored = gd::load_fresh(path, RECORD_TYPE);
    if (stored.is_null()) {
        return NetwPromise::resolved(Dictionary());
    }
    return NetwPromise::resolved(stored->get_data().duplicate());
}

Ref<NetwPromise> FileSystemDatabase::find_all(
    const StringName &p_table,
    const Dictionary &p_filter
) {
    TypedArray<Dictionary> results;
    const String table_dir = dir_for(p_table);
    if (!DirAccess::dir_exists_absolute(table_dir)) {
        return NetwPromise::resolved(results);
    }
    const Ref<DirAccess> dir = DirAccess::open(table_dir);
    if (dir.is_null()) {
        return NetwPromise::resolved(results);
    }

    const String ext = extension();
    dir->list_dir_begin();
    String entry = dir->get_next();
    while (!entry.is_empty()) {
        if (!dir->current_is_dir() && entry.ends_with(ext)) {
            const Ref<DictionaryRecord> stored
                = gd::load_fresh(table_dir.path_join(entry), RECORD_TYPE);
            if (stored.is_valid()
                && matches_filter(stored->get_data(), p_filter)) {
                results.push_back(stored->get_data().duplicate());
            }
        }
        entry = dir->get_next();
    }
    dir->list_dir_end();

    return NetwPromise::resolved(results);
}

Ref<NetwPromise> FileSystemDatabase::erase(
    const StringName &p_table,
    const StringName &p_id
) {
    const String path = path_for(p_table, p_id);
    if (!gd::resource_exists(path)) {
        return NetwPromise::resolved(int64_t(OK));
    }
    return NetwPromise::resolved(int64_t(DirAccess::remove_absolute(path)));
}

Ref<NetwPromise> FileSystemDatabase::list_namespaces() {
    TypedArray<StringName> slots;
    const Ref<DirAccess> dir = DirAccess::open(app_dir());
    if (dir.is_null()) {
        return NetwPromise::resolved(slots);
    }
    dir->list_dir_begin();
    String entry = dir->get_next();
    while (!entry.is_empty()) {
        if (dir->current_is_dir() && !entry.begins_with(".")) {
            slots.push_back(StringName(entry));
        }
        entry = dir->get_next();
    }
    dir->list_dir_end();
    return NetwPromise::resolved(slots);
}

Ref<NetwPromise> FileSystemDatabase::delete_namespace(const String &p_slot) {
    if (p_slot.is_empty()) {
        return NetwPromise::resolved(int64_t(ERR_INVALID_PARAMETER));
    }
    const String slot_root = root_for(p_slot);
    if (!DirAccess::dir_exists_absolute(slot_root)) {
        return NetwPromise::resolved(int64_t(OK));
    }
    return NetwPromise::resolved(int64_t(remove_recursive(slot_root)));
}

Error FileSystemDatabase::remove_recursive(const String &p_path) {
    const Ref<DirAccess> dir = DirAccess::open(p_path);
    if (dir.is_null()) {
        return ERR_CANT_OPEN;
    }
    dir->list_dir_begin();
    String entry = dir->get_next();
    while (!entry.is_empty()) {
        const String child = p_path.path_join(entry);
        const Error removed = dir->current_is_dir()
            ? remove_recursive(child)
            : DirAccess::remove_absolute(child);
        if (removed != OK) {
            dir->list_dir_end();
            return removed;
        }
        entry = dir->get_next();
    }
    dir->list_dir_end();
    return DirAccess::remove_absolute(p_path);
}

bool FileSystemDatabase::matches_filter(
    const Dictionary &p_record,
    const Dictionary &p_filter
) {
    const Array keys = p_filter.keys();
    for (int at = 0; at < keys.size(); at++) {
        if (!p_record.has(keys[at])
            || p_record[keys[at]] != p_filter[keys[at]]) {
            return false;
        }
    }
    return true;
}

} // namespace netw
