#include "netw/api/database_connection.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

namespace {

Ref<NetwPromise> unimplemented(const char *p_verb) {
    return NetwPromise::rejected(
        ERR_UNAVAILABLE,
        String("this connection implements no ") + p_verb
    );
}

} // namespace

Ref<NetwPromise> NetwDatabaseConnection::read_default(
    const Dictionary &p_address
) {
    return unimplemented("_read");
}

Ref<NetwPromise> NetwDatabaseConnection::scan_default(
    const Dictionary &p_request
) {
    return unimplemented("_scan");
}

Ref<NetwPromise> NetwDatabaseConnection::write_batch_default(
    const Array &p_operations
) {
    return unimplemented("_write_batch");
}

Ref<NetwPromise> NetwDatabaseConnection::close_default() {
    return NetwPromise::resolved(OK);
}

Ref<NetwPromise> NetwDatabaseConnection::read(const Dictionary &p_address) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_read, p_address, answer)) {
        return answer.is_valid() ? answer
                                 : NetwPromise::rejected(
                                       ERR_INVALID_DATA,
                                       "_read answered no promise"
                                   );
    }
    return read_default(p_address);
}

Ref<NetwPromise> NetwDatabaseConnection::scan(const Dictionary &p_request) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_scan, p_request, answer)) {
        return answer.is_valid() ? answer
                                 : NetwPromise::rejected(
                                       ERR_INVALID_DATA,
                                       "_scan answered no promise"
                                   );
    }
    return scan_default(p_request);
}

Ref<NetwPromise> NetwDatabaseConnection::write_batch(const Array &p_operations
) {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_write_batch, p_operations, answer)) {
        return answer.is_valid() ? answer
                                 : NetwPromise::rejected(
                                       ERR_INVALID_DATA,
                                       "_write_batch answered no promise"
                                   );
    }
    return write_batch_default(p_operations);
}

Ref<NetwPromise> NetwDatabaseConnection::close() {
    Ref<NetwPromise> answer;
    if (GDVIRTUAL_CALL(_close, answer)) {
        return answer.is_valid() ? answer : NetwPromise::resolved(OK);
    }
    return close_default();
}

void NetwDatabaseConnection::_bind_methods() {
    GDVIRTUAL_BIND(_read, "address");
    GDVIRTUAL_BIND(_scan, "request");
    GDVIRTUAL_BIND(_write_batch, "operations");
    GDVIRTUAL_BIND(_close);

    ClassDB::bind_method(
        D_METHOD("read_default", "address"),
        &NetwDatabaseConnection::read_default
    );
    ClassDB::bind_method(
        D_METHOD("scan_default", "request"),
        &NetwDatabaseConnection::scan_default
    );
    ClassDB::bind_method(
        D_METHOD("write_batch_default", "operations"),
        &NetwDatabaseConnection::write_batch_default
    );
    ClassDB::bind_method(
        D_METHOD("close_default"),
        &NetwDatabaseConnection::close_default
    );
}

} // namespace netw
