#include "netw/persist/database.hpp"

#include "netw/log.hpp"
#include "netw/persist/envelope.hpp"
#include "netw/schema_core.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *KEY_KIND = "kind";
const char *KEY_ADDRESS = "address";
const char *KEY_ENVELOPE = "envelope";
const char *KEY_ERROR = "error";
const char *KEY_DETAIL = "detail";
const char *KEY_FOUND = "found";
const char *KEY_RECORDS = "records";
const char *KEY_CURSOR = "cursor";
const char *KEY_ERRORS = "errors";
const char *KEY_UNCERTAIN = "uncertain";

const char *OP_REPLACE = "replace";
const char *OP_ERASE = "erase";

} // namespace

Databases::Databases() {
    instances.set_description("netw::persist database");
}

Databases::~Databases() {
    clear();
}

void Databases::bind(
    SchemaCore *p_schemas,
    const Callable &p_settled,
    const Callable &p_connected
) {
    schemas = p_schemas;
    on_settled = p_settled;
    on_connected = p_connected;
}

Databases::Instance *Databases::at(const RID &p_database) {
    return instances.get_or_null(p_database);
}

const Databases::Instance *Databases::at(const RID &p_database) const {
    return instances.get_or_null(p_database);
}

RID Databases::create(const StringName &p_name) {
    if (p_name == StringName()) {
        return RID();
    }
    const HashMap<StringName, RID>::ConstIterator found = by_name.find(p_name);
    if (found != by_name.end()) {
        return found->value;
    }
    Instance *made = memnew(Instance);
    made->name = p_name;
    const RID handle = instances.make_rid(made);
    by_name[p_name] = handle;
    return handle;
}

RID Databases::find(const StringName &p_name) const {
    const HashMap<StringName, RID>::ConstIterator found = by_name.find(p_name);
    return found != by_name.end() ? found->value : RID();
}

bool Databases::is_valid(const RID &p_database) const {
    return instances.owns(p_database);
}

void Databases::clear() {
    LocalVector<RID> held;
    held.resize(instances.get_rid_count());
    instances.fill_owned_buffer(held.ptr());
    for (const RID &handle : held) {
        Instance *instance = instances.get_or_null(handle);
        if (instance != nullptr) {
            fail_queue(instance, ERR_UNAVAILABLE, "the session ended");
            instances.free(handle);
            memdelete(instance);
        }
    }
    by_name.clear();
}

StringName Databases::name_of(const RID &p_database) const {
    const Instance *instance = at(p_database);
    return instance != nullptr ? instance->name : StringName();
}

StringName Databases::slot_of(const RID &p_database) const {
    const Instance *instance = at(p_database);
    if (instance == nullptr || instance->state != State::OPEN) {
        return StringName();
    }
    return instance->slot;
}

State Databases::state_of(const RID &p_database) const {
    const Instance *instance = at(p_database);
    return instance != nullptr ? instance->state : State::CLOSED;
}

int64_t Databases::generation_of(const RID &p_database) const {
    const Instance *instance = at(p_database);
    return instance != nullptr ? instance->generation : 0;
}

Ref<NetwDatabaseConnection> Databases::connection_of(
    const RID &p_database
) const {
    const Instance *instance = at(p_database);
    return instance != nullptr ? instance->connection
                               : Ref<NetwDatabaseConnection>();
}

