#include "netw/persist/file_store.hpp"

#include "godot/file_access.hpp"
#include "godot/file_system.hpp"
#include "netw/persist/envelope.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *KEY_ERROR = "error";
const char *KEY_DETAIL = "detail";
const char *KEY_FOUND = "found";
const char *KEY_ENVELOPE = "envelope";
const char *KEY_RECORDS = "records";
const char *KEY_CURSOR = "cursor";
const char *KEY_ERRORS = "errors";
const char *KEY_UNCERTAIN = "uncertain";
const char *KEY_KIND = "kind";
const char *KEY_SCHEMA = "schema_name";
const char *KEY_KEY = "key";
const char *KEY_ADDRESS = "address";

String escaped(const String &p_text) {
    return p_text.uri_encode().replace(".", "%2E");
}

String kind_folder(int p_kind) {
    return String::num_int64(p_kind);
}

bool remove_tree(const String &p_path) {
    if (!DirAccess::dir_exists_absolute(p_path)) {
        return false;
    }
    const PackedStringArray folders = DirAccess::get_directories_at(p_path);
    for (int at = 0; at < folders.size(); ++at) {
        remove_tree(p_path.path_join(folders[at]));
    }
    const PackedStringArray files = DirAccess::get_files_at(p_path);
    for (int at = 0; at < files.size(); ++at) {
        DirAccess::remove_absolute(p_path.path_join(files[at]));
    }
    return DirAccess::remove_absolute(p_path) == OK;
}

} // namespace

FileStore::FileStore(const String &p_root, RecordFormat p_format)
    : root(p_root), format(p_format) {
}

const String &FileStore::root_path() const {
    return root;
}

String FileStore::slot_path(const String &p_slot) const {
    return root.path_join(escaped(p_slot));
}

String FileStore::path_in(
    const String &p_slot,
    const Dictionary &p_address,
    RecordFormat p_format
) const {
    return slot_path(p_slot)
        .path_join(kind_folder(int(p_address.get(KEY_KIND, 0))))
        .path_join(escaped(String(p_address.get(KEY_SCHEMA, ""))))
        .path_join(
            escaped(String(p_address.get(KEY_KEY, ""))) + String(".")
            + extension_of(p_format)
        );
}

String FileStore::path_of(
    const String &p_slot,
    const Dictionary &p_address
) const {
    return path_in(p_slot, p_address, format);
}

String FileStore::staging_path_of(
    const String &p_slot,
    const Dictionary &p_address
) const {
    return staging_path_for(path_of(p_slot, p_address));
}

bool FileStore::has_slot(const String &p_slot) const {
    return DirAccess::dir_exists_absolute(slot_path(p_slot));
}

Error FileStore::open_slot(const String &p_slot) {
    const String path = slot_path(p_slot);
    if (DirAccess::dir_exists_absolute(path)) {
        return OK;
    }
    return DirAccess::make_dir_recursive_absolute(path);
}

bool FileStore::erase_slot(const String &p_slot) {
    return remove_tree(slot_path(p_slot));
}

PackedStringArray FileStore::slot_names() const {
    PackedStringArray out;
    if (!DirAccess::dir_exists_absolute(root)) {
        return out;
    }
    const PackedStringArray folders = DirAccess::get_directories_at(root);
    for (int at = 0; at < folders.size(); ++at) {
        out.push_back(String(folders[at]).uri_decode());
    }
    out.sort();
    return out;
}

Error FileStore::read(
    const String &p_slot,
    const Dictionary &p_address,
    Dictionary &r_envelope,
    bool &r_found
) const {
    r_found = false;
    String path = path_of(p_slot, p_address);
    if (!gd::file_exists(path)) {
        path = path_in(p_slot, p_address, other_format(format));
        if (!gd::file_exists(path)) {
            return OK;
        }
    }
    const Error answered = read_record_file(path, r_envelope);
    if (answered != OK) {
        return answered;
    }
    r_found = true;
    return OK;
}

Error FileStore::replace(
    const String &p_slot,
    const Dictionary &p_address,
    const Dictionary &p_envelope
) {
    const Error written
        = write_record_file(path_of(p_slot, p_address), p_envelope);
    if (written != OK) {
        return written;
    }
    const String stale = path_in(p_slot, p_address, other_format(format));
    if (gd::file_exists(stale)) {
        DirAccess::remove_absolute(stale);
    }
    return OK;
}

Error FileStore::erase(const String &p_slot, const Dictionary &p_address) {
    const RecordFormat formats[] = {format, other_format(format)};
    for (RecordFormat each : formats) {
        const String path = path_in(p_slot, p_address, each);
        if (!gd::file_exists(path)) {
            continue;
        }
        const Error removed = DirAccess::remove_absolute(path);
        if (removed != OK) {
            return removed;
        }
    }
    return OK;
}

