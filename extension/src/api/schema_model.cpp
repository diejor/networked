#include "netw/api/schema_model.hpp"

#include "godot/class_db.hpp"
#include "godot/templates.hpp"
#include "netw/log.hpp"
#include "netw/schema_core.hpp"
#include "netw/schema_model.hpp"

using namespace godot;

namespace netw {

Ref<NetwColumnRef> NetwColumnRef::make(
    const Ref<NetwSchema> &p_schema,
    int p_index
) {
    Ref<NetwColumnRef> made;
    made.instantiate();
    made->schema = p_schema;
    made->index = p_index;
    return made;
}

StringName NetwColumnRef::get_key() const {
    if (!is_valid()) {
        return StringName();
    }
    const Ref<NetwSchemaColumn> column = schema->get_columns()[index];
    return column.is_valid() ? column->get_key() : StringName();
}

bool NetwColumnRef::is_valid() const {
    return schema.is_valid() && index >= 0
        && index < schema->get_columns().size();
}

void NetwColumnRef::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_schema"), &NetwColumnRef::get_schema);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "schema",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwSchema",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_schema"
    );
    ClassDB::bind_method(D_METHOD("get_index"), &NetwColumnRef::get_index);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "index",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_index"
    );
    ClassDB::bind_method(D_METHOD("get_key"), &NetwColumnRef::get_key);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::STRING_NAME,
            "key",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_key"
    );
    ClassDB::bind_method(D_METHOD("is_valid"), &NetwColumnRef::is_valid);
}

void NetwSchemaColumn::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_key", "key"),
        &NetwSchemaColumn::set_key
    );
    ClassDB::bind_method(D_METHOD("get_key"), &NetwSchemaColumn::get_key);
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "key"),
        "set_key",
        "get_key"
    );

    ClassDB::bind_method(
        D_METHOD("set_type", "type"),
        &NetwSchemaColumn::set_type
    );
    ClassDB::bind_method(D_METHOD("get_type"), &NetwSchemaColumn::get_type);
    ADD_PROPERTY(PropertyInfo(Variant::INT, "type"), "set_type", "get_type");

    ClassDB::bind_method(
        D_METHOD("set_stride", "stride"),
        &NetwSchemaColumn::set_stride
    );
    ClassDB::bind_method(D_METHOD("get_stride"), &NetwSchemaColumn::get_stride);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "stride"),
        "set_stride",
        "get_stride"
    );

    ClassDB::bind_method(
        D_METHOD("set_quantizer", "quantizer"),
        &NetwSchemaColumn::set_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_quantizer"),
        &NetwSchemaColumn::get_quantizer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_quantizer",
        "get_quantizer"
    );
}

Ref<NetwSchema> NetwSchema::create(const StringName &p_name) {
    Ref<NetwSchema> made;
    made.instantiate();
    made->name = p_name;
    return made;
}

int NetwSchema::column(
    const StringName &p_key,
    NetwMultiplayer::ColumnType p_type,
    int p_stride,
    const Ref<NetwQuantize> &p_quantizer
) {
    for (int at = 0; at < columns.size(); ++at) {
        const Ref<NetwSchemaColumn> existing = columns[at];
        if (existing.is_null() || existing->get_key() != p_key) {
            continue;
        }
        if (existing->get_type() == p_type
            && existing->get_stride() == p_stride) {
            return at;
        }
        return -1;
    }
    Ref<NetwSchemaColumn> fresh;
    fresh.instantiate();
    fresh->set_key(p_key);
    fresh->set_type(p_type);
    fresh->set_stride(p_stride);
    fresh->set_quantizer(p_quantizer);
    columns.push_back(fresh);
    return int(columns.size()) - 1;
}

Ref<NetwSchema> NetwSchema::declare(const StringName &p_name) {
    const Ref<NetwSchema> declaration = schema_model::declare(p_name);
    if (declaration.is_null()) {
        NETW_ERROR(sys::TABLE, "a schema name may not be empty");
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

Ref<NetwColumnRef> NetwSchema::column_ref(int p_index) {
    if (p_index < 0 || p_index >= columns.size()) {
        NETW_ERROR(
            sys::TABLE,
            "schema '%s' has no column %d to reference",
            String(name),
            p_index
        );
        return Ref<NetwColumnRef>();
    }
    return NetwColumnRef::make(Ref<NetwSchema>(this), p_index);
}

Ref<NetwSchema> NetwSchema::storage_version(int p_version) {
    if (p_version < 1) {
        NETW_ERROR(
            sys::TABLE,
            "schema '%s' asks for storage version %d and the first is 1",
            String(name),
            p_version
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
    if (p_from_version < 1 || p_step.is_null()) {
        NETW_ERROR(
            sys::TABLE,
            "schema '%s' declares an unusable migration from version %d",
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
    replicated_lane = p_value;
    return Ref<NetwSchema>(this);
}

Ref<NetwSchema> NetwSchema::reliable(bool p_value) {
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
        D_METHOD("column_ref", "index"),
        &NetwSchema::column_ref
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
    ClassDB::bind_method(D_METHOD("get_columns"), &NetwSchema::get_columns);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "columns",
            PROPERTY_HINT_ARRAY_TYPE,
            "NetwSchemaColumn",
            PROPERTY_USAGE_NONE
        ),
        "",
        "get_columns"
    );
}

} // namespace netw