Ref<NetwPromise> Databases::open(
    const RID &p_database,
    const StringName &p_slot,
    const Ref<NetwPromise> &p_connecting
) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this database handle names no database"
        );
    }
    if (instance->state == State::FAULTED) {
        return NetwPromise::rejected(
            ERR_CANT_ACQUIRE_RESOURCE,
            vformat(
                "database '%s' is faulted and its storage is unavailable "
                "until the backend proves its outstanding work has stopped",
                String(instance->name)
            )
        );
    }
    if (instance->state == State::OPENING) {
        if (instance->opening_slot == p_slot) {
            return instance->opening;
        }
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "database '%s' is opening slot '%s' and cannot open '%s' too",
                String(instance->name),
                String(instance->opening_slot),
                String(p_slot)
            )
        );
    }
    if (instance->state == State::OPEN) {
        if (instance->slot == p_slot) {
            return NetwPromise::resolved(OK);
        }
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "database '%s' holds slot '%s'. Close it before opening '%s'",
                String(instance->name),
                String(instance->slot),
                String(p_slot)
            )
        );
    }
    if (instance->state == State::CLOSING) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "database '%s' is closing and cannot open until it has",
                String(instance->name)
            )
        );
    }
    if (p_connecting.is_null()) {
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            vformat(
                "database '%s' has no backend to open",
                String(instance->name)
            )
        );
    }

    instance->state = State::OPENING;
    instance->opening_slot = p_slot;
    instance->connecting = p_connecting;
    Ref<NetwPromise> answer;
    answer.instantiate();
    instance->opening = answer;
    if (on_connected.is_valid()) {
        p_connecting->when_settled(
            on_connected.bind(p_database, instance->generation)
        );
    }
    return answer;
}

void Databases::connected(const RID &p_database, int64_t p_generation) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return;
    }
    if (instance->generation != p_generation
        || instance->state != State::OPENING) {
        discard(instance, p_generation);
        return;
    }
    const Ref<NetwPromise> connecting = instance->connecting;
    instance->connecting.unref();
    const Ref<NetwPromise> opening = instance->opening;
    instance->opening.unref();

    if (connecting.is_null() || connecting->get_is_failed()) {
        instance->state = State::CLOSED;
        instance->opening_slot = StringName();
        if (opening.is_valid()) {
            opening->reject(
                connecting.is_valid() ? connecting->get_code() : FAILED,
                connecting.is_valid() ? connecting->get_detail()
                                      : "the backend opened no connection"
            );
        }
        return;
    }

    const Ref<NetwDatabaseConnection> made = connecting->get_result();
    if (made.is_null()) {
        instance->state = State::CLOSED;
        instance->opening_slot = StringName();
        if (opening.is_valid()) {
            opening->reject(
                ERR_CANT_CREATE,
                vformat(
                    "the backend of database '%s' resolved no connection",
                    String(instance->name)
                )
            );
        }
        return;
    }

    instance->connection = made;
    instance->slot = instance->opening_slot;
    instance->opening_slot = StringName();
    instance->state = State::OPEN;
    if (opening.is_valid()) {
        opening->resolve(OK);
    }
    pump(p_database);
}

Ref<NetwPromise> Databases::close(const RID &p_database) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this database handle names no database"
        );
    }
    if (instance->state == State::CLOSED) {
        return NetwPromise::resolved(OK);
    }
    if (instance->state == State::CLOSING) {
        return instance->closing;
    }
    const StringName name = instance->name;
    const Ref<NetwPromise> overtaken = instance->opening;
    if (instance->state == State::OPENING) {
        instance->opening.unref();
        instance->opening_slot = StringName();
        if (instance->connecting.is_valid()) {
            instance->abandoned[instance->generation] = instance->connecting;
            instance->connecting.unref();
        }
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    instance->closing = answer;
    instance->state = State::CLOSING;
    if (instance->queue.is_empty()) {
        release_waiters(instance);
    }
    if (overtaken.is_valid()) {
        overtaken->reject(
            ERR_UNAVAILABLE,
            vformat(
                "database '%s' was closed before it finished opening",
                String(name)
            )
        );
    }
    return answer;
}

void Databases::discard(Instance *p_instance, int64_t p_generation) {
    const HashMap<int64_t, Ref<NetwPromise>>::Iterator found
        = p_instance->abandoned.find(p_generation);
    if (found == p_instance->abandoned.end()) {
        return;
    }
    const Ref<NetwPromise> connecting = found->value;
    p_instance->abandoned.remove(found);
    if (connecting.is_null() || !connecting->get_is_completed()) {
        return;
    }
    const Ref<NetwDatabaseConnection> late = connecting->get_result();
    if (late.is_valid()) {
        late->close();
    }
}

