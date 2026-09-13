#include "netw/api/quantize.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "godot/class_db.hpp"
#include "godot/object.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr double PI_VALUE = 3.14159265358979323846;
constexpr double TAU_VALUE = PI_VALUE * 2.0;
constexpr double QUATERNION_COMPONENT_LIMIT = 0.7071067811865476;

bool is_scalar_or_vector(Variant::Type type) {
    return type == Variant::FLOAT || type == Variant::INT
        || type == Variant::VECTOR2 || type == Variant::VECTOR3;
}

int component_count(Variant::Type type) {
    if (type == Variant::VECTOR3) {
        return 3;
    }
    if (type == Variant::VECTOR2) {
        return 2;
    }
    return 1;
}

double vector_error(double axis, Variant::Type type) {
    return axis * std::sqrt(double(component_count(type)));
}

bool carries_layout(const Dictionary &property) {
    const int64_t usage = property.get("usage", 0);
    return (usage & PROPERTY_USAGE_STORAGE) != 0;
}

} // namespace

void NetwQuantize::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("supports_type", "type"),
        &NetwQuantize::supports_type
    );
    ClassDB::bind_method(
        D_METHOD("bit_width", "type"),
        &NetwQuantize::bit_width
    );
    ClassDB::bind_method(D_METHOD("stride", "type"), &NetwQuantize::stride);
    ClassDB::bind_method(
        D_METHOD("encode", "value", "element"),
        &NetwQuantize::encode
    );
    ClassDB::bind_method(
        D_METHOD("decode", "codes", "type"),
        &NetwQuantize::decode
    );
    ClassDB::bind_method(
        D_METHOD("total_bits", "type"),
        &NetwQuantize::total_bits
    );
    ClassDB::bind_method(
        D_METHOD("max_error", "type"),
        &NetwQuantize::max_error
    );
    ClassDB::bind_method(
        D_METHOD("is_same_layout", "other"),
        &NetwQuantize::is_same_layout
    );
    GDVIRTUAL_BIND(_supports_type, "type");
    GDVIRTUAL_BIND(_bit_width, "type");
    GDVIRTUAL_BIND(_stride, "type");
    GDVIRTUAL_BIND(_encode, "value", "element");
    GDVIRTUAL_BIND(_decode, "codes", "type");
    GDVIRTUAL_BIND(_max_error, "type");
}

int NetwQuantize::stride(Variant::Type type) const {
    int result = 1;
    GDVIRTUAL_CALL(_stride, type, result);
    return result;
}

int64_t NetwQuantize::encode(const Variant &value, int element) const {
    int64_t result = 0;
    GDVIRTUAL_CALL(_encode, value, element, result);
    return result;
}

Variant NetwQuantize::decode(
    const PackedInt64Array &codes,
    Variant::Type type
) const {
    Variant result;
    GDVIRTUAL_CALL(_decode, codes, type, result);
    return result;
}

int64_t NetwQuantize::total_bits(Variant::Type type) const {
    return int64_t(bit_width(type)) * int64_t(stride(type));
}

bool NetwQuantize::write(wire::WriteStream &stream, const Variant &value) {
    const Variant::Type type = value.get_type();
    const int width = bit_width(type);
    const int count = stride(type);
    for (int element = 0; element < count; ++element) {
        uint64_t code = uint64_t(encode(value, element));
        if (!stream.bits(code, width)) {
            return false;
        }
    }
    return true;
}

bool NetwQuantize::read(
    wire::ReadStream &stream,
    Variant::Type type,
    Variant &r_value
) {
    const int width = bit_width(type);
    const int count = stride(type);
    PackedInt64Array codes;
    codes.resize(count);
    for (int element = 0; element < count; ++element) {
        uint64_t code = 0;
        if (!stream.bits(code, width)) {
            return false;
        }
        codes.set(element, int64_t(code));
    }
    r_value = decode(codes, type);
    return true;
}

bool NetwQuantize::supports_type(Variant::Type type) const {
    bool result = false;
    GDVIRTUAL_CALL(_supports_type, type, result);
    return result;
}

int NetwQuantize::bit_width(Variant::Type type) const {
    int result = 0;
    GDVIRTUAL_CALL(_bit_width, type, result);
    return result;
}

double NetwQuantize::max_error(Variant::Type type) const {
    double result = 0.0;
    GDVIRTUAL_CALL(_max_error, type, result);
    return result;
}