Array FileStore::page(
    const String &p_slot,
    const String &p_schema_name,
    int p_kind,
    const String &p_cursor,
    int p_limit,
    String &r_next
) const {
    r_next = String();
    Array out;
    const String folder = slot_path(p_slot)
                              .path_join(kind_folder(p_kind))
                              .path_join(escaped(p_schema_name));
    if (!DirAccess::dir_exists_absolute(folder)) {
        return out;
    }

    const PackedStringArray files = DirAccess::get_files_at(folder);
    PackedStringArray stems;
    for (int at = 0; at < files.size(); ++at) {
        const String name = files[at];
        RecordFormat named = RecordFormat::BINARY;
        if (!format_of_path(name, named)) {
            continue;
        }
        const String stem = name.get_basename();
        if (!stems.has(stem)) {
            stems.push_back(stem);
        }
    }
    stems.sort();

    const String prefix
        = kind_folder(p_kind) + "/" + escaped(p_schema_name) + "/";
    int taken = 0;
    String last;
    for (int at = 0; at < stems.size(); ++at) {
        const String address = prefix + stems[at];
        if (!p_cursor.is_empty() && address <= p_cursor) {
            continue;
        }
        if (taken >= p_limit) {
            r_next = last;
            return out;
        }
        const String key = String(stems[at]).uri_decode();
        Dictionary envelope;
        bool found = false;
        read(
            p_slot,
            address_of(Kind(p_kind), StringName(p_schema_name), key),
            envelope,
            found
        );
        Dictionary row;
        row[KEY_KEY] = key;
        row[KEY_ENVELOPE] = envelope;
        out.push_back(row);
        taken += 1;
        last = address;
    }
    return out;
}

Ref<FileConnection> FileConnection::opened(
    const String &p_root,
    const String &p_slot,
    RecordFormat p_format
) {
    Ref<FileConnection> made;
    made.instantiate();
    made->root = p_root;
    made->slot = p_slot;
    made->format = p_format;
    FileStore(p_root).open_slot(p_slot);
    return made;
}

FileStore FileConnection::store() const {
    return FileStore(root, format);
}

const String &FileConnection::slot_name() const {
    return slot;
}

Ref<NetwPromise> FileConnection::read(const Dictionary &p_address) {
    Dictionary envelope;
    bool found = false;
    const Error code = store().read(slot, p_address, envelope, found);
    Dictionary reply;
    reply[KEY_ERROR] = int(code);
    reply[KEY_DETAIL] = code == OK ? String()
                                   : vformat(
                                         "the file store could not read '%s'",
                                         store().path_of(slot, p_address)
                                     );
    reply[KEY_FOUND] = found;
    if (found) {
        reply[KEY_ENVELOPE] = envelope;
    }
    return NetwPromise::resolved(reply);
}

Ref<NetwPromise> FileConnection::scan(const Dictionary &p_request) {
    String next;
    const Array rows = store().page(
        slot,
        String(p_request.get(KEY_SCHEMA, "")),
        int(p_request.get(KEY_KIND, 0)),
        String(p_request.get(KEY_CURSOR, "")),
        int(p_request.get("limit", 100)),
        next
    );
    Dictionary reply;
    reply[KEY_ERROR] = int(OK);
    reply[KEY_DETAIL] = String();
    reply[KEY_RECORDS] = rows;
    reply[KEY_CURSOR] = next;
    return NetwPromise::resolved(reply);
}

Ref<NetwPromise> FileConnection::write_batch(const Array &p_operations) {
    FileStore held = store();
    PackedInt32Array errors;
    PackedByteArray uncertain;
    for (int at = 0; at < p_operations.size(); ++at) {
        const Dictionary operation = p_operations[at];
        const Dictionary address = operation.get(KEY_ADDRESS, Dictionary());
        const String kind = String(operation.get(KEY_KIND, ""));
        if (kind == "erase") {
            errors.push_back(int(held.erase(slot, address)));
        } else if (kind == "replace") {
            errors.push_back(
                int(held.replace(
                    slot,
                    address,
                    operation.get(KEY_ENVELOPE, Dictionary())
                ))
            );
        } else {
            errors.push_back(int(ERR_INVALID_DATA));
        }
        uncertain.push_back(0);
    }
    Dictionary reply;
    reply[KEY_ERROR] = int(OK);
    reply[KEY_DETAIL] = String();
    reply[KEY_ERRORS] = errors;
    reply[KEY_UNCERTAIN] = uncertain;
    return NetwPromise::resolved(reply);
}

Ref<NetwPromise> FileConnection::close() {
    return NetwPromise::resolved(OK);
}

} // namespace netw::persist
