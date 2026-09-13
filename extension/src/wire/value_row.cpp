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
    const LocalVector<uint64_t> &p_codes,
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
        if (column->stride != 1 || column->type == SchemaCore::VARIANT) {
            return false;
        }
    }
    return true;
}

} // namespace

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
        "Value row does not match its sealed scalar schema."
    );
    CodeRow staged = CodeRow::for_plan(plan);
    for (int at = 0; at < p_schema.column_count(); ++at) {
        const SchemaColumn *column = p_schema.at(at);
        const ColumnPlan &slot = plan.column(uint32_t(at));
        const Variant::Type expected
            = Variant::Type(SchemaCore::element_type(column->type));
        NETW_ERR_COND_V(
            !Variant::can_convert_strict(p_values[at].get_type(), expected),
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
            for (int element = 0; element < slot.stride; ++element) {
                const uint64_t code = uint64_t(
                    column->quantizer->encode(p_values[at], element)
                );
                NETW_ERR_COND_V(
                    !staged.write(slot, element, code),
                    false,
                    sys::WIRE,
                    "Column %d answered a code wider than it declared.",
                    at
                );
            }
            continue;
        }
        for (int element = 0; element < slot.stride; ++element) {
            uint64_t code = 0;
            NETW_ERR_COND_V(
                !raw_code(column->type, slot.width, p_values[at], element, code)
                    || !staged.write(slot, element, code),
                false,
                sys::WIRE,
                "Column %d has no fixed value-row encoding.",
                at
            );
        }
    }
    r_row = staged;
    NETW_TRACE(
        sys::WIRE,
        "value row columns=%d bits=%d",
        p_values.size(),
        int(plan.row_bits())
    );
    return true;
}

bool gather_scalar_row(
    const SchemaRecord &p_schema,
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
    LocalVector<uint64_t> codes;
    for (int at = 0; at < p_schema.column_count(); ++at) {
        const SchemaColumn *column = p_schema.at(at);
        const ColumnPlan &slot = plan.column(uint32_t(at));
        codes.clear();
        for (int element = 0; element < slot.stride; ++element) {
            codes.push_back(p_row.read(slot, element));
        }
        Variant value;
        if (column->quantizer.is_valid()) {
            const Variant::Type expected
                = Variant::Type(SchemaCore::element_type(column->type));
            if (!column->quantizer->supports_type(expected)) {
                return false;
            }
            PackedInt64Array held;
            held.resize(int64_t(codes.size()));
            for (uint32_t element = 0; element < codes.size(); ++element) {
                held.set(int64_t(element), int64_t(codes[element]));
            }
            value = column->quantizer->decode(held, expected);
        } else if (!raw_value(column->type, slot.width, codes, value)) {
            return false;
        }
        staged.push_back(value);
    }
    r_values = staged;
    return true;
}

} // namespace netw::wire