bool NetwQuantize::is_same_layout(const Ref<NetwQuantize> &other) const {
    if (other.ptr() == this) {
        return true;
    }
    if (other.is_null() || other->get_class() != get_class()) {
        return false;
    }
    if (other->get_script() != get_script()) {
        return false;
    }
    const Array properties = gd::property_list(this);
    for (int index = 0; index < properties.size(); ++index) {
        const Dictionary property = properties[index];
        if (!carries_layout(property)) {
            continue;
        }
        const StringName property_name = property.get("name", StringName());
        if (get(property_name) != other->get(property_name)) {
            return false;
        }
    }
    return true;
}

void NetwQuantizeScalar::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_bit_count", "value"),
        &NetwQuantizeScalar::set_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("get_bit_count"),
        &NetwQuantizeScalar::get_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("set_min_limit", "value"),
        &NetwQuantizeScalar::set_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_min_limit"),
        &NetwQuantizeScalar::get_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("set_max_limit", "value"),
        &NetwQuantizeScalar::set_max_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_max_limit"),
        &NetwQuantizeScalar::get_max_limit
    );
    ClassDB::bind_method(
        D_METHOD("set_resolution_step", "value"),
        &NetwQuantizeScalar::set_resolution_step
    );
    ClassDB::bind_method(
        D_METHOD("get_resolution_step"),
        &NetwQuantizeScalar::get_resolution_step
    );
    ClassDB::bind_method(D_METHOD("bits", "bits"), &NetwQuantizeScalar::bits);
    ClassDB::bind_method(D_METHOD("step", "step"), &NetwQuantizeScalar::step);
    ClassDB::bind_method(
        D_METHOD("limits", "min", "max"),
        &NetwQuantizeScalar::limits
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "bit_count", PROPERTY_HINT_RANGE, "1,32,1"),
        "set_bit_count",
        "get_bit_count"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "min_limit"),
        "set_min_limit",
        "get_min_limit"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "max_limit"),
        "set_max_limit",
        "get_max_limit"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::FLOAT,
            "resolution_step",
            PROPERTY_HINT_NONE,
            "",
            PROPERTY_USAGE_EDITOR
        ),
        "set_resolution_step",
        "get_resolution_step"
    );
}

void NetwQuantizeScalar::set_bit_count(int value) {
    bit_count = std::clamp(value, 1, 32);
}

int NetwQuantizeScalar::get_bit_count() const {
    return bit_count;
}

void NetwQuantizeScalar::set_min_limit(double value) {
    min_limit = value;
}

double NetwQuantizeScalar::get_min_limit() const {
    return min_limit;
}

void NetwQuantizeScalar::set_max_limit(double value) {
    max_limit = value;
}

double NetwQuantizeScalar::get_max_limit() const {
    return max_limit;
}

void NetwQuantizeScalar::set_resolution_step(double value) {
    if (!(value > 0.0)) {
        return;
    }
    const int64_t needed
        = int64_t(std::ceil((max_limit - min_limit) / value)) + 2;
    int width = 1;
    while ((int64_t(1) << width) < needed) {
        width += 1;
    }
    set_bit_count(width);
}

double NetwQuantizeScalar::get_resolution_step() const {
    return (max_limit - min_limit) / double(top_code());
}

Ref<NetwQuantizeScalar> NetwQuantizeScalar::bits(int value) {
    set_bit_count(value);
    return Ref<NetwQuantizeScalar>(this);
}

Ref<NetwQuantizeScalar> NetwQuantizeScalar::step(double value) {
    set_resolution_step(value);
    return Ref<NetwQuantizeScalar>(this);
}

Ref<NetwQuantizeScalar> NetwQuantizeScalar::limits(
    double minimum,
    double maximum
) {
    min_limit = minimum;
    max_limit = maximum;
    return Ref<NetwQuantizeScalar>(this);
}

int64_t NetwQuantizeScalar::top_code() const {
    return std::max<int64_t>(2, (int64_t(1) << bit_count) - 1) - 1;
}

int64_t NetwQuantizeScalar::encode_axis(double value) const {
    const double span = max_limit - min_limit;
    const int64_t top = top_code();
    const double unit
        = span == 0.0 ? 0.0 : std::clamp((value - min_limit) / span, 0.0, 1.0);
    return std::clamp<int64_t>(int64_t(std::round(unit * double(top))), 0, top);
}