Ref<NetwPromise> Databases::flush(const RID &p_database) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this database handle names no database"
        );
    }
    if (instance->queue.is_empty()) {
        return NetwPromise::resolved(OK);
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    instance->flush_waiters.push_back(answer);
    return answer;
}

void Databases::release_waiters(Instance *p_instance) {
    if (!p_instance->queue.is_empty()) {
        return;
    }
    const LocalVector<Ref<NetwPromise>> waiting(p_instance->flush_waiters);
    p_instance->flush_waiters.clear();
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        if (waiting[at].is_valid()) {
            waiting[at]->resolve(OK);
        }
    }
    if (p_instance->state != State::CLOSING) {
        return;
    }
    const Ref<NetwPromise> closing = p_instance->closing;
    p_instance->closing.unref();
    const Ref<NetwDatabaseConnection> connection = p_instance->connection;
    p_instance->connection.unref();
    p_instance->state = State::CLOSED;
    p_instance->slot = StringName();
    p_instance->generation += 1;
    p_instance->held.clear();
    if (connection.is_valid()) {
        connection->close();
    }
    if (closing.is_valid()) {
        closing->resolve(OK);
    }
}

void Databases::fail_queue(
    Instance *p_instance,
    Error p_error,
    const String &p_detail
) {
    const LocalVector<Pending> waiting(p_instance->queue);
    p_instance->queue.clear();
    p_instance->held.clear();
    for (uint32_t at = 0; at < waiting.size(); ++at) {
        if (waiting[at].answer.is_valid()) {
            waiting[at].answer->reject(p_error, p_detail);
        }
    }
    const LocalVector<Ref<NetwPromise>> flushing(p_instance->flush_waiters);
    p_instance->flush_waiters.clear();
    for (uint32_t at = 0; at < flushing.size(); ++at) {
        if (flushing[at].is_valid()) {
            flushing[at]->reject(p_error, p_detail);
        }
    }
}

Ref<NetwPromise> Databases::admit(const RID &p_database, Pending &p_pending) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this database handle names no database"
        );
    }
    if (instance->state != State::OPEN) {
        return NetwPromise::rejected(
            ERR_UNCONFIGURED,
            vformat(
                "database '%s' is not open, so it admits no work. Await "
                "open() before starting play",
                String(instance->name)
            )
        );
    }
    if (int(instance->queue.size()) >= queue_limit) {
        return NetwPromise::rejected(
            ERR_BUSY,
            vformat(
                "database '%s' already holds %d unsettled operations",
                String(instance->name),
                queue_limit
            )
        );
    }
    Ref<NetwPromise> answer;
    answer.instantiate();
    p_pending.answer = answer;
    p_pending.sequence = instance->next_sequence++;
    p_pending.generation = instance->generation;
    instance->queue.push_back(p_pending);
    pump(p_database);
    return answer;
}

void Databases::pump(const RID &p_database) {
    Instance *instance = at(p_database);
    if (instance == nullptr || instance->connection.is_null()) {
        return;
    }
    for (uint32_t row = 0; row < instance->queue.size(); ++row) {
        Pending &pending = instance->queue[row];
        if (pending.issued) {
            continue;
        }
        bool blocked = false;
        for (uint32_t lock = 0; lock < pending.locks.size(); ++lock) {
            if (instance->held.has(pending.locks[lock])) {
                blocked = true;
                break;
            }
        }
        if (blocked) {
            continue;
        }
        for (uint32_t lock = 0; lock < pending.locks.size(); ++lock) {
            instance->held[pending.locks[lock]] = pending.sequence;
        }
        issue(p_database, int(row));
        instance = at(p_database);
        if (instance == nullptr) {
            return;
        }
    }
}

