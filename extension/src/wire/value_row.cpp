#include "netw/wire/value_row.hpp"

#include <cstring>

#include "netw/api/bit_buffer.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/api/quantize.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

namespace {

using namespace godot;

void put_u64(const Ref<NetwBitBufferWriter> &p_writer, uint64_t p_value) {
    p_writer->put_bits(int64_t(p_value & 0xffffffffU), 32);
    p_writer->put_bits(int64_t(p_value >> 32), 32);
}

uint64_t get_u64(const Ref<NetwBitBufferReader> &p_reader) {
    const uint64_t low = uint64_t(p_reader->get_bits(32));
    const uint64_t high = uint64_t(p_reader->get_bits(32));
    return low | (high << 32);
}

void put_f32(const Ref<NetwBitBufferWriter> &p_writer, double p_value) {
    const float narrowed = float(p_value);
    uint32_t bits = 0;
    memcpy(&bits, &narrowed, sizeof(bits));
    p_writer->put_bits(bits, 32);
}

double get_f32(const Ref<NetwBitBufferReader> &p_reader) {
    const uint32_t bits = uint32_t(p_reader->get_bits(32));
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

void put_f64(const Ref<NetwBitBufferWriter> &p_writer, double p_value) {
    uint64_t bits = 0;
    memcpy(&bits, &p_value, sizeof(bits));
    put_u64(p_writer, bits);
}

double get_f64(const Ref<NetwBitBufferReader> &p_reader) {
    const uint64_t bits = get_u64(p_reader);
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int integer_width(int p_type) {
    switch (p_type) {
        case SchemaCore::I8:
        case SchemaCore::U8:
            return 8;
        case SchemaCore::I16:
        case SchemaCore::U16:
            return 16;
        case SchemaCore::I32:
            return 32;
        case SchemaCore::I64:
            return 64;
        default:
            return 0;
    }
}

bool signed_integer(int p_type) {
    return p_type == SchemaCore::I8 || p_type == SchemaCore::I16
        || p_type == SchemaCore::I32 || p_type == SchemaCore::I64;
}

int64_t sign_extend(uint64_t p_value, int p_width) {
    if (p_width >= 64) {
        return int64_t(p_value);
    }
    const uint64_t sign = uint64_t(1) << (p_width - 1);
    return int64_t((p_value ^ sign) - sign);
}

bool write_raw(
    const Ref<NetwBitBufferWriter> &p_writer,
    int p_type,
    const Variant &p_value
) {
    const int width = integer_width(p_type);
    if (width > 0) {
        const int64_t value = p_value;
        if (width == 64) {
            put_u64(p_writer, uint64_t(value));
        } else {
            p_writer->put_bits(value, width);
        }
        return true;
    }
    switch (p_type) {
        case SchemaCore::F32:
            put_f32(p_writer, double(p_value));
            return true;
        case SchemaCore::F64:
            put_f64(p_writer, double(p_value));
            return true;
        case SchemaCore::BOOL:
            p_writer->put_bits(bool(p_value) ? 1 : 0, 1);
            return true;
        case SchemaCore::VECTOR2: {
            const Vector2 value = p_value;
            put_f32(p_writer, value.x);
            put_f32(p_writer, value.y);
            return true;
        }
        case SchemaCore::VECTOR3: {
            const Vector3 value = p_value;
            put_f32(p_writer, value.x);
            put_f32(p_writer, value.y);
            put_f32(p_writer, value.z);
            return true;
        }
        case SchemaCore::VECTOR4: {
            const Vector4 value = p_value;
            put_f32(p_writer, value.x);
            put_f32(p_writer, value.y);
            put_f32(p_writer, value.z);
            put_f32(p_writer, value.w);
            return true;
        }
        case SchemaCore::COLOR: {
            const Color value = p_value;
            put_f32(p_writer, value.r);
            put_f32(p_writer, value.g);
            put_f32(p_writer, value.b);
            put_f32(p_writer, value.a);
            return true;
        }
        case SchemaCore::QUATERNION: {
            const Quaternion value = p_value;
            put_f32(p_writer, value.x);
            put_f32(p_writer, value.y);
            put_f32(p_writer, value.z);
            put_f32(p_writer, value.w);
            return true;
        }
        default:
            return false;
    }
}

bool read_raw(
    const Ref<NetwBitBufferReader> &p_reader,
    int p_type,
    Variant &r_value
) {
    const int width = integer_width(p_type);
    if (width > 0) {
        const uint64_t raw = width == 64 ? get_u64(p_reader)
                                         : uint64_t(p_reader->get_bits(width));
        r_value
            = signed_integer(p_type) ? sign_extend(raw, width) : int64_t(raw);
        return true;
    }
    switch (p_type) {
        case SchemaCore::F32:
            r_value = get_f32(p_reader);
            return true;
        case SchemaCore::F64:
            r_value = get_f64(p_reader);
            return true;
        case SchemaCore::BOOL:
            r_value = p_reader->get_bits(1) != 0;
            return true;
        case SchemaCore::VECTOR2: {
            const double x = get_f32(p_reader);
            const double y = get_f32(p_reader);
            r_value = Vector2(x, y);
            return true;
        }
        case SchemaCore::VECTOR3: {
            const double x = get_f32(p_reader);
            const double y = get_f32(p_reader);
            const double z = get_f32(p_reader);
            r_value = Vector3(x, y, z);
            return true;
        }
        case SchemaCore::VECTOR4: {
            const double x = get_f32(p_reader);
            const double y = get_f32(p_reader);
            const double z = get_f32(p_reader);
            const double w = get_f32(p_reader);
            r_value = Vector4(x, y, z, w);
            return true;
        }
        case SchemaCore::COLOR: {
            const double r = get_f32(p_reader);
            const double g = get_f32(p_reader);
            const double b = get_f32(p_reader);
            const double a = get_f32(p_reader);
            r_value = Color(r, g, b, a);
            return true;
        }
        case SchemaCore::QUATERNION: {
            const double x = get_f32(p_reader);
            const double y = get_f32(p_reader);
            const double z = get_f32(p_reader);
            const double w = get_f32(p_reader);
            r_value = Quaternion(x, y, z, w);
            return true;
        }
        default:
            return false;
    }
}

bool scalar_schema(const Ref<SchemaRecord> &p_schema, const WirePlan &p_plan) {
    if (p_schema.is_null() || !p_plan.valid()) {
        return false;
    }
    for (int at = 0; at < p_schema->column_count(); ++at) {
        const Ref<SchemaColumn> column = p_schema->at(at);
        if (column.is_null() || column->stride != 1
            || column->type == SchemaCore::VARIANT
            || column->type == SchemaCore::ENTITY) {
            return false;
        }
    }
    return true;
}

} // namespace

bool encode_scalar_row(
    const Ref<SchemaRecord> &p_schema,
    const Array &p_values,
    CodeRow &r_row
) {
    NETW_ZONE_NC("Wire value row encode", colors::WIRE);
    const WirePlan plan = WirePlan::compile(p_schema);
    NETW_ERR_COND_V(
        !scalar_schema(p_schema, plan)
            || p_values.size() != p_schema->column_count(),
        false,
        sys::WIRE,
        "Value row does not match its sealed scalar schema."
    );
    Ref<NetwBitBufferWriter> writer;
    writer.instantiate();
    for (int at = 0; at < p_schema->column_count(); ++at) {
        const Ref<SchemaColumn> column = p_schema->at(at);
        const int expected = SchemaCore::element_type(column->type);
        NETW_ERR_COND_V(
            !Variant::can_convert_strict(
                p_values[at].get_type(),
                Variant::Type(expected)
            ),
            false,
            sys::WIRE,
            "Column %d value does not match its declared type.",
            at
        );
        if (column->quantizer.is_valid()) {
            NETW_ERR_COND_V(
                !column->quantizer->supports_type(expected),
                false,
                sys::WIRE,
                "Column %d quantizer does not support its declared type.",
                at
            );
            column->quantizer->write(writer, p_values[at]);
        } else {
            NETW_ERR_COND_V(
                !write_raw(writer, column->type, p_values[at]),
                false,
                sys::WIRE,
                "Column %d has no fixed value-row encoding.",
                at
            );
        }
    }
    const CodeRow staged = CodeRow::from_bytes(plan, writer->to_bytes());
    NETW_ERR_COND_V(
        !staged.valid_for(plan),
        false,
        sys::WIRE,
        "Value row encoder did not write its declared bit width."
    );
    r_row = staged;
    NETW_TRACE(
        sys::WIRE,
        "value row columns=%d bytes=%d",
        p_values.size(),
        staged.to_bytes().size()
    );
    return true;
}

bool gather_scalar_row(
    const Ref<SchemaRecord> &p_schema,
    const WirePlan &p_plan,
    const Array &p_values,
    CodeRow &r_row
) {
    if (!p_plan.valid()) {
        return false;
    }
    CodeRow staged = CodeRow::for_plan(p_plan);
    if (!encode_scalar_row(p_schema, p_values, staged)) {
        return false;
    }
    r_row = staged;
    return true;
}

bool decode_scalar_row(
    const Ref<SchemaRecord> &p_schema,
    const CodeRow &p_row,
    Array &r_values
) {
    NETW_ZONE_NC("Wire value row decode", colors::WIRE);
    const WirePlan plan = WirePlan::compile(p_schema);
    if (!scalar_schema(p_schema, plan) || !p_row.valid_for(plan)) {
        return false;
    }
    const Ref<NetwBitBufferReader> reader
        = NetwBitBufferReader::create(p_row.to_bytes());
    Array staged;
    for (int at = 0; at < p_schema->column_count(); ++at) {
        const Ref<SchemaColumn> column = p_schema->at(at);
        Variant value;
        if (column->quantizer.is_valid()) {
            if (!column->quantizer->supports_type(
                    SchemaCore::element_type(column->type)
                )) {
                return false;
            }
            value = column->quantizer->read(
                reader,
                SchemaCore::element_type(column->type)
            );
        } else if (!read_raw(reader, column->type, value)) {
            return false;
        }
        staged.push_back(value);
    }
    r_values = staged;
    return true;
}

} // namespace netw::wire
