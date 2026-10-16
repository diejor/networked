#include "netw/api/schema_model.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "netw/log.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"

using namespace godot;

namespace netw {

Ref<NetwSchema> NetwSchema::create(const StringName &p_name) {
    Ref<NetwSchema> made;
    made.instantiate();
    made->name = p_name;
    return made;
}

Ref<NetwSchema> NetwSchema::from_record(const table::SchemaRecord &p_record) {
    const Ref<NetwSchema> made = create(p_record.name);
    made->columns.reserve(p_record.columns.size());
    for (const table::SchemaColumn &property : p_record.columns) {
        Column declared;
        declared.key = property.key;
        declared.type = NetwMultiplayer::ColumnType(property.type);
        declared.stride = property.stride;
        declared.quantizer = property.quantizer;
        made->columns.push_back(declared);
    }
    for (const table::SchemaMigration &migration : p_record.migrations) {
        made->migrations[migration.from_version] = migration.step;
    }
    made->version = p_record.storage_version;
    made->replicated_lane = false;
    made->read_only = true;
    return made;
}

bool NetwSchema::can_edit() const {
    NETW_ERR_COND_V(
        read_only,
        false,
        sys::TABLE,
        "NetwPersistence.schema cannot be changed. Configure saved "
        "properties with Netw.configure_property."
    );
    return true;
}

int NetwSchema::column(
    const StringName &p_key,
    NetwMultiplayer::ColumnType p_type,
    int p_stride,
    const Ref<NetwQuantize> &p_quantizer
) {
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const Column &existing = columns[at];
        if (existing.key != p_key) {
            continue;
        }
        if (existing.type == p_type && existing.stride == p_stride) {
            return int(at);
        }
        return -1;
    }
    if (!can_edit()) {
        return -1;
    }
    for (const ObjectID &adopter : adopters) {
        if (gd::object_of(adopter) != nullptr) {
            NETW_ERR_V(
                -1,
                sys::TABLE,
                "Schema '%s' is already in use by a live session. Declare "
                "its columns before the session uses it.",
                String(name).utf8().get_data()
            );
        }
    }
    Column fresh;
    fresh.key = p_key;
    fresh.type = p_type;
    fresh.stride = p_stride;
    fresh.quantizer = p_quantizer;
    columns.push_back(fresh);
    return int(columns.size()) - 1;
}

Ref<NetwSchema> NetwSchema::declare(const StringName &p_name) {
    const Ref<NetwSchema> declaration = schema_model::declare(p_name);
    if (declaration.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "NetwSchema.declare needs a name, and it was empty."
        );
    }
    return declaration;
}

int NetwSchema::f32(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(p_key, NetwMultiplayer::COLUMN_F32, p_stride, p_quantizer);
}

int NetwSchema::f64(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(p_key, NetwMultiplayer::COLUMN_F64, p_stride, p_quantizer);
}

