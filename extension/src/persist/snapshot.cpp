#include "netw/persist/snapshot.hpp"

#include "godot/templates.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const char *KEY_FORMAT = "format_version";
const char *KEY_KIND = "kind";
const char *KEY_SCHEMA = "schema_name";
const char *KEY_VERSION = "schema_version";
const char *KEY_DESCRIPTOR = "descriptor";
const char *KEY_PAYLOAD = "payload";
const char *KEY_IDS = "ids";
const char *KEY_COLUMNS = "columns";

constexpr int MAX_MIGRATION_STEPS = 64;

int element_count(const Variant &p_data) {
    switch (p_data.get_type()) {
        case Variant::PACKED_FLOAT32_ARRAY:
            return PackedFloat32Array(p_data).size();
        case Variant::PACKED_FLOAT64_ARRAY:
            return PackedFloat64Array(p_data).size();
        case Variant::PACKED_INT32_ARRAY:
            return PackedInt32Array(p_data).size();
        case Variant::PACKED_INT64_ARRAY:
            return PackedInt64Array(p_data).size();
        case Variant::PACKED_BYTE_ARRAY:
            return PackedByteArray(p_data).size();
        case Variant::PACKED_VECTOR2_ARRAY:
            return PackedVector2Array(p_data).size();
        case Variant::PACKED_VECTOR3_ARRAY:
            return PackedVector3Array(p_data).size();
        case Variant::PACKED_VECTOR4_ARRAY:
            return PackedVector4Array(p_data).size();
        case Variant::PACKED_COLOR_ARRAY:
            return PackedColorArray(p_data).size();
        case Variant::PACKED_STRING_ARRAY:
            return PackedStringArray(p_data).size();
        case Variant::ARRAY:
            return Array(p_data).size();
        default:
            return -1;
    }
}

template <class Packed, class Element>
Variant packed_of(const Array &p_values) {
    Packed out;
    out.resize(p_values.size());
    for (int at = 0; at < p_values.size(); ++at) {
        out.set(at, Element(p_values[at]));
    }
    return out;
}

Error pack_column(int p_type, const Array &p_values, Variant &r_out) {
    if (p_type == SchemaCore::QUATERNION) {
        PackedVector4Array out;
        out.resize(p_values.size());
        for (int at = 0; at < p_values.size(); ++at) {
            const Quaternion held = p_values[at];
            out.set(at, Vector4(held.x, held.y, held.z, held.w));
        }
        r_out = out;
        return OK;
    }
    switch (SchemaCore::storage_type(p_type)) {
        case Variant::PACKED_FLOAT32_ARRAY:
            r_out = packed_of<PackedFloat32Array, float>(p_values);
            return OK;
        case Variant::PACKED_FLOAT64_ARRAY:
            r_out = packed_of<PackedFloat64Array, double>(p_values);
            return OK;
        case Variant::PACKED_INT32_ARRAY:
            r_out = packed_of<PackedInt32Array, int32_t>(p_values);
            return OK;
        case Variant::PACKED_INT64_ARRAY:
            r_out = packed_of<PackedInt64Array, int64_t>(p_values);
            return OK;
        case Variant::PACKED_BYTE_ARRAY:
            r_out = packed_of<PackedByteArray, uint8_t>(p_values);
            return OK;
        case Variant::PACKED_VECTOR2_ARRAY:
            r_out = packed_of<PackedVector2Array, Vector2>(p_values);
            return OK;
        case Variant::PACKED_VECTOR3_ARRAY:
            r_out = packed_of<PackedVector3Array, Vector3>(p_values);
            return OK;
        case Variant::PACKED_VECTOR4_ARRAY:
            r_out = packed_of<PackedVector4Array, Vector4>(p_values);
            return OK;
        case Variant::PACKED_COLOR_ARRAY:
            r_out = packed_of<PackedColorArray, Color>(p_values);
            return OK;
        case Variant::PACKED_STRING_ARRAY:
            r_out = packed_of<PackedStringArray, String>(p_values);
            return OK;
        case Variant::ARRAY:
            r_out = p_values;
            return OK;
        default:
            return ERR_INVALID_DATA;
    }
}