void Databases::issue(const RID &p_database, int p_index) {
    Instance *instance = at(p_database);
    if (instance == nullptr || p_index >= int(instance->queue.size())) {
        return;
    }
    Pending &pending = instance->queue[p_index];
    pending.issued = true;
    const Ref<NetwDatabaseConnection> connection = instance->connection;
    const int64_t sequence = pending.sequence;

    Ref<NetwPromise> outstanding;
    switch (pending.op) {
        case Op::READ:
        case Op::SNAPSHOT:
            outstanding = connection->read(pending.address);
            break;
        case Op::PATCH:
            if (pending.operations.is_empty()) {
                pending.patching = true;
                outstanding = connection->read(pending.address);
            } else {
                outstanding = connection->write_batch(pending.operations);
            }
            break;
        case Op::SCAN:
            outstanding = connection->scan(pending.request);
            break;
        case Op::WRITE:
        case Op::ERASE:
        case Op::BATCH:
            outstanding = connection->write_batch(pending.operations);
            break;
    }
    if (outstanding.is_null()) {
        outstanding = NetwPromise::rejected(
            ERR_INVALID_DATA,
            "the connection answered no promise"
        );
    }
    instance->queue[p_index].outstanding = outstanding;
    if (on_settled.is_valid()) {
        outstanding->when_settled(on_settled.bind(p_database, sequence));
    }
}

void Databases::retire(Instance *p_instance, int p_index) {
    const Pending held = p_instance->queue[p_index];
    for (uint32_t lock = 0; lock < held.locks.size(); ++lock) {
        const HashMap<String, int64_t>::Iterator found
            = p_instance->held.find(held.locks[lock]);
        if (found != p_instance->held.end() && found->value == held.sequence) {
            p_instance->held.remove(found);
        }
    }
    p_instance->queue.remove_at(p_index);
}

void Databases::settled(const RID &p_database, int64_t p_sequence) {
    Instance *instance = at(p_database);
    if (instance == nullptr) {
        return;
    }
    int index = -1;
    for (uint32_t at_row = 0; at_row < instance->queue.size(); ++at_row) {
        if (instance->queue[at_row].sequence == p_sequence) {
            index = int(at_row);
            break;
        }
    }
    if (index < 0) {
        return;
    }
    Pending &pending = instance->queue[index];
    const Ref<NetwPromise> outstanding = pending.outstanding;
    const Ref<NetwPromise> answer = pending.answer;
    const bool stale = pending.generation != instance->generation;

    if (outstanding.is_null()) {
        retire(instance, index);
        pump(p_database);
        return;
    }

    if (stale) {
        retire(instance, index);
        if (answer.is_valid()) {
            answer->reject(
                ERR_UNAVAILABLE,
                "the database closed before this operation settled"
            );
        }
        instance = at(p_database);
        if (instance != nullptr) {
            release_waiters(instance);
            pump(p_database);
        }
        return;
    }

    if (outstanding->get_is_failed()) {
        const Error code = outstanding->get_code();
        const String detail = outstanding->get_detail();
        const Op op = pending.op;
        const StringName id = pending.id;
        const int count = op == Op::BATCH ? pending.operations.size() : 1;
        retire(instance, index);
        if (answer.is_valid()) {
            switch (op) {
                case Op::READ:
                    answer->resolve(database_read_failure(id, code, detail));
                    break;
                case Op::SCAN:
                    answer->resolve(database_page_failure(code, detail));
                    break;
                case Op::SNAPSHOT: {
                    Dictionary refused;
                    refused[KEY_ERROR] = int(code);
                    refused[KEY_DETAIL] = detail;
                    refused[KEY_FOUND] = false;
                    answer->resolve(refused);
                    break;
                }
                case Op::BATCH:
                    answer->resolve(
                        database_batch_result_refused(count, code, detail)
                    );
                    break;
                case Op::WRITE:
                case Op::PATCH:
                case Op::ERASE:
                    answer->resolve(code);
                    break;
            }
        }
        instance = at(p_database);
        if (instance != nullptr) {
            release_waiters(instance);
            pump(p_database);
        }
        return;
    }

    const Dictionary reply = outstanding->get_result();
    if (pending.patching && pending.operations.is_empty()) {
        const Error staged = stage_patch(instance, index, reply);
        if (staged == OK) {
            issue(p_database, index);
            return;
        }
        const StringName id = pending.id;
        retire(instance, index);
        if (answer.is_valid()) {
            answer->resolve(staged);
        }
        instance = at(p_database);
        if (instance != nullptr) {
            release_waiters(instance);
            pump(p_database);
        }
        return;
    }

    const Op op = pending.op;
    const RID schema = pending.schema;
    const StringName id = pending.id;
    const int count = op == Op::BATCH ? pending.operations.size() : 1;
    retire(instance, index);
    if (answer.is_valid()) {
        answer->resolve(unpack(op, schema, id, count, reply));
    }
    instance = at(p_database);
    if (instance != nullptr) {
        release_waiters(instance);
        pump(p_database);
    }
}

