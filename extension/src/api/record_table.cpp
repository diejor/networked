#include "netw/api/record_table.hpp"

#include "godot/class_db.hpp"
#include "netw/api/database.hpp"
#include "netw/api/transaction.hpp"

using namespace godot;

namespace netw {

void NetwRecordTable::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("get_columns"),
        &NetwRecordTable::get_columns
    );
    ClassDB::bind_method(D_METHOD("fetch", "id"), &NetwRecordTable::fetch);
    ClassDB::bind_method(
        D_METHOD("fetch_all", "filter"),
        &NetwRecordTable::fetch_all,
        DEFVAL(Dictionary())
    );
    ClassDB::bind_method(
        D_METHOD("put", "id", "record"),
        &NetwRecordTable::put
    );
    ClassDB::bind_method(D_METHOD("erase", "id"), &NetwRecordTable::erase);
}

Ref<NetwRecordTable> NetwRecordTable::open(
    const Ref<NetwDatabase> &p_database,
    const StringName &p_table,
    const Ref<Script> &p_script
) {
    Ref<NetwRecordTable> opened;
    opened.instantiate();
    opened->db = p_database;
    opened->table = p_table;
    opened->record_script = p_script;
    return opened;
}

Ref<NetwRecord> NetwRecordTable::mint() const {
    if (record_script.is_valid()) {
        const Ref<NetwRecord> made
            = Ref<NetwRecord>(record_script->call(StringName("new")));
        if (made.is_valid()) {
            return made;
        }
    }
    Ref<DictionaryRecord> plain;
    plain.instantiate();
    return plain;
}

TypedArray<StringName> NetwRecordTable::get_columns() const {
    if (db.is_null()) {
        return TypedArray<StringName>();
    }
    return db->get_registered_columns(table);
}

Ref<NetwPromise> NetwRecordTable::fetch(const StringName &p_id) {
    if (db.is_null()) {
        return NetwPromise::resolved(Variant());
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    const Ref<NetwPromise> stored = db->find(table, p_id);
    stored->catch_error(
        callable_mp(this, &NetwRecordTable::settle_missing).bind(answer)
    );
    stored->then(
        callable_mp(this, &NetwRecordTable::settle_fetched).bind(answer)
    );
    return answer;
}

void NetwRecordTable::settle_fetched(
    const Dictionary &p_record,
    const Ref<NetwPromise> &p_answer
) {
    if (p_record.is_empty()) {
        p_answer->resolve(Variant());
        return;
    }
    const Ref<NetwRecord> loaded = mint();
    loaded->from_dict(p_record);
    p_answer->resolve(loaded);
}

void NetwRecordTable::settle_missing(
    int,
    const String &,
    const Ref<NetwPromise> &p_answer
) {
    p_answer->resolve(Variant());
}

Ref<NetwPromise> NetwRecordTable::fetch_all(const Dictionary &p_filter) {
    if (db.is_null()) {
        return NetwPromise::resolved(TypedArray<NetwRecord>());
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    const Ref<NetwPromise> stored = db->find_all(table, p_filter);
    stored->catch_error(
        callable_mp(this, &NetwRecordTable::settle_none).bind(answer)
    );
    stored->then(
        callable_mp(this, &NetwRecordTable::settle_fetched_all).bind(answer)
    );
    return answer;
}

void NetwRecordTable::settle_fetched_all(
    const Array &p_rows,
    const Ref<NetwPromise> &p_answer
) {
    TypedArray<NetwRecord> records;
    for (int at = 0; at < p_rows.size(); at++) {
        const Ref<NetwRecord> loaded = mint();
        loaded->from_dict(p_rows[at]);
        records.push_back(loaded);
    }
    p_answer->resolve(records);
}

void NetwRecordTable::settle_none(
    int,
    const String &,
    const Ref<NetwPromise> &p_answer
) {
    p_answer->resolve(TypedArray<NetwRecord>());
}

Ref<NetwPromise> NetwRecordTable::put(
    const StringName &p_id,
    const Ref<NetwRecord> &p_record
) {
    if (db.is_null() || p_record.is_null()) {
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    return db->transaction(
        callable_mp(this, &NetwRecordTable::queue_put).bind(p_id, p_record)
    );
}

void NetwRecordTable::queue_put(
    const Ref<NetwTransaction> &p_transaction,
    const StringName &p_id,
    const Ref<NetwRecord> &p_record
) {
    if (p_transaction.is_null()) {
        return;
    }
    p_transaction->queue_upsert(table, p_id, p_record->to_dict());
}

Ref<NetwPromise> NetwRecordTable::erase(const StringName &p_id) {
    if (db.is_null()) {
        return NetwPromise::resolved(int64_t(ERR_UNCONFIGURED));
    }
    return db->erase(table, p_id);
}

} // namespace netw