Error migrate_rows(
    const SchemaCore &p_core,
    const RID &p_schema,
    const SchemaRecord &p_record,
    int p_from,
    int p_to,
    const PackedStringArray &p_ids,
    Dictionary &r_columns,
    String &r_detail
) {
    const Array keys = r_columns.keys();
    LocalVector<Dictionary> rows;
    rows.resize(p_ids.size());
    for (int at = 0; at < p_ids.size(); ++at) {
        Dictionary row;
        for (int key = 0; key < keys.size(); ++key) {
            const Array held = r_columns[keys[key]];
            row[keys[key]] = at < held.size() ? held[at] : Variant();
        }
        rows[at] = row;
    }

    int have = p_from;
    int steps = 0;
    while (have < p_to) {
        if (++steps > MAX_MIGRATION_STEPS) {
            r_detail = "the migration chain is longer than this library runs";
            return ERR_INVALID_DATA;
        }
        const Callable step = p_core.migration_from(p_schema, have);
        if (step.is_null()) {
            r_detail = vformat(
                "schema '%s' declares no migration from storage version %d",
                String(p_record.name),
                have
            );
            return ERR_UNCONFIGURED;
        }
        for (uint32_t at = 0; at < rows.size(); ++at) {
            Array args;
            args.push_back(rows[at]);
            const Variant answered = step.callv(args);
            if (answered.get_type() != Variant::DICTIONARY) {
                r_detail = vformat(
                    "the migration from storage version %d of schema '%s' "
                    "answered no Dictionary for row %d",
                    have,
                    String(p_record.name),
                    int(at)
                );
                return ERR_INVALID_DATA;
            }
            rows[at] = Dictionary(answered).duplicate(true);
        }
        have += 1;
    }

    Dictionary rebuilt;
    for (int at = 0; at < p_record.column_count(); ++at) {
        const SchemaColumn *column = p_record.at(at);
        Array held;
        for (uint32_t row = 0; row < rows.size(); ++row) {
            if (!rows[row].has(column->key)) {
                r_detail = vformat(
                    "the migrated row %d omits column '%s'",
                    int(row),
                    String(column->key)
                );
                return ERR_INVALID_DATA;
            }
            const Variant value = rows[row][column->key];
            const Error checked = SchemaCore::validate_value(
                column->type,
                column->stride,
                value
            );
            if (checked != OK) {
                r_detail = vformat(
                    "the migrated row %d refuses the value it carries for "
                    "column '%s'",
                    int(row),
                    String(column->key)
                );
                return checked;
            }
            if (column->stride == 1) {
                held.push_back(value);
                continue;
            }
            const Array spread = value;
            for (int part = 0; part < spread.size(); ++part) {
                held.push_back(spread[part]);
            }
        }
        Variant packed;
        const Error repacked = pack_column(column->type, held, packed);
        if (repacked != OK) {
            r_detail = vformat(
                "column '%s' of schema '%s' cannot hold what its migration "
                "answered",
                String(column->key),
                String(p_record.name)
            );
            return repacked;
        }
        rebuilt[String(column->key)] = packed;
    }
    r_columns = rebuilt;
    return OK;
}

} // namespace

Error validate_ids(
    const PackedStringArray &p_ids,
    int p_rows,
    String &r_detail
) {
    if (p_ids.size() != p_rows) {
        r_detail = vformat(
            "the snapshot carries %d durable ids for %d rows",
            p_ids.size(),
            p_rows
        );
        return ERR_INVALID_DATA;
    }
    HashSet<String> seen;
    for (int at = 0; at < p_ids.size(); ++at) {
        if (p_ids[at].is_empty()) {
            r_detail = vformat("row %d carries an empty durable id", at);
            return ERR_INVALID_DATA;
        }
        if (seen.has(p_ids[at])) {
            r_detail = vformat(
                "durable id '%s' names more than one row",
                p_ids[at]
            );
            return ERR_INVALID_DATA;
        }
        seen.insert(p_ids[at]);
    }
    return OK;
}