double NetwQuantizeScalar::decode_axis(int64_t code) const {
    const int64_t top = top_code();
    const int64_t held = std::clamp<int64_t>(code, 0, top);
    return min_limit + double(held) / double(top) * (max_limit - min_limit);
}

int64_t NetwQuantizeScalar::encode(const Variant &value, int element) const {
    if (value.get_type() == Variant::VECTOR3) {
        const Vector3 vector = value;
        return encode_axis(vector[element]);
    }
    if (value.get_type() == Variant::VECTOR2) {
        const Vector2 vector = value;
        return encode_axis(vector[element]);
    }
    return encode_axis(double(value));
}

Variant NetwQuantizeScalar::decode(
    const PackedInt64Array &codes,
    Variant::Type type
) const {
    if (type == Variant::VECTOR3) {
        return Vector3(
            decode_axis(codes[0]),
            decode_axis(codes[1]),
            decode_axis(codes[2])
        );
    }
    if (type == Variant::VECTOR2) {
        return Vector2(decode_axis(codes[0]), decode_axis(codes[1]));
    }
    const double value = decode_axis(codes[0]);
    return type == Variant::INT ? Variant(int64_t(std::round(value)))
                                : Variant(value);
}

bool NetwQuantizeScalar::supports_type(Variant::Type type) const {
    return is_scalar_or_vector(type);
}

int NetwQuantizeScalar::bit_width(Variant::Type type) const {
    (void)type;
    return bit_count;
}

int NetwQuantizeScalar::stride(Variant::Type type) const {
    return component_count(type);
}

double NetwQuantizeScalar::max_error(Variant::Type type) const {
    return vector_error(get_resolution_step() * 0.5, type);
}

void NetwQuantizeAngle::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_bit_count", "value"),
        &NetwQuantizeAngle::set_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("get_bit_count"),
        &NetwQuantizeAngle::get_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("set_centered_on_zero", "value"),
        &NetwQuantizeAngle::set_centered_on_zero
    );
    ClassDB::bind_method(
        D_METHOD("get_centered_on_zero"),
        &NetwQuantizeAngle::get_centered_on_zero
    );
    ClassDB::bind_method(D_METHOD("bits", "bits"), &NetwQuantizeAngle::bits);
    ClassDB::bind_method(D_METHOD("centered"), &NetwQuantizeAngle::centered);
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "bit_count", PROPERTY_HINT_RANGE, "1,32,1"),
        "set_bit_count",
        "get_bit_count"
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::BOOL, "centered_on_zero"),
        "set_centered_on_zero",
        "get_centered_on_zero"
    );
}

void NetwQuantizeAngle::set_bit_count(int value) {
    bit_count = std::clamp(value, 1, 32);
}

int NetwQuantizeAngle::get_bit_count() const {
    return bit_count;
}

void NetwQuantizeAngle::set_centered_on_zero(bool value) {
    centered_on_zero = value;
}

bool NetwQuantizeAngle::get_centered_on_zero() const {
    return centered_on_zero;
}

Ref<NetwQuantizeAngle> NetwQuantizeAngle::bits(int value) {
    set_bit_count(value);
    return Ref<NetwQuantizeAngle>(this);
}

Ref<NetwQuantizeAngle> NetwQuantizeAngle::centered() {
    centered_on_zero = true;
    return Ref<NetwQuantizeAngle>(this);
}

int64_t NetwQuantizeAngle::encode(const Variant &value, int element) const {
    (void)element;
    const uint64_t level_count = uint64_t(1) << bit_count;
    double wrapped = std::fmod(double(value), TAU_VALUE);
    if (wrapped < 0.0) {
        wrapped += TAU_VALUE;
    }
    return int64_t(
        uint64_t(std::llround(wrapped / TAU_VALUE * double(level_count)))
        % level_count
    );
}

Variant NetwQuantizeAngle::decode(
    const PackedInt64Array &codes,
    Variant::Type type
) const {
    const uint64_t level_count = uint64_t(1) << bit_count;
    double angle = double(codes[0]) / double(level_count) * TAU_VALUE;
    if (centered_on_zero && angle > PI_VALUE) {
        angle -= TAU_VALUE;
    }
    return type == Variant::INT ? Variant(int64_t(std::round(angle)))
                                : Variant(angle);
}

bool NetwQuantizeAngle::supports_type(Variant::Type type) const {
    return type == Variant::FLOAT || type == Variant::INT;
}

