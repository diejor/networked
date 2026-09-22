#include "netw/api/database_result.hpp"

using namespace godot;

namespace netw {

namespace {

const char *KEY_ERROR = "error";
const char *KEY_DETAIL = "detail";
const char *KEY_FOUND = "found";
const char *KEY_ID = "id";
const char *KEY_VALUES = "values";
const char *KEY_RECORDS = "records";
const char *KEY_CURSOR = "cursor";
const char *KEY_SLOTS = "slots";
const char *KEY_ERRORS = "errors";
const char *KEY_UNCERTAIN = "uncertain";
const char *KEY_IDS = "ids";
const char *KEY_ROUTES = "routes";

} // namespace

Dictionary database_read_hit(
    const StringName &p_id,
    const Dictionary &p_values
) {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_FOUND] = true;
    out[KEY_ID] = p_id;
    out[KEY_VALUES] = p_values.duplicate(true);
    return out;
}

Dictionary database_read_miss(const StringName &p_id) {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_FOUND] = false;
    out[KEY_ID] = p_id;
    out[KEY_VALUES] = Dictionary();
    return out;
}

Dictionary database_read_failure(
    const StringName &p_id,
    Error p_error,
    const String &p_detail
) {
    Dictionary out;
    out[KEY_ERROR] = int(p_error == OK ? FAILED : p_error);
    out[KEY_DETAIL] = p_detail;
    out[KEY_FOUND] = false;
    out[KEY_ID] = p_id;
    out[KEY_VALUES] = Dictionary();
    return out;
}

Dictionary database_page_of(const Array &p_records, const String &p_cursor) {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_RECORDS] = p_records;
    out[KEY_CURSOR] = p_cursor;
    return out;
}

Dictionary database_page_failure(Error p_error, const String &p_detail) {
    Dictionary out;
    out[KEY_ERROR] = int(p_error == OK ? FAILED : p_error);
    out[KEY_DETAIL] = p_detail;
    out[KEY_RECORDS] = Array();
    out[KEY_CURSOR] = String();
    return out;
}

Dictionary database_slots_of(const PackedStringArray &p_slots) {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_SLOTS] = p_slots;
    return out;
}

Dictionary database_slots_failure(Error p_error, const String &p_detail) {
    Dictionary out;
    out[KEY_ERROR] = int(p_error == OK ? FAILED : p_error);
    out[KEY_DETAIL] = p_detail;
    out[KEY_SLOTS] = PackedStringArray();
    return out;
}

Dictionary database_batch_result_of(
    Error p_error,
    const String &p_detail,
    const PackedInt32Array &p_errors,
    const PackedByteArray &p_uncertain
) {
    Dictionary out;
    out[KEY_ERROR] = int(p_error);
    out[KEY_DETAIL] = p_detail;
    out[KEY_ERRORS] = p_errors;
    out[KEY_UNCERTAIN] = p_uncertain;
    return out;
}

Dictionary database_batch_result_refused(
    int p_count,
    Error p_error,
    const String &p_detail
) {
    PackedInt32Array errors;
    PackedByteArray uncertain;
    errors.resize(p_count);
    uncertain.resize(p_count);
    errors.fill(p_error == OK ? FAILED : p_error);
    uncertain.fill(0);
    return database_batch_result_of(
        p_error == OK ? FAILED : p_error,
        p_detail,
        errors,
        uncertain
    );
}

Dictionary table_load_of(
    const PackedStringArray &p_ids,
    const PackedInt64Array &p_routes
) {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_FOUND] = true;
    out[KEY_IDS] = p_ids;
    out[KEY_ROUTES] = p_routes;
    return out;
}

Dictionary table_load_miss() {
    Dictionary out;
    out[KEY_ERROR] = int(OK);
    out[KEY_DETAIL] = String();
    out[KEY_FOUND] = false;
    out[KEY_IDS] = PackedStringArray();
    out[KEY_ROUTES] = PackedInt64Array();
    return out;
}

Dictionary table_load_failure(Error p_error, const String &p_detail) {
    Dictionary out;
    out[KEY_ERROR] = int(p_error == OK ? FAILED : p_error);
    out[KEY_DETAIL] = p_detail;
    out[KEY_FOUND] = false;
    out[KEY_IDS] = PackedStringArray();
    out[KEY_ROUTES] = PackedInt64Array();
    return out;
}

} // namespace netw