Dictionary seal_snapshot(
    const SchemaRecord &p_schema,
    int p_version,
    const PackedStringArray &p_ids,
    const LocalVector<Variant> &p_columns
) {
    Dictionary columns;
    for (uint32_t at = 0; at < p_columns.size() && at < uint32_t(p_schema.column_count());
         ++at) {
        columns[String(p_schema.at(int(at))->key)] = p_columns[at];
    }
    Dictionary payload;
    payload[KEY_IDS] = p_ids;
    payload[KEY_COLUMNS] = columns;

    Dictionary out;
    out[KEY_FORMAT] = FORMAT_VERSION;
    out[KEY_KIND] = int(Kind::SNAPSHOT);
    out[KEY_SCHEMA] = String(p_schema.name);
    out[KEY_VERSION] = p_version;
    out[KEY_DESCRIPTOR] = descriptor_of(p_schema);
    out[KEY_PAYLOAD] = payload;
    return out;
}

Error open_snapshot(
    const SchemaCore &p_core,
    const RID &p_schema,
    const Dictionary &p_envelope,
    PackedStringArray &r_ids,
    LocalVector<Variant> &r_columns,
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
            "the stored snapshot for schema '%s' is not in this library's "
            "format, so it was left untouched",
            String(schema->name)
        );
        return ERR_FILE_UNRECOGNIZED;
    }
    if (int(p_envelope[KEY_FORMAT]) != FORMAT_VERSION
        || int(p_envelope.get(KEY_KIND, -1)) != int(Kind::SNAPSHOT)) {
        r_detail = vformat(
            "the stored snapshot for schema '%s' is not a snapshot this "
            "library reads",
            String(schema->name)
        );
        return ERR_FILE_UNRECOGNIZED;
    }
    if (String(p_envelope[KEY_SCHEMA]) != String(schema->name)) {
        r_detail = vformat(
            "the stored snapshot names schema '%s' and the load asked for "
            "'%s'",
            String(p_envelope[KEY_SCHEMA]),
            String(schema->name)
        );
        return ERR_INVALID_DATA;
    }

    const int want = schema->storage_version;
    const int have = int(p_envelope[KEY_VERSION]);
    if (have > want) {
        r_detail = vformat(
            "the stored snapshot carries storage version %d and schema '%s' "
            "reads %d, so it was left untouched",
            have,
            String(schema->name),
            want
        );
        return ERR_FILE_UNRECOGNIZED;
    }

    const Dictionary payload = p_envelope[KEY_PAYLOAD];
    const PackedStringArray ids = payload.get(KEY_IDS, PackedStringArray());
    Dictionary stored = Dictionary(payload.get(KEY_COLUMNS, Dictionary()))
                            .duplicate(true);

    if (have < want) {
        const Error stepped = migrate_rows(
            p_core,
            p_schema,
            *schema,
            have,
            want,
            ids,
            stored,
            r_detail
        );
        if (stepped != OK) {
            return stepped;
        }
    } else if (p_envelope.has(KEY_DESCRIPTOR)) {
        const Array held = p_envelope[KEY_DESCRIPTOR];
        if (!descriptors_agree(held, descriptor_of(*schema))) {
            r_detail = vformat(
                "the stored snapshot's columns disagree with schema '%s' at "
                "storage version %d",
                String(schema->name),
                want
            );
            return ERR_INVALID_DATA;
        }
    }

    const Error checked = validate_ids(ids, ids.size(), r_detail);
    if (checked != OK) {
        return checked;
    }

    LocalVector<Variant> columns;
    columns.resize(schema->column_count());
    for (int at = 0; at < schema->column_count(); ++at) {
        const SchemaColumn *column = schema->at(at);
        const String key = String(column->key);
        if (!stored.has(key)) {
            r_detail = vformat(
                "the stored snapshot omits column '%s' of schema '%s'",
                key,
                String(schema->name)
            );
            return ERR_INVALID_DATA;
        }
        const Variant held = stored[key];
        if (int(held.get_type())
            != SchemaCore::storage_type(column->type)) {
            r_detail = vformat(
                "column '%s' is stored as type %d and schema '%s' declares "
                "%d",
                key,
                int(held.get_type()),
                String(schema->name),
                SchemaCore::storage_type(column->type)
            );
            return ERR_INVALID_DATA;
        }
        if (element_count(held) != ids.size() * column->stride) {
            r_detail = vformat(
                "column '%s' carries %d elements and %d rows of stride %d "
                "need %d",
                key,
                element_count(held),
                ids.size(),
                column->stride,
                ids.size() * column->stride
            );
            return ERR_INVALID_DATA;
        }
        columns[at] = held;
    }

    r_ids = ids;
    r_columns = columns;
    return OK;
}

} // namespace netw::persist