int NetwQuantizeAngle::bit_width(Variant::Type type) const {
    (void)type;
    return bit_count;
}

int NetwQuantizeAngle::stride(Variant::Type type) const {
    (void)type;
    return 1;
}

double NetwQuantizeAngle::max_error(Variant::Type type) const {
    (void)type;
    return TAU_VALUE / double(uint64_t(1) << bit_count) * 0.5;
}

void NetwQuantizeQuaternion::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_bit_count", "value"),
        &NetwQuantizeQuaternion::set_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("get_bit_count"),
        &NetwQuantizeQuaternion::get_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("bits", "bits"),
        &NetwQuantizeQuaternion::bits
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::INT, "bit_count", PROPERTY_HINT_RANGE, "1,20,1"),
        "set_bit_count",
        "get_bit_count"
    );
}

void NetwQuantizeQuaternion::set_bit_count(int value) {
    bit_count = std::clamp(value, 1, 20);
}

int NetwQuantizeQuaternion::get_bit_count() const {
    return bit_count;
}

Ref<NetwQuantizeQuaternion> NetwQuantizeQuaternion::bits(int value) {
    set_bit_count(value);
    return Ref<NetwQuantizeQuaternion>(this);
}

int64_t NetwQuantizeQuaternion::encode_component(double value) const {
    const int64_t top = (int64_t(1) << bit_count) - 1;
    const double span = QUATERNION_COMPONENT_LIMIT * 2.0;
    const double unit
        = std::clamp((value + QUATERNION_COMPONENT_LIMIT) / span, 0.0, 1.0);
    return int64_t(std::round(unit * double(top)));
}

double NetwQuantizeQuaternion::decode_component(int64_t code) const {
    const int64_t top = (int64_t(1) << bit_count) - 1;
    const double unit = double(code) / double(top);
    return unit * QUATERNION_COMPONENT_LIMIT * 2.0 - QUATERNION_COMPONENT_LIMIT;
}

int64_t NetwQuantizeQuaternion::encode(
    const Variant &value,
    int element
) const {
    (void)element;
    Quaternion quaternion = Quaternion(value).normalized();
    std::array<double, 4> components = {
        quaternion.x,
        quaternion.y,
        quaternion.z,
        quaternion.w,
    };
    int largest = 0;
    for (int index = 1; index < 4; ++index) {
        if (std::abs(components[index]) > std::abs(components[largest])) {
            largest = index;
        }
    }
    if (components[largest] < 0.0) {
        for (double &component : components) {
            component = -component;
        }
    }
    int64_t packed = int64_t(largest);
    int at = 2;
    for (int index = 0; index < 4; ++index) {
        if (index == largest) {
            continue;
        }
        packed |= encode_component(components[index]) << at;
        at += bit_count;
    }
    return packed;
}

Variant NetwQuantizeQuaternion::decode(
    const PackedInt64Array &codes,
    Variant::Type type
) const {
    (void)type;
    const int64_t packed = codes[0];
    const int largest = int(packed & 0x3);
    const int64_t mask = (int64_t(1) << bit_count) - 1;
    std::array<double, 4> components = {0.0, 0.0, 0.0, 0.0};
    double length_squared = 0.0;
    int at = 2;
    for (int index = 0; index < 4; ++index) {
        if (index == largest) {
            continue;
        }
        components[index] = decode_component((packed >> at) & mask);
        length_squared += components[index] * components[index];
        at += bit_count;
    }
    components[largest] = std::sqrt(std::max(0.0, 1.0 - length_squared));
    return Quaternion(
               components[0],
               components[1],
               components[2],
               components[3]
    )
        .normalized();
}

bool NetwQuantizeQuaternion::supports_type(Variant::Type type) const {
    return type == Variant::QUATERNION;
}

int NetwQuantizeQuaternion::bit_width(Variant::Type type) const {
    (void)type;
    return 2 + bit_count * 3;
}

int NetwQuantizeQuaternion::stride(Variant::Type type) const {
    (void)type;
    return 1;
}

double NetwQuantizeQuaternion::max_error(Variant::Type type) const {
    (void)type;
    const double axis
        = QUATERNION_COMPONENT_LIMIT / double((int64_t(1) << bit_count) - 1);
    return 2.0 * std::asin(std::min(1.0, axis * std::sqrt(3.0)));
}

