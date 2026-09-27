#include "netw/persist/file_store.hpp"

#include "godot/file_access.hpp"
#include "godot/file_system.hpp"
#include "godot/utility.hpp"
#include "netw/persist/envelope.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *RECORD_EXTENSION = ".netwrec";
const char *STAGING_EXTENSION = ".netwtmp";

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

const uint8_t MAGIC[] = {'N', 'E', 'T', 'W', 'R', 'E', 'C', '1'};
constexpr int MAGIC_LENGTH = 8;

String escaped(const String &p_text) {
    return p_text.uri_encode().replace(".", "%2E");
}

String kind_folder(int p_kind) {
    return String::num_int64(p_kind);
}

Dictionary decoded(const PackedByteArray &p_bytes) {
    if (p_bytes.size() < MAGIC_LENGTH) {
        return Dictionary();
    }
    for (int at = 0; at < MAGIC_LENGTH; ++at) {
        if (uint8_t(p_bytes[at]) != MAGIC[at]) {
            return Dictionary();
        }
    }
    PackedByteArray body;
    for (int at = MAGIC_LENGTH; at < p_bytes.size(); ++at) {
        body.push_back(p_bytes[at]);
    }
    const Variant held = gd::bytes_to_var(body);
    if (held.get_type() != Variant::DICTIONARY) {
        return Dictionary();
    }
    return Dictionary(held);
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

FileStore::FileStore(const String &p_root) : root(p_root) {
}

const String &FileStore::root_path() const {
    return root;
}

String FileStore::slot_path(const String &p_slot) const {
    return root.path_join(escaped(p_slot));
}

String FileStore::path_of(
    const String &p_slot,
    const Dictionary &p_address
) const {
    return slot_path(p_slot)
        .path_join(kind_folder(int(p_address.get(KEY_KIND, 0))))
        .path_join(escaped(String(p_address.get(KEY_SCHEMA, ""))))
        .path_join(
            escaped(String(p_address.get(KEY_KEY, ""))) + RECORD_EXTENSION
        );
}

String FileStore::staging_path_of(
    const String &p_slot,
    const Dictionary &p_address
) const {
    return slot_path(p_slot)
        .path_join(kind_folder(int(p_address.get(KEY_KIND, 0))))
        .path_join(escaped(String(p_address.get(KEY_SCHEMA, ""))))
        .path_join(
            escaped(String(p_address.get(KEY_KEY, ""))) + STAGING_EXTENSION
        );
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
    const String path = path_of(p_slot, p_address);
    if (!gd::file_exists(path)) {
        return OK;
    }
    Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
    if (file.is_null()) {
        return ERR_FILE_CANT_OPEN;
    }
    const PackedByteArray bytes = file->get_buffer(int64_t(file->get_length()));
    file->close();
    r_found = true;
    r_envelope = decoded(bytes);
    return OK;
}

Error FileStore::replace(
    const String &p_slot,
    const Dictionary &p_address,
    const Dictionary &p_envelope
) {
    const String path = path_of(p_slot, p_address);
    const String folder = path.get_base_dir();
    if (!DirAccess::dir_exists_absolute(folder)) {
        const Error made = DirAccess::make_dir_recursive_absolute(folder);
        if (made != OK) {
            return made;
        }
    }

    const String staging = staging_path_of(p_slot, p_address);
    Ref<FileAccess> file = FileAccess::open(staging, FileAccess::WRITE);
    if (file.is_null()) {
        return ERR_FILE_CANT_WRITE;
    }
    PackedByteArray bytes;
    for (int at = 0; at < MAGIC_LENGTH; ++at) {
        bytes.push_back(MAGIC[at]);
    }
    bytes.append_array(gd::var_to_bytes(p_envelope));
    file->store_buffer(bytes);
    file->flush();
    file->close();
    file = Ref<FileAccess>();

    const Error moved = DirAccess::rename_absolute(staging, path);
    if (moved != OK) {
        DirAccess::remove_absolute(staging);
    }
    return moved;
}

Error FileStore::erase(const String &p_slot, const Dictionary &p_address) {
    const String path = path_of(p_slot, p_address);
    if (!gd::file_exists(path)) {
        return OK;
    }
    return DirAccess::remove_absolute(path);
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

    const String suffix(RECORD_EXTENSION);
    const PackedStringArray files = DirAccess::get_files_at(folder);
    PackedStringArray stems;
    for (int at = 0; at < files.size(); ++at) {
        const String name = files[at];
        if (!name.ends_with(suffix)) {
            continue;
        }
        stems.push_back(name.substr(0, name.length() - suffix.length()));
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
    const String &p_slot
) {
    Ref<FileConnection> made;
    made.instantiate();
    made->root = p_root;
    made->slot = p_slot;
    FileStore(p_root).open_slot(p_slot);
    return made;
}

FileStore FileConnection::store() const {
    return FileStore(root);
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
