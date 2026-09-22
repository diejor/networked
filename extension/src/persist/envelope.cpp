#include "netw/persist/envelope.hpp"

#include "netw/log.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *KEY_FORMAT = "format_version";
const char *KEY_KIND = "kind";
const char *KEY_SCHEMA = "schema_name";
const char *KEY_VERSION = "schema_version";
const char *KEY_DESCRIPTOR = "descriptor";
const char *KEY_PAYLOAD = "payload";

constexpr int MAX_MIGRATION_STEPS = 64;

Array sorted_keys(const SchemaRecord &p_schema) {
    PackedStringArray names;
    for (int at = 0; at < p_schema.column_count(); ++at) {
        names.push_back(String(p_schema.at(at)->key));
    }
    names.sort();
    Array out;
    for (int at = 0; at < names.size(); ++at) {
        out.push_back(names[at]);
    }
    return out;
}

} // namespace

Array descriptor_of(const SchemaRecord &p_schema) {
    const Array order = sorted_keys(p_schema);
    Array out;
    for (int at = 0; at < order.size(); ++at) {
        const StringName key = StringName(String(order[at]));
        for (int column = 0; column < p_schema.column_count(); ++column) {
            const SchemaColumn *found = p_schema.at(column);
            if (found->key != key) {
                continue;
            }
            Array row;
            row.push_back(String(found->key));
            row.push_back(found->type);
            row.push_back(found->stride);
            out.push_back(row);
            break;
        }
    }
    return out;
}

bool descriptors_agree(const Array &p_left, const Array &p_right) {
    if (p_left.size() != p_right.size()) {
        return false;
    }
    for (int at = 0; at < p_left.size(); ++at) {
        const Array left = p_left[at];
        const Array right = p_right[at];
        if (left.size() != 3 || right.size() != 3) {
            return false;
        }
        for (int part = 0; part < 3; ++part) {
            if (left[part] != right[part]) {
                return false;
            }
        }
    }
    return true;
}

Error validate_row(
    const SchemaRecord &p_schema,
    const Dictionary &p_values,
    String &r_detail
) {
    if (p_values.size() != p_schema.column_count()) {
        r_detail = vformat(
            "schema '%s' declares %d columns and the row carries %d",
            String(p_schema.name),
            p_schema.column_count(),
            p_values.size()
        );
        return ERR_INVALID_DATA;
    }
    for (int at = 0; at < p_schema.column_count(); ++at) {
        const SchemaColumn *column = p_schema.at(at);
        if (!p_values.has(column->key)) {
            r_detail = vformat(
                "schema '%s' declares column '%s' and the row omits it",
                String(p_schema.name),
                String(column->key)
            );
            return ERR_INVALID_DATA;
        }
        const Error checked = SchemaCore::validate_value(
            column->type,
            column->stride,
            p_values[column->key]
        );
        if (checked != OK) {
            r_detail = vformat(
                "column '%s' of schema '%s' refuses the value it was given",
                String(column->key),
                String(p_schema.name)
            );
            return checked;
        }
    }
    return OK;
}

Dictionary seal_record(
    const SchemaRecord &p_schema,
    int p_version,
    const Dictionary &p_values
) {
    Dictionary out;
    out[KEY_FORMAT] = FORMAT_VERSION;
    out[KEY_KIND] = int(Kind::RECORD);
    out[KEY_SCHEMA] = String(p_schema.name);
    out[KEY_VERSION] = p_version;
    out[KEY_DESCRIPTOR] = descriptor_of(p_schema);
    out[KEY_PAYLOAD] = p_values.duplicate(true);
    return out;
}

Error open_record(
    const SchemaCore &p_core,
    const RID &p_schema,
    const Dictionary &p_envelope,
    Dictionary &r_values,
    String &r_detail
) {
    const SchemaRecord *schema = p_core.record_of(p_schema);
    if (schema == nullptr) {
        r_detail = "the reading session declares no such schema";
        return ERR_DOES_NOT_EXIST;
    }
    if (!p_envelope.has(KEY_FORMAT) || !p_envelope.has(KEY_VERSION)
        || !p_envelope.has(KEY_PAYLOAD)) {
        r_detail = vformat(
            "the stored record for schema '%s' is not in this library's "
            "format, so it was left untouched",
            String(schema->name)
        );
        return ERR_FILE_UNRECOGNIZED;
    }
    if (int(p_envelope[KEY_FORMAT]) != FORMAT_VERSION) {
        r_detail = vformat(
            "the stored record carries format version %d and this library "
            "reads %d",
            int(p_envelope[KEY_FORMAT]),
            FORMAT_VERSION
        );
        return ERR_FILE_UNRECOGNIZED;
    }
    if (String(p_envelope[KEY_SCHEMA]) != String(schema->name)) {
        r_detail = vformat(
            "the stored record names schema '%s' and the read asked for '%s'",
            String(p_envelope[KEY_SCHEMA]),
            String(schema->name)
        );
        return ERR_INVALID_DATA;
    }

    const int want = schema->storage_version;
    int have = int(p_envelope[KEY_VERSION]);
    if (have > want) {
        r_detail = vformat(
            "the stored record carries storage version %d and schema '%s' "
            "reads %d, so it was left untouched",
            have,
            String(schema->name),
            want
        );
        return ERR_FILE_UNRECOGNIZED;
    }

    Dictionary row = Dictionary(p_envelope[KEY_PAYLOAD]).duplicate(true);
    int steps = 0;
    while (have < want) {
        if (++steps > MAX_MIGRATION_STEPS) {
            r_detail = "the migration chain is longer than this library runs";
            return ERR_INVALID_DATA;
        }
        const Callable step = p_core.migration_from(p_schema, have);
        if (step.is_null()) {
            r_detail = vformat(
                "schema '%s' declares no migration from storage version %d",
                String(schema->name),
                have
            );
            return ERR_UNCONFIGURED;
        }
        Array args;
        args.push_back(row);
        const Variant answered = step.callv(args);
        if (answered.get_type() != Variant::DICTIONARY) {
            r_detail = vformat(
                "the migration from storage version %d of schema '%s' "
                "answered no Dictionary",
                have,
                String(schema->name)
            );
            return ERR_INVALID_DATA;
        }
        row = Dictionary(answered).duplicate(true);
        have += 1;
    }

    if (steps == 0 && p_envelope.has(KEY_DESCRIPTOR)) {
        const Array stored = p_envelope[KEY_DESCRIPTOR];
        if (!descriptors_agree(stored, descriptor_of(*schema))) {
            r_detail = vformat(
                "the stored record's columns disagree with schema '%s' at "
                "storage version %d",
                String(schema->name),
                want
            );
            return ERR_INVALID_DATA;
        }
    }

    const Error checked = validate_row(*schema, row, r_detail);
    if (checked != OK) {
        return checked;
    }
    r_values = row;
    return OK;
}

Dictionary address_of(
    Kind p_kind,
    const StringName &p_schema_name,
    const String &p_key
) {
    Dictionary out;
    out[KEY_KIND] = int(p_kind);
    out[KEY_SCHEMA] = String(p_schema_name);
    out["key"] = p_key;
    return out;
}

String address_text(const Dictionary &p_address) {
    return String::num_int64(int(p_address.get(KEY_KIND, 0))) + "/"
        + String(p_address.get(KEY_SCHEMA, "")).uri_encode() + "/"
        + String(p_address.get("key", "")).uri_encode();
}

} // namespace netw::persist