int NetwSchema::i8(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_I8,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::u8(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_U8,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::i16(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_I16,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::u16(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_U16,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::i32(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_I32,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::i64(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_I64,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::boolean(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_BOOL,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::vector2(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_VECTOR2,
        p_stride,
        p_quantizer
    );
}

int NetwSchema::vector3(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_VECTOR3,
        p_stride,
        p_quantizer
    );
}

int NetwSchema::vector4(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_VECTOR4,
        p_stride,
        p_quantizer
    );
}

int NetwSchema::color(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(p_key, NetwMultiplayer::COLUMN_COLOR, p_stride, p_quantizer);
}

int NetwSchema::quaternion(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_stride
) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_QUATERNION,
        p_stride,
        p_quantizer
    );
}

int NetwSchema::entity(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_ENTITY,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::variant(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_VARIANT,
        p_stride,
        Ref<NetwQuantize>()
    );
}

int NetwSchema::string(const StringName &p_key, int p_stride) {
    return column(
        p_key,
        NetwMultiplayer::COLUMN_STRING,
        p_stride,
        Ref<NetwQuantize>()
    );
}

Ref<NetwSchema> NetwSchema::storage_version(int p_version) {
    if (!can_edit()) {
        return Ref<NetwSchema>(this);
    }
    if (p_version < 1) {
        NETW_ERROR(
            sys::TABLE,
            "NetwSchema.storage_version got %d for '%s'. Versions start at "
            "1.",
            p_version,
            String(name)
        );
        return Ref<NetwSchema>(this);
    }
    version = p_version;
    return Ref<NetwSchema>(this);
}

Ref<NetwSchema> NetwSchema::migrate(
    int p_from_version,
    const Callable &p_step
) {
    if (!can_edit()) {
        return Ref<NetwSchema>(this);
    }
    if (p_from_version < 1 || p_step.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "NetwSchema.migrate on '%s' needs a version of 1 or more and a "
            "valid step, and got version %d.",
            String(name),
            p_from_version
        );
        return Ref<NetwSchema>(this);
    }
    migrations[p_from_version] = p_step;
    return Ref<NetwSchema>(this);
}

Ref<NetwSchema> NetwSchema::register_declaration() {
    schema_model::adopt(Ref<NetwSchema>(this));
    return Ref<NetwSchema>(this);
}

Ref<NetwSchema> NetwSchema::replicated(bool p_value) {
    if (!can_edit()) {
        return Ref<NetwSchema>(this);
    }
    replicated_lane = p_value;
    return Ref<NetwSchema>(this);
}

Ref<NetwSchema> NetwSchema::reliable(bool p_value) {
    if (!can_edit()) {
        return Ref<NetwSchema>(this);
    }
    reliable_lane = p_value;
    return Ref<NetwSchema>(this);
}

void NetwSchema::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwSchema",
        D_METHOD("declare", "name"),
        &NetwSchema::declare
    );
    ClassDB::bind_static_method(
        "NetwSchema",
        D_METHOD("create", "name"),
        &NetwSchema::create
    );
    ClassDB::bind_method(
        D_METHOD("column", "key", "type", "stride", "quantizer"),
        &NetwSchema::column,
        DEFVAL(1),
        DEFVAL(Ref<NetwQuantize>())
    );
    ClassDB::bind_method(
        D_METHOD("f32", "key", "quantizer", "stride"),
        &NetwSchema::f32,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("f64", "key", "quantizer", "stride"),
        &NetwSchema::f64,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("i8", "key", "stride"),
        &NetwSchema::i8,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("u8", "key", "stride"),
        &NetwSchema::u8,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("i16", "key", "stride"),
        &NetwSchema::i16,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("u16", "key", "stride"),
        &NetwSchema::u16,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("i32", "key", "stride"),
        &NetwSchema::i32,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("i64", "key", "stride"),
        &NetwSchema::i64,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("boolean", "key", "stride"),
        &NetwSchema::boolean,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("vector2", "key", "quantizer", "stride"),
        &NetwSchema::vector2,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("vector3", "key", "quantizer", "stride"),
        &NetwSchema::vector3,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("vector4", "key", "quantizer", "stride"),
        &NetwSchema::vector4,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("color", "key", "quantizer", "stride"),
        &NetwSchema::color,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("quaternion", "key", "quantizer", "stride"),
        &NetwSchema::quaternion,
        DEFVAL(Ref<NetwQuantize>()),
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("entity", "key", "stride"),
        &NetwSchema::entity,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("variant", "key", "stride"),
        &NetwSchema::variant,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("string", "key", "stride"),
        &NetwSchema::string,
        DEFVAL(1)
    );
    ClassDB::bind_method(
        D_METHOD("storage_version", "version"),
        &NetwSchema::storage_version
    );
    ClassDB::bind_method(
        D_METHOD("migrate", "from_version", "step"),
        &NetwSchema::migrate
    );
    ClassDB::bind_method(
        D_METHOD("get_storage_version"),
        &NetwSchema::get_storage_version
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "stored_version",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_storage_version"
    );
    ClassDB::bind_method(
        D_METHOD("register"),
        &NetwSchema::register_declaration
    );
    ClassDB::bind_method(
        D_METHOD("replicated", "value"),
        &NetwSchema::replicated,
        DEFVAL(true)
    );
    ClassDB::bind_method(
        D_METHOD("reliable", "value"),
        &NetwSchema::reliable,
        DEFVAL(true)
    );
    ClassDB::bind_method(D_METHOD("is_replicated"), &NetwSchema::is_replicated);
    ClassDB::bind_method(D_METHOD("is_reliable"), &NetwSchema::is_reliable);
    ClassDB::bind_method(
        D_METHOD("get_schema_name"),
        &NetwSchema::get_schema_name
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::STRING_NAME,
            "schema_name",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_schema_name"
    );
}

} // namespace netw