Error Databases::stage_patch(
    Instance *p_instance,
    int p_index,
    const Dictionary &p_reply
) {
    Pending &pending = p_instance->queue[p_index];
    const Error reported = Error(int(p_reply.get(KEY_ERROR, int(OK))));
    if (reported != OK) {
        return reported;
    }
    if (!bool(p_reply.get(KEY_FOUND, false))) {
        return ERR_DOES_NOT_EXIST;
    }
    if (schemas == nullptr) {
        return ERR_UNCONFIGURED;
    }
    Dictionary held;
    String detail;
    const Error opened = open_record(
        *schemas,
        pending.schema,
        p_reply.get(KEY_ENVELOPE, Dictionary()),
        held,
        detail
    );
    if (opened != OK) {
        NETW_WARN(
            sys::TABLE,
            "patch read a record it cannot open: %s",
            detail.utf8().get_data()
        );
        return opened;
    }
    const SchemaRecord *schema = schemas->record_of(pending.schema);
    if (schema == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    const Array fields = pending.patch_values.keys();
    for (int at = 0; at < fields.size(); ++at) {
        held[fields[at]] = pending.patch_values[fields[at]];
    }
    const Error checked = validate_row(*schema, held, detail);
    if (checked != OK) {
        NETW_WARN(
            sys::TABLE,
            "patch refuses the row it would have written: %s",
            detail.utf8().get_data()
        );
        return checked;
    }
    Dictionary operation;
    operation[KEY_KIND] = OP_REPLACE;
    operation[KEY_ADDRESS] = pending.address;
    operation[KEY_ENVELOPE]
        = seal_record(*schema, schema->storage_version, held);
    pending.operations.push_back(operation);
    pending.issued = false;
    return OK;
}

Variant Databases::unpack(
    Op p_op,
    const RID &p_schema,
    const StringName &p_id,
    int p_count,
    const Dictionary &p_reply
) {
    const Error reported = Error(int(p_reply.get(KEY_ERROR, int(OK))));
    const String detail = p_reply.get(KEY_DETAIL, "");

    switch (p_op) {
        case Op::READ: {
            if (reported != OK) {
                return database_read_failure(p_id, reported, detail);
            }
            if (!bool(p_reply.get(KEY_FOUND, false))) {
                return database_read_miss(p_id);
            }
            Dictionary values;
            String opened_detail;
            const Error opened = open_record(
                *schemas,
                p_schema,
                p_reply.get(KEY_ENVELOPE, Dictionary()),
                values,
                opened_detail
            );
            if (opened != OK) {
                return database_read_failure(p_id, opened, opened_detail);
            }
            return database_read_hit(p_id, values);
        }
        case Op::SCAN: {
            if (reported != OK) {
                return database_page_failure(reported, detail);
            }
            const Array rows = p_reply.get(KEY_RECORDS, Array());
            Array out;
            for (int at = 0; at < rows.size(); ++at) {
                const Dictionary row = rows[at];
                Dictionary values;
                String opened_detail;
                const Error opened = open_record(
                    *schemas,
                    p_schema,
                    row.get(KEY_ENVELOPE, Dictionary()),
                    values,
                    opened_detail
                );
                if (opened != OK) {
                    return database_page_failure(opened, opened_detail);
                }
                out.push_back(database_read_hit(
                    StringName(String(row.get("key", ""))),
                    values
                ));
            }
            return database_page_of(out, String(p_reply.get(KEY_CURSOR, "")));
        }
        case Op::BATCH: {
            PackedInt32Array errors = p_reply.get(KEY_ERRORS, Variant());
            PackedByteArray uncertain = p_reply.get(KEY_UNCERTAIN, Variant());
            if (errors.size() != p_count) {
                return database_batch_result_refused(
                    p_count,
                    ERR_INVALID_DATA,
                    vformat(
                        "the backend answered %d outcomes for %d operations",
                        errors.size(),
                        p_count
                    )
                );
            }
            if (uncertain.size() != p_count) {
                uncertain.resize(p_count);
                uncertain.fill(0);
            }
            Error worst = reported;
            for (int at = 0; at < errors.size(); ++at) {
                if (errors[at] != OK && worst == OK) {
                    worst = Error(errors[at]);
                }
            }
            return database_batch_result_of(worst, detail, errors, uncertain);
        }
        case Op::SNAPSHOT: {
            Dictionary out;
            out[KEY_ERROR] = int(reported);
            out[KEY_DETAIL] = detail;
            out[KEY_FOUND]
                = reported == OK && bool(p_reply.get(KEY_FOUND, false));
            out[KEY_ENVELOPE] = p_reply.get(KEY_ENVELOPE, Dictionary());
            return out;
        }
        case Op::WRITE:
        case Op::PATCH:
        case Op::ERASE: {
            if (reported != OK) {
                return reported;
            }
            const PackedInt32Array errors = p_reply.get(KEY_ERRORS, Variant());
            if (errors.size() != 1) {
                return ERR_INVALID_DATA;
            }
            const PackedByteArray uncertain
                = p_reply.get(KEY_UNCERTAIN, Variant());
            const bool doubted = uncertain.size() == 1 && uncertain[0] != 0;
            if (errors[0] == OK && doubted) {
                return ERR_UNAVAILABLE;
            }
            return Error(errors[0]);
        }
    }
    return FAILED;
}

const SchemaRecord *Databases::sealed(const RID &p_schema) const {
    if (schemas == nullptr) {
        return nullptr;
    }
    const SchemaRecord *record = schemas->record_of(p_schema);
    return record != nullptr && record->sealed ? record : nullptr;
}

Ref<NetwPromise> Databases::read(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (String(p_id).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a record id may not be empty"
        );
    }
    Pending pending;
    pending.op = Op::READ;
    pending.schema = p_schema;
    pending.id = p_id;
    pending.address = address_of(Kind::RECORD, schema->name, String(p_id));
    pending.locks.push_back(address_text(pending.address));
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::read_snapshot(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_key
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (String(p_key).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a snapshot key may not be empty"
        );
    }
    Pending pending;
    pending.op = Op::SNAPSHOT;
    pending.schema = p_schema;
    pending.id = p_key;
    pending.address = address_of(Kind::SNAPSHOT, schema->name, String(p_key));
    pending.locks.push_back(address_text(pending.address));
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::write(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (String(p_id).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a record id may not be empty"
        );
    }
    String detail;
    const Error checked = validate_row(*schema, p_values, detail);
    if (checked != OK) {
        return NetwPromise::rejected(checked, detail);
    }
    Pending pending;
    pending.op = Op::WRITE;
    pending.schema = p_schema;
    pending.id = p_id;
    pending.address = address_of(Kind::RECORD, schema->name, String(p_id));
    pending.locks.push_back(address_text(pending.address));
    Dictionary operation;
    operation[KEY_KIND] = OP_REPLACE;
    operation[KEY_ADDRESS] = pending.address;
    operation[KEY_ENVELOPE]
        = seal_record(*schema, schema->storage_version, p_values);
    pending.operations.push_back(operation);
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::patch(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id,
    const Dictionary &p_values
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (String(p_id).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a record id may not be empty"
        );
    }
    if (p_values.is_empty()) {
        return NetwPromise::resolved(OK);
    }
    const Array fields = p_values.keys();
    for (int at = 0; at < fields.size(); ++at) {
        const StringName key = StringName(String(fields[at]));
        const int column = schemas->find_column(p_schema, key);
        if (column < 0) {
            return NetwPromise::rejected(
                ERR_INVALID_DATA,
                vformat(
                    "schema '%s' declares no column '%s' to patch",
                    String(schema->name),
                    String(key)
                )
            );
        }
        const Error checked = SchemaCore::validate_value(
            schema->at(column)->type,
            schema->at(column)->stride,
            p_values[fields[at]]
        );
        if (checked != OK) {
            return NetwPromise::rejected(
                checked,
                vformat(
                    "column '%s' of schema '%s' refuses the value the patch "
                    "carries",
                    String(key),
                    String(schema->name)
                )
            );
        }
    }
    Pending pending;
    pending.op = Op::PATCH;
    pending.schema = p_schema;
    pending.id = p_id;
    pending.patch_values = p_values.duplicate(true);
    pending.address = address_of(Kind::RECORD, schema->name, String(p_id));
    pending.locks.push_back(address_text(pending.address));
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::erase(
    const RID &p_database,
    const RID &p_schema,
    const StringName &p_id
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (String(p_id).is_empty()) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a record id may not be empty"
        );
    }
    Pending pending;
    pending.op = Op::ERASE;
    pending.schema = p_schema;
    pending.id = p_id;
    pending.address = address_of(Kind::RECORD, schema->name, String(p_id));
    pending.locks.push_back(address_text(pending.address));
    Dictionary operation;
    operation[KEY_KIND] = OP_ERASE;
    operation[KEY_ADDRESS] = pending.address;
    pending.operations.push_back(operation);
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::scan(
    const RID &p_database,
    const RID &p_schema,
    const Dictionary &p_filter,
    const String &p_cursor,
    int p_limit
) {
    const SchemaRecord *schema = sealed(p_schema);
    if (schema == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "this session declares no such sealed schema"
        );
    }
    if (p_limit < 1) {
        return NetwPromise::rejected(
            ERR_INVALID_PARAMETER,
            "a scan reads at least one record per page"
        );
    }
    Pending pending;
    pending.op = Op::SCAN;
    pending.schema = p_schema;
    Dictionary request;
    request["schema_name"] = String(schema->name);
    request[KEY_KIND] = int(Kind::RECORD);
    request["filter"] = p_filter.duplicate(true);
    request[KEY_CURSOR] = p_cursor;
    request["limit"] = p_limit;
    pending.request = request;
    return admit(p_database, pending);
}

Ref<NetwPromise> Databases::submit(
    const RID &p_database,
    const Array &p_operations
) {
    if (p_operations.is_empty()) {
        return NetwPromise::resolved(database_batch_result_of(
            OK,
            String(),
            PackedInt32Array(),
            PackedByteArray()
        ));
    }
    Pending pending;
    pending.op = Op::BATCH;
    for (int at = 0; at < p_operations.size(); ++at) {
        const Dictionary operation = p_operations[at];
        const Dictionary address = operation.get(KEY_ADDRESS, Dictionary());
        if (address.is_empty()) {
            return NetwPromise::rejected(
                ERR_INVALID_DATA,
                vformat("batch operation %d names no address", at)
            );
        }
        const String text = address_text(address);
        bool known = false;
        for (uint32_t lock = 0; lock < pending.locks.size(); ++lock) {
            known = known || pending.locks[lock] == text;
        }
        if (!known) {
            pending.locks.push_back(text);
        }
        pending.operations.push_back(operation);
    }
    return admit(p_database, pending);
}

} // namespace netw::persist
