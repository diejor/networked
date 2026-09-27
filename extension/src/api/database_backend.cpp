#include "netw/api/database_backend.hpp"

#include "godot/class_db.hpp"
#include "netw/persist/file_store.hpp"
#include "netw/persist/memory_store.hpp"

using namespace godot;

namespace netw {

namespace {

Ref<NetwPromise> unimplemented(const char *p_verb) {
    return NetwPromise::rejected(
        ERR_UNAVAILABLE,
        String("this backend implements no ") + p_verb
    );
}

Ref<NetwPromise> answered(
    const Ref<NetwPromise> &p_answer,
    const char *p_verb
) {
    if (p_answer.is_valid()) {
        return p_answer;
    }
    return NetwPromise::rejected(
        ERR_INVALID_DATA,
        String(p_verb) + " answered no promise"
    );
}

} // namespace

Ref<NetwPromise> NetwDatabaseBackend::open_default(
    Object *p_session,
    const StringName &p_slot
) {
    return unimplemented("_open");
}

Ref<NetwPromise> NetwDatabaseBackend::list_slots_default(Object *p_session) {
    return unimplemented("_list_slots");
}

Ref<NetwPromise> NetwDatabaseBackend::delete_slot_default(
    Object *p_session,
    const StringName &p_slot
) {
    return unimplemented("_delete_slot");
}

Ref<NetwPromise> NetwDatabaseBackend::open(
    Object *p_session,
    const StringName &p_slot
) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_open, p_session, p_slot, answer)) {
        return answered(answer, "_open");
    }
    return open_default(p_session, p_slot);
}

Ref<NetwPromise> NetwDatabaseBackend::list_slots(Object *p_session) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_list_slots, p_session, answer)) {
        return answered(answer, "_list_slots");
    }
    return list_slots_default(p_session);
}

Ref<NetwPromise> NetwDatabaseBackend::delete_slot(
    Object *p_session,
    const StringName &p_slot
) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_delete_slot, p_session, p_slot, answer)) {
        return answered(answer, "_delete_slot");
    }
    return delete_slot_default(p_session, p_slot);
}

void NetwDatabaseBackend::_bind_methods() {
    GDVIRTUAL_BIND(_open, "session", "slot");
    GDVIRTUAL_BIND(_list_slots, "session");
    GDVIRTUAL_BIND(_delete_slot, "session", "slot");

    ClassDB::bind_method(
        D_METHOD("open_default", "session", "slot"),
        &NetwDatabaseBackend::open_default
    );
    ClassDB::bind_method(
        D_METHOD("list_slots_default", "session"),
        &NetwDatabaseBackend::list_slots_default
    );
    ClassDB::bind_method(
        D_METHOD("delete_slot_default", "session", "slot"),
        &NetwDatabaseBackend::delete_slot_default
    );
}

void MemoryDatabase::set_store(const StringName &p_store) {
    store = p_store;
}

Ref<NetwPromise> MemoryDatabase::open_default(
    Object *p_session,
    const StringName &p_slot
) {
    return NetwPromise::resolved(
        persist::MemoryConnection::opened(String(store), String(p_slot))
    );
}

Ref<NetwPromise> MemoryDatabase::list_slots_default(Object *p_session) {
    return NetwPromise::resolved(
        persist::store_named(String(store)).slot_names()
    );
}

Ref<NetwPromise> MemoryDatabase::delete_slot_default(
    Object *p_session,
    const StringName &p_slot
) {
    persist::store_named(String(store)).erase_slot(String(p_slot));
    return NetwPromise::resolved(OK);
}

void MemoryDatabase::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_store", "store"),
        &MemoryDatabase::set_store
    );
    ClassDB::bind_method(D_METHOD("get_store"), &MemoryDatabase::get_store);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "store"),
        "set_store",
        "get_store"
    );
}

void FileSystemDatabase::set_root(const String &p_root) {
    root = p_root;
}

Ref<NetwPromise> FileSystemDatabase::open_default(
    Object *p_session,
    const StringName &p_slot
) {
    persist::FileStore store(root);
    const Error made = store.open_slot(String(p_slot));
    if (made != OK) {
        return NetwPromise::rejected(
            made,
            vformat("the slot directory '%s' could not be created", root)
        );
    }
    return NetwPromise::resolved(
        persist::FileConnection::opened(root, String(p_slot))
    );
}

Ref<NetwPromise> FileSystemDatabase::list_slots_default(Object *p_session) {
    return NetwPromise::resolved(persist::FileStore(root).slot_names());
}

Ref<NetwPromise> FileSystemDatabase::delete_slot_default(
    Object *p_session,
    const StringName &p_slot
) {
    persist::FileStore store(root);
    if (!store.has_slot(String(p_slot))) {
        return NetwPromise::resolved(OK);
    }
    store.erase_slot(String(p_slot));
    return NetwPromise::resolved(store.has_slot(String(p_slot)) ? FAILED : OK);
}

void FileSystemDatabase::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_root", "root"),
        &FileSystemDatabase::set_root
    );
    ClassDB::bind_method(D_METHOD("get_root"), &FileSystemDatabase::get_root);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING, "root", PROPERTY_HINT_DIR),
        "set_root",
        "get_root"
    );
}

} // namespace netw
