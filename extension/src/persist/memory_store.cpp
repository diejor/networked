#include "netw/persist/memory_store.hpp"

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
const char *KEY_ADDRESS = "address";

HashMap<String, MemoryStore> &book() {
    static HashMap<String, MemoryStore> rows;
    return rows;
}

} // namespace

MemoryStore &store_named(const String &p_root) {
    if (!book().has(p_root)) {
        book().insert(p_root, MemoryStore());
    }
    return book()[p_root];
}

void forget_stores() {
    book().clear();
}

bool MemoryStore::has_slot(const String &p_slot) const {
    return slots.has(p_slot);
}

void MemoryStore::open_slot(const String &p_slot) {
    if (!slots.has(p_slot)) {
        slots.insert(p_slot, HashMap<String, Dictionary>());
    }
}

bool MemoryStore::erase_slot(const String &p_slot) {
    return slots.erase(p_slot);
}

PackedStringArray MemoryStore::slot_names() const {
    PackedStringArray out;
    for (const KeyValue<String, HashMap<String, Dictionary>> &row : slots) {
        out.push_back(row.key);
    }
    out.sort();
    return out;
}

bool MemoryStore::read(
    const String &p_slot,
    const String &p_address,
    Dictionary &r_envelope
) const {
    const HashMap<String, HashMap<String, Dictionary>>::ConstIterator found
        = slots.find(p_slot);
    if (found == slots.end()) {
        return false;
    }
    const HashMap<String, Dictionary>::ConstIterator row
        = found->value.find(p_address);
    if (row == found->value.end()) {
        return false;
    }
    r_envelope = row->value.duplicate(true);
    return true;
}

void MemoryStore::replace(
    const String &p_slot,
    const String &p_address,
    const Dictionary &p_envelope
) {
    open_slot(p_slot);
    slots[p_slot][p_address] = p_envelope.duplicate(true);
}

void MemoryStore::erase(const String &p_slot, const String &p_address) {
    if (slots.has(p_slot)) {
        slots[p_slot].erase(p_address);
    }
}

Array MemoryStore::page(
    const String &p_slot,
    const String &p_schema_name,
    int p_kind,
    const String &p_cursor,
    int p_limit,
    String &r_next
) const {
    r_next = String();
    Array out;
    const HashMap<String, HashMap<String, Dictionary>>::ConstIterator found
        = slots.find(p_slot);
    if (found == slots.end()) {
        return out;
    }
    PackedStringArray addresses;
    for (const KeyValue<String, Dictionary> &row : found->value) {
        addresses.push_back(row.key);
    }
    addresses.sort();

    const String prefix
        = String::num_int64(p_kind) + "/" + p_schema_name.uri_encode() + "/";
    int taken = 0;
    for (int at = 0; at < addresses.size(); ++at) {
        const String address = addresses[at];
        if (!address.begins_with(prefix)) {
            continue;
        }
        if (!p_cursor.is_empty() && address <= p_cursor) {
            continue;
        }
        if (taken >= p_limit) {
            r_next = addresses[at - 1];
            return out;
        }
        Dictionary row;
        row["key"] = address.substr(prefix.length()).uri_decode();
        row[KEY_ENVELOPE] = found->value[address].duplicate(true);
        out.push_back(row);
        taken += 1;
    }
    return out;
}

void MemoryStore::clear() {
    slots.clear();
}

Ref<MemoryConnection> MemoryConnection::opened(
    const String &p_root,
    const String &p_slot
) {
    Ref<MemoryConnection> made;
    made.instantiate();
    made->root = p_root;
    made->slot = p_slot;
    store_named(p_root).open_slot(p_slot);
    return made;
}

void MemoryConnection::defer(bool p_value) {
    deferring = p_value;
}

void MemoryConnection::fail_next(Error p_error) {
    next_failure = p_error;
}

void MemoryConnection::doubt_next(Error p_error) {
    doubting = true;
    doubted = p_error;
}

int MemoryConnection::close_count() const {
    return closes;
}

int MemoryConnection::withheld_count() const {
    return int(withheld.size());
}

void MemoryConnection::release() {
    LocalVector<Ref<NetwPromise>> held;
    LocalVector<Variant> values;
    held.reserve(withheld.size());
    values.reserve(withheld_values.size());
    for (uint32_t at = 0; at < withheld.size(); ++at) {
        held.push_back(withheld[at]);
        values.push_back(withheld_values[at]);
    }
    withheld.clear();
    withheld_values.clear();
    for (uint32_t at = 0; at < held.size(); ++at) {
        held[at]->resolve(values[at]);
    }
}

Ref<NetwPromise> MemoryConnection::answer(const Variant &p_value) {
    if (!deferring) {
        return NetwPromise::resolved(p_value);
    }
    Ref<NetwPromise> made;
    made.instantiate();
    withheld.push_back(made);
    withheld_values.push_back(p_value);
    return made;
}

Ref<NetwPromise> MemoryConnection::read(const Dictionary &p_address) {
    if (next_failure != OK) {
        const Error code = next_failure;
        next_failure = OK;
        return NetwPromise::rejected(code, "the memory store was told to fail");
    }
    Dictionary envelope;
    const bool found
        = store_named(root).read(slot, address_text(p_address), envelope);
    Dictionary reply;
    reply[KEY_ERROR] = int(OK);
    reply[KEY_DETAIL] = String();
    reply[KEY_FOUND] = found;
    if (found) {
        reply[KEY_ENVELOPE] = envelope;
    }
    return answer(reply);
}

Ref<NetwPromise> MemoryConnection::scan(const Dictionary &p_request) {
    if (next_failure != OK) {
        const Error code = next_failure;
        next_failure = OK;
        return NetwPromise::rejected(code, "the memory store was told to fail");
    }
    String next;
    const Array rows = store_named(root).page(
        slot,
        String(p_request.get("schema_name", "")),
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
    return answer(reply);
}

Ref<NetwPromise> MemoryConnection::write_batch(const Array &p_operations) {
    if (next_failure != OK) {
        const Error code = next_failure;
        next_failure = OK;
        return NetwPromise::rejected(code, "the memory store was told to fail");
    }
    PackedInt32Array errors;
    PackedByteArray uncertain;
    for (int at = 0; at < p_operations.size(); ++at) {
        const Dictionary operation = p_operations[at];
        const String address
            = address_text(operation.get(KEY_ADDRESS, Dictionary()));
        const String kind = String(operation.get(KEY_KIND, ""));
        if (kind == "erase") {
            store_named(root).erase(slot, address);
            errors.push_back(int(OK));
        } else if (kind == "replace") {
            store_named(root).replace(
                slot,
                address,
                operation.get(KEY_ENVELOPE, Dictionary())
            );
            errors.push_back(int(OK));
        } else {
            errors.push_back(int(ERR_INVALID_DATA));
        }
        uncertain.push_back(0);
    }
    if (doubting) {
        doubting = false;
        errors.fill(int(doubted));
        uncertain.fill(1);
    }
    Dictionary reply;
    reply[KEY_ERROR] = int(OK);
    reply[KEY_DETAIL] = String();
    reply[KEY_ERRORS] = errors;
    reply[KEY_UNCERTAIN] = uncertain;
    return answer(reply);
}

Ref<NetwPromise> MemoryConnection::close() {
    closes += 1;
    return NetwPromise::resolved(OK);
}

} // namespace netw::persist