void NetwQuantizeTransform2D::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_origin_quantizer", "value"),
        &NetwQuantizeTransform2D::set_origin_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_origin_quantizer"),
        &NetwQuantizeTransform2D::get_origin_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("set_rotation_quantizer", "value"),
        &NetwQuantizeTransform2D::set_rotation_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_rotation_quantizer"),
        &NetwQuantizeTransform2D::get_rotation_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("set_scale_quantizer", "value"),
        &NetwQuantizeTransform2D::set_scale_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_scale_quantizer"),
        &NetwQuantizeTransform2D::get_scale_quantizer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "origin_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_origin_quantizer",
        "get_origin_quantizer"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "rotation_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_rotation_quantizer",
        "get_rotation_quantizer"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "scale_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_scale_quantizer",
        "get_scale_quantizer"
    );
}

const Ref<NetwQuantize> &NetwQuantizeTransform2D::origin_or_stock() const {
    if (origin_quantizer.is_valid()) {
        return origin_quantizer;
    }
    if (stock_origin.is_null()) {
        Ref<NetwQuantizeScalar> scalar;
        scalar.instantiate();
        scalar->limits(-2048.0, 2048.0);
        scalar->step(0.5);
        stock_origin = scalar;
    }
    return stock_origin;
}

const Ref<NetwQuantize> &NetwQuantizeTransform2D::rotation_or_stock() const {
    if (rotation_quantizer.is_valid()) {
        return rotation_quantizer;
    }
    if (stock_rotation.is_null()) {
        Ref<NetwQuantizeAngle> angle;
        angle.instantiate();
        stock_rotation = angle;
    }
    return stock_rotation;
}

void NetwQuantizeTransform2D::set_origin_quantizer(
    const Ref<NetwQuantize> &value
) {
    origin_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform2D::get_origin_quantizer() const {
    return origin_quantizer;
}

void NetwQuantizeTransform2D::set_rotation_quantizer(
    const Ref<NetwQuantize> &value
) {
    rotation_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform2D::get_rotation_quantizer() const {
    return rotation_quantizer;
}

void NetwQuantizeTransform2D::set_scale_quantizer(
    const Ref<NetwQuantize> &value
) {
    scale_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform2D::get_scale_quantizer() const {
    return scale_quantizer;
}

bool NetwQuantizeTransform2D::write(
    wire::WriteStream &stream,
    const Variant &value
) {
    const Transform2D transform = value;
    return origin_or_stock()->write(stream, transform.get_origin())
        && rotation_or_stock()->write(stream, transform.get_rotation())
        && (!scale_quantizer.is_valid()
            || scale_quantizer->write(stream, transform.get_scale()));
}

bool NetwQuantizeTransform2D::read(
    wire::ReadStream &stream,
    Variant::Type type,
    Variant &r_value
) {
    (void)type;
    Variant origin;
    Variant rotation;
    if (!origin_or_stock()->read(stream, Variant::VECTOR2, origin)
        || !rotation_or_stock()->read(stream, Variant::FLOAT, rotation)) {
        return false;
    }
    if (!scale_quantizer.is_valid()) {
        r_value = Transform2D(double(rotation), Vector2(origin));
        return true;
    }
    Variant scale;
    if (!scale_quantizer->read(stream, Variant::VECTOR2, scale)) {
        return false;
    }
    r_value
        = Transform2D(double(rotation), Vector2(scale), 0.0, Vector2(origin));
    return true;
}

bool NetwQuantizeTransform2D::supports_type(Variant::Type type) const {
    return type == Variant::TRANSFORM2D;
}

int NetwQuantizeTransform2D::bit_width(Variant::Type type) const {
    (void)type;
    int64_t total = origin_or_stock()->total_bits(Variant::VECTOR2)
        + rotation_or_stock()->total_bits(Variant::FLOAT);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->total_bits(Variant::VECTOR2);
    }
    return int(total);
}

int NetwQuantizeTransform2D::stride(Variant::Type type) const {
    (void)type;
    return 1;
}

double NetwQuantizeTransform2D::max_error(Variant::Type type) const {
    (void)type;
    double total = origin_or_stock()->max_error(Variant::VECTOR2)
        + rotation_or_stock()->max_error(Variant::FLOAT);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->max_error(Variant::VECTOR2);
    }
    return total;
}

void NetwQuantizeTransform3D::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_origin_quantizer", "value"),
        &NetwQuantizeTransform3D::set_origin_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_origin_quantizer"),
        &NetwQuantizeTransform3D::get_origin_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("set_rotation_quantizer", "value"),
        &NetwQuantizeTransform3D::set_rotation_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_rotation_quantizer"),
        &NetwQuantizeTransform3D::get_rotation_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("set_scale_quantizer", "value"),
        &NetwQuantizeTransform3D::set_scale_quantizer
    );
    ClassDB::bind_method(
        D_METHOD("get_scale_quantizer"),
        &NetwQuantizeTransform3D::get_scale_quantizer
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "origin_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_origin_quantizer",
        "get_origin_quantizer"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "rotation_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_rotation_quantizer",
        "get_rotation_quantizer"
    );
    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "scale_quantizer",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwQuantize"
        ),
        "set_scale_quantizer",
        "get_scale_quantizer"
    );
}

