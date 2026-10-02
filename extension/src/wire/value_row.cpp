#include "netw/wire/value_row.hpp"

#include <cstring>

#include "netw/api/quantize.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

namespace {

using namespace godot;

uint64_t f32_code(double p_value) {
    const float narrowed = float(p_value);
    uint32_t bits = 0;
    memcpy(&bits, &narrowed, sizeof(bits));
    return uint64_t(bits);
}

double f32_value(uint64_t p_code) {
    const uint32_t bits = uint32_t(p_code);
    float value = 0.0f;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

uint64_t f64_code(double p_value) {
    uint64_t bits = 0;
    memcpy(&bits, &p_value, sizeof(bits));
    return bits;
}

double f64_value(uint64_t p_code) {
    double value = 0.0;
    memcpy(&value, &p_code, sizeof(value));
    return value;
}

bool signed_integer(int p_type) {
    return p_type == SchemaCore::I8 || p_type == SchemaCore::I16
        || p_type == SchemaCore::I32 || p_type == SchemaCore::I64;
}

bool integer_column(int p_type) {
    return signed_integer(p_type) || p_type == SchemaCore::U8
        || p_type == SchemaCore::U16;
}

int64_t sign_extend(uint64_t p_value, int p_width) {
    if (p_width >= 64) {
        return int64_t(p_value);
    }
    const uint64_t sign = uint64_t(1) << (p_width - 1);
    return int64_t((p_value ^ sign) - sign);
}

double component_of(const Variant &p_value, int p_type, int p_element) {
    switch (p_type) {
        case SchemaCore::VECTOR2:
            return double(Vector2(p_value)[p_element]);
        case SchemaCore::VECTOR3:
            return double(Vector3(p_value)[p_element]);
        case SchemaCore::VECTOR4:
            return double(Vector4(p_value)[p_element]);
        case SchemaCore::COLOR:
            return double(Color(p_value)[p_element]);
        case SchemaCore::QUATERNION:
            return double(Quaternion(p_value)[p_element]);
        default:
            return 0.0;
    }
}

uint64_t width_mask(int p_width) {
    return p_width >= 64 ? ~uint64_t(0) : ((uint64_t(1) << p_width) - 1);
}

bool raw_code(
    int p_type,
    int p_width,
    const Variant &p_value,
    int p_element,
    uint64_t &r_code
) {
    if (p_type == SchemaCore::ENTITY) {
        const int64_t route = int64_t(p_value);
        r_code = route > 0 ? (uint64_t(route + 1) & width_mask(p_width)) : 0;
        return true;
    }
    if (integer_column(p_type)) {
        r_code = uint64_t(int64_t(p_value)) & width_mask(p_width);
        return true;
    }
    switch (p_type) {
        case SchemaCore::F32:
            r_code = f32_code(double(p_value));
            return true;
        case SchemaCore::F64:
            r_code = f64_code(double(p_value));
            return true;
        case SchemaCore::BOOL:
            r_code = bool(p_value) ? 1 : 0;
            return true;
        case SchemaCore::VECTOR2:
        case SchemaCore::VECTOR3:
        case SchemaCore::VECTOR4:
        case SchemaCore::COLOR:
        case SchemaCore::QUATERNION:
            r_code = f32_code(component_of(p_value, p_type, p_element));
            return true;
        default:
            return false;
    }
}

bool raw_value(
    int p_type,
    int p_width,
    const uint64_t *p_codes,
    Variant &r_value
) {
    if (p_type == SchemaCore::ENTITY) {
        r_value = p_codes[0] == 0 ? int64_t(0) : int64_t(p_codes[0] - 1);
        return true;
    }
    if (integer_column(p_type)) {
        r_value = signed_integer(p_type) ? sign_extend(p_codes[0], p_width)
                                         : int64_t(p_codes[0]);
        return true;
    }
    switch (p_type) {
        case SchemaCore::F32:
            r_value = f32_value(p_codes[0]);
            return true;
        case SchemaCore::F64:
            r_value = f64_value(p_codes[0]);
            return true;
        case SchemaCore::BOOL:
            r_value = p_codes[0] != 0;
            return true;
        case SchemaCore::VECTOR2:
            r_value = Vector2(f32_value(p_codes[0]), f32_value(p_codes[1]));
            return true;
        case SchemaCore::VECTOR3:
            r_value = Vector3(
                f32_value(p_codes[0]),
                f32_value(p_codes[1]),
                f32_value(p_codes[2])
            );
            return true;
        case SchemaCore::VECTOR4:
            r_value = Vector4(
                f32_value(p_codes[0]),
                f32_value(p_codes[1]),
                f32_value(p_codes[2]),
                f32_value(p_codes[3])
            );
            return true;
        case SchemaCore::COLOR:
            r_value = Color(
                float(f32_value(p_codes[0])),
                float(f32_value(p_codes[1])),
                float(f32_value(p_codes[2])),
                float(f32_value(p_codes[3]))
            );
            return true;
        case SchemaCore::QUATERNION:
            r_value = Quaternion(
                f32_value(p_codes[0]),
                f32_value(p_codes[1]),
                f32_value(p_codes[2]),
                f32_value(p_codes[3])
            );
            return true;
        default:
            return false;
    }
}

bool scalar_schema(const SchemaRecord &p_schema, const WirePlan &p_plan) {
    if (!p_plan.valid()) {
        return false;
    }
    for (int at = 0; at < p_schema.column_count(); ++at) {
        const SchemaColumn *column = p_schema.at(at);
        if (column->stride != 1) {
            return false;
        }
    }
    return true;
}

} // namespace

bool is_scalar_schema(const SchemaRecord &p_schema, const WirePlan &p_plan) {
    return scalar_schema(p_schema, p_plan);
}

bool encode_scalar_column(
    const SchemaColumn &p_column,
    const ColumnPlan &p_slot,
    uint32_t p_index,
    const Variant &p_value,
    CodeRow &r_row
) {
    const int at = int(p_index);
    const Variant::Type expected
        = Variant::Type(SchemaCore::element_type(p_column.type));
    if (p_slot.variable) {
        const bool textual = p_column.type != SchemaCore::STRING
            || Variant::can_convert_strict(p_value.get_type(), Variant::STRING);
        NETW_ERR_COND_V(
            !textual || SchemaCore::validate_storable(p_value) != OK,
            false,
            sys::WIRE,
            "Column %d got a value that cannot be sent. Use a plain value, "
            "not an Object.",
            at
        );
        r_row.write_value(p_index, p_value);
        return true;
    }
    NETW_ERR_COND_V(
        !Variant::can_convert_strict(p_value.get_type(), expected),
        false,
        sys::WIRE,
        "Column %d got a value of the wrong type. Write the type its schema "
        "declares.",
        at
    );
    if (p_column.quantizer.is_valid()) {
        NETW_ERR_COND_V(
            !p_column.quantizer->supports_type(expected),
            false,
            sys::WIRE,
            "Column %d has a quantizer that does not support its type. Use "
            "a quantizer made for that type.",
            at
        );
        for (int element = 0; element < p_slot.stride; ++element) {
            const uint64_t code
                = uint64_t(p_column.quantizer->encode(p_value, element));
            NETW_ERR_COND_V(
                !r_row.write(p_slot, element, code),
                false,
                sys::WIRE,
                "Column %d's quantizer produced more bits than it declares. "
                "Check the quantizer's bit count.",
                at
            );
        }
        return true;
    }
    for (int element = 0; element < p_slot.stride; ++element) {
        uint64_t code = 0;
        NETW_ERR_COND_V(
            !raw_code(p_column.type, p_slot.width, p_value, element, code)
                || !r_row.write(p_slot, element, code),
            false,
            sys::WIRE,
            "Column %d has a type that cannot be packed into a fixed row.",
            at
        );
    }
    return true;
}

bool encode_scalar_row(
    const SchemaRecord &p_schema,
    const Array &p_values,
    CodeRow &r_row
) {
    NETW_ZONE_NC("Wire value row encode", colors::WIRE);
    const WirePlan plan = WirePlan::compile(p_schema);
    NETW_ERR_COND_V(
        !scalar_schema(p_schema, plan)
            || p_values.size() != p_schema.column_count(),
        false,
        sys::WIRE,
        "The row's values do not match its schema. Pass one value for each "
        "column."
    );
    CodeRow staged = CodeRow::for_plan(plan);
    for (int at = 0; at < p_schema.column_count(); ++at) {
        if (!encode_scalar_column(
                *p_schema.at(at),
                plan.column(uint32_t(at)),
                uint32_t(at),
                p_values[at],
                staged
            )) {
            return false;
        }
    }
    r_row = staged;
    NETW_TRACE(
        sys::WIRE,
        "Packed %d values into %d bits.",
        p_values.size(),
        int(plan.row_bits())
    );
    return true;
}

bool decode_scalar_column(
    const SchemaColumn &p_column,
    const ColumnPlan &p_slot,
    uint32_t p_index,
    const CodeRow &p_row,
    PackedInt64Array &r_codes,
    Variant &r_value
) {
    if (p_slot.variable) {
        r_value = p_row.read_value(p_index);
        return true;
    }
    constexpr int WIDEST = 16;
    if (p_slot.stride < 1 || p_slot.stride > WIDEST) {
        return false;
    }
    uint64_t codes[WIDEST] = {};
    for (int element = 0; element < p_slot.stride; ++element) {
        codes[element] = p_row.read(p_slot, element);
    }
    if (p_column.quantizer.is_null()) {
        return raw_value(p_column.type, p_slot.width, codes, r_value);
    }
    const Variant::Type expected
        = Variant::Type(SchemaCore::element_type(p_column.type));
    if (!p_column.quantizer->supports_type(expected)) {
        return false;
    }
    if (r_codes.size() != int64_t(p_slot.stride)) {
        r_codes.resize(int64_t(p_slot.stride));
    }
    for (int element = 0; element < p_slot.stride; ++element) {
        r_codes.set(int64_t(element), int64_t(codes[element]));
    }
    r_value = p_column.quantizer->decode(r_codes, expected);
    return true;
}

bool decode_scalar_row(
    const SchemaRecord &p_schema,
    const CodeRow &p_row,
    Array &r_values
) {
    NETW_ZONE_NC("Wire value row decode", colors::WIRE);
    const WirePlan plan = WirePlan::compile(p_schema);
    if (!scalar_schema(p_schema, plan) || !p_row.valid_for(plan)) {
        return false;
    }
    Array staged;
    PackedInt64Array held;
    for (int at = 0; at < p_schema.column_count(); ++at) {
        Variant value;
        if (!decode_scalar_column(
                *p_schema.at(at),
                plan.column(uint32_t(at)),
                uint32_t(at),
                p_row,
                held,
                value
            )) {
            return false;
        }
        staged.push_back(value);
    }
    r_values = staged;
    return true;
}

} // namespace netw::wire