const Ref<NetwQuantize> &NetwQuantizeTransform3D::origin_or_stock() const {
    if (origin_quantizer.is_valid()) {
        return origin_quantizer;
    }
    if (stock_origin.is_null()) {
        Ref<NetwQuantizeScalar> scalar;
        scalar.instantiate();
        scalar->limits(-2048.0, 2048.0);
        scalar->step(0.5);
        stock_origin = scalar;
    }
    return stock_origin;
}

const Ref<NetwQuantize> &NetwQuantizeTransform3D::rotation_or_stock() const {
    if (rotation_quantizer.is_valid()) {
        return rotation_quantizer;
    }
    if (stock_rotation.is_null()) {
        Ref<NetwQuantizeQuaternion> quaternion;
        quaternion.instantiate();
        stock_rotation = quaternion;
    }
    return stock_rotation;
}

void NetwQuantizeTransform3D::set_origin_quantizer(
    const Ref<NetwQuantize> &value
) {
    origin_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform3D::get_origin_quantizer() const {
    return origin_quantizer;
}

void NetwQuantizeTransform3D::set_rotation_quantizer(
    const Ref<NetwQuantize> &value
) {
    rotation_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform3D::get_rotation_quantizer() const {
    return rotation_quantizer;
}

void NetwQuantizeTransform3D::set_scale_quantizer(
    const Ref<NetwQuantize> &value
) {
    scale_quantizer = value;
}

Ref<NetwQuantize> NetwQuantizeTransform3D::get_scale_quantizer() const {
    return scale_quantizer;
}

bool NetwQuantizeTransform3D::write(
    wire::WriteStream &stream,
    const Variant &value
) {
    const Transform3D transform = value;
    const Basis basis = transform.basis;
    return origin_or_stock()->write(stream, transform.origin)
        && rotation_or_stock()->write(
            stream,
            basis.orthonormalized().get_rotation_quaternion()
        )
        && (!scale_quantizer.is_valid()
            || scale_quantizer->write(stream, basis.get_scale()));
}

bool NetwQuantizeTransform3D::read(
    wire::ReadStream &stream,
    Variant::Type type,
    Variant &r_value
) {
    (void)type;
    Variant origin;
    Variant rotation;
    if (!origin_or_stock()->read(stream, Variant::VECTOR3, origin)
        || !rotation_or_stock()->read(stream, Variant::QUATERNION, rotation)) {
        return false;
    }
    const Quaternion turn = rotation;
    Basis basis(turn);
    if (scale_quantizer.is_valid()) {
        Variant scale;
        if (!scale_quantizer->read(stream, Variant::VECTOR3, scale)) {
            return false;
        }
        basis = basis.scaled_local(Vector3(scale));
    }
    r_value = Transform3D(basis, Vector3(origin));
    return true;
}

bool NetwQuantizeTransform3D::supports_type(Variant::Type type) const {
    return type == Variant::TRANSFORM3D;
}

int NetwQuantizeTransform3D::bit_width(Variant::Type type) const {
    (void)type;
    int64_t total = origin_or_stock()->total_bits(Variant::VECTOR3)
        + rotation_or_stock()->total_bits(Variant::QUATERNION);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->total_bits(Variant::VECTOR3);
    }
    return int(total);
}

int NetwQuantizeTransform3D::stride(Variant::Type type) const {
    (void)type;
    return 1;
}

double NetwQuantizeTransform3D::max_error(Variant::Type type) const {
    (void)type;
    double total = origin_or_stock()->max_error(Variant::VECTOR3)
        + rotation_or_stock()->max_error(Variant::QUATERNION);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->max_error(Variant::VECTOR3);
    }
    return total;
}

} // namespace netw
