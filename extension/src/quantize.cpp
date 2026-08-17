#include "netw/quantize.hpp"

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

bool is_scalar_or_vector(int type) {
    return type == Variant::FLOAT || type == Variant::INT
        || type == Variant::VECTOR2 || type == Variant::VECTOR3;
}

int component_count(int type) {
    if (type == Variant::VECTOR3) {
        return 3;
    }
    if (type == Variant::VECTOR2) {
        return 2;
    }
    return 1;
}

double vector_error(double axis, int type) {
    return axis * std::sqrt(double(component_count(type)));
}

bool carries_layout(const Dictionary &property) {
    const int64_t usage = property.get("usage", 0);
    return (usage & PROPERTY_USAGE_STORAGE) != 0;
}

} // namespace

void NetwQuantize::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("write", "writer", "value"),
        &NetwQuantize::write
    );
    ClassDB::bind_method(
        D_METHOD("read", "reader", "type"),
        &NetwQuantize::read
    );
    ClassDB::bind_method(
        D_METHOD("supports_type", "type"),
        &NetwQuantize::supports_type
    );
    ClassDB::bind_method(
        D_METHOD("bit_width", "type"),
        &NetwQuantize::bit_width
    );
    ClassDB::bind_method(
        D_METHOD("max_error", "type"),
        &NetwQuantize::max_error
    );
    ClassDB::bind_method(
        D_METHOD("is_same_layout", "other"),
        &NetwQuantize::is_same_layout
    );
    GDVIRTUAL_BIND(_write, "writer", "value");
    GDVIRTUAL_BIND(_read, "reader", "type");
    GDVIRTUAL_BIND(_supports_type, "type");
    GDVIRTUAL_BIND(_bit_width, "type");
    GDVIRTUAL_BIND(_max_error, "type");
}

void NetwQuantize::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    GDVIRTUAL_CALL(_write, writer, value);
}

Variant NetwQuantize::read(const Ref<NetwBitBufferReader> &reader, int type) {
    Variant result;
    GDVIRTUAL_CALL(_read, reader, type, result);
    return result;
}

bool NetwQuantize::supports_type(int type) const {
    bool result = false;
    GDVIRTUAL_CALL(_supports_type, type, result);
    return result;
}

int NetwQuantize::bit_width(int type) const {
    int result = 0;
    GDVIRTUAL_CALL(_bit_width, type, result);
    return result;
}

double NetwQuantize::max_error(int type) const {
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

void NetwQuantizeBits::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_bit_count", "value"),
        &NetwQuantizeBits::set_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("get_bit_count"),
        &NetwQuantizeBits::get_bit_count
    );
    ClassDB::bind_method(
        D_METHOD("set_min_limit", "value"),
        &NetwQuantizeBits::set_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_min_limit"),
        &NetwQuantizeBits::get_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("set_max_limit", "value"),
        &NetwQuantizeBits::set_max_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_max_limit"),
        &NetwQuantizeBits::get_max_limit
    );
    ClassDB::bind_method(D_METHOD("bits", "bits"), &NetwQuantizeBits::bits);
    ClassDB::bind_method(
        D_METHOD("limits", "min", "max"),
        &NetwQuantizeBits::limits
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
}

void NetwQuantizeBits::set_bit_count(int value) {
    bit_count = std::clamp(value, 1, 32);
}

int NetwQuantizeBits::get_bit_count() const {
    return bit_count;
}

void NetwQuantizeBits::set_min_limit(double value) {
    min_limit = value;
}

double NetwQuantizeBits::get_min_limit() const {
    return min_limit;
}

void NetwQuantizeBits::set_max_limit(double value) {
    max_limit = value;
}

double NetwQuantizeBits::get_max_limit() const {
    return max_limit;
}

Ref<NetwQuantizeBits> NetwQuantizeBits::bits(int value) {
    set_bit_count(value);
    return Ref<NetwQuantizeBits>(this);
}

Ref<NetwQuantizeBits> NetwQuantizeBits::limits(double minimum, double maximum) {
    min_limit = minimum;
    max_limit = maximum;
    return Ref<NetwQuantizeBits>(this);
}

int NetwQuantizeBits::levels() const {
    const int64_t count = (int64_t(1) << bit_count) - 1;
    return int(std::max<int64_t>(2, count));
}

void NetwQuantizeBits::encode_axis(
    const Ref<NetwBitBufferWriter> &writer,
    double value
) const {
    const double span = max_limit - min_limit;
    const int top = levels() - 1;
    const double unit
        = span == 0.0 ? 0.0 : std::clamp((value - min_limit) / span, 0.0, 1.0);
    const int64_t code
        = std::clamp<int64_t>(int64_t(std::round(unit * top)), 0, top);
    writer->put_bits(code, bit_count);
}

double NetwQuantizeBits::decode_axis(
    const Ref<NetwBitBufferReader> &reader
) const {
    const int top = levels() - 1;
    const int64_t code = std::min<int64_t>(reader->get_bits(bit_count), top);
    return min_limit + double(code) / double(top) * (max_limit - min_limit);
}

void NetwQuantizeBits::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    if (value.get_type() == Variant::VECTOR3) {
        const Vector3 vector = value;
        encode_axis(writer, vector.x);
        encode_axis(writer, vector.y);
        encode_axis(writer, vector.z);
    } else if (value.get_type() == Variant::VECTOR2) {
        const Vector2 vector = value;
        encode_axis(writer, vector.x);
        encode_axis(writer, vector.y);
    } else {
        encode_axis(writer, double(value));
    }
}

Variant NetwQuantizeBits::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    if (type == Variant::VECTOR3) {
        const double x = decode_axis(reader);
        const double y = decode_axis(reader);
        const double z = decode_axis(reader);
        return Vector3(x, y, z);
    }
    if (type == Variant::VECTOR2) {
        const double x = decode_axis(reader);
        const double y = decode_axis(reader);
        return Vector2(x, y);
    }
    const double value = decode_axis(reader);
    return type == Variant::INT ? Variant(int64_t(std::round(value)))
                                : Variant(value);
}

bool NetwQuantizeBits::supports_type(int type) const {
    return is_scalar_or_vector(type);
}

int NetwQuantizeBits::bit_width(int type) const {
    return bit_count * component_count(type);
}

double NetwQuantizeBits::max_error(int type) const {
    const double axis = (max_limit - min_limit) / double(levels() - 1) * 0.5;
    return vector_error(axis, type);
}

void NetwQuantizeFixed::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_resolution_step", "value"),
        &NetwQuantizeFixed::set_resolution_step
    );
    ClassDB::bind_method(
        D_METHOD("get_resolution_step"),
        &NetwQuantizeFixed::get_resolution_step
    );
    ClassDB::bind_method(
        D_METHOD("set_min_limit", "value"),
        &NetwQuantizeFixed::set_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_min_limit"),
        &NetwQuantizeFixed::get_min_limit
    );
    ClassDB::bind_method(
        D_METHOD("set_max_limit", "value"),
        &NetwQuantizeFixed::set_max_limit
    );
    ClassDB::bind_method(
        D_METHOD("get_max_limit"),
        &NetwQuantizeFixed::get_max_limit
    );
    ClassDB::bind_method(D_METHOD("step", "step"), &NetwQuantizeFixed::step);
    ClassDB::bind_method(
        D_METHOD("limits", "min", "max"),
        &NetwQuantizeFixed::limits
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "resolution_step"),
        "set_resolution_step",
        "get_resolution_step"
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
}

void NetwQuantizeFixed::set_resolution_step(double value) {
    resolution_step = value;
}

double NetwQuantizeFixed::get_resolution_step() const {
    return resolution_step;
}

void NetwQuantizeFixed::set_min_limit(double value) {
    min_limit = value;
}

double NetwQuantizeFixed::get_min_limit() const {
    return min_limit;
}

void NetwQuantizeFixed::set_max_limit(double value) {
    max_limit = value;
}

double NetwQuantizeFixed::get_max_limit() const {
    return max_limit;
}

Ref<NetwQuantizeFixed> NetwQuantizeFixed::step(double value) {
    resolution_step = value;
    return Ref<NetwQuantizeFixed>(this);
}

Ref<NetwQuantizeFixed> NetwQuantizeFixed::limits(
    double minimum,
    double maximum
) {
    min_limit = minimum;
    max_limit = maximum;
    return Ref<NetwQuantizeFixed>(this);
}

int NetwQuantizeFixed::grid_bits() const {
    const int64_t level_count
        = int64_t(std::ceil((max_limit - min_limit) / resolution_step)) + 1;
    int result = 1;
    while ((int64_t(1) << result) < level_count) {
        result += 1;
    }
    return result;
}

void NetwQuantizeFixed::encode_axis(
    const Ref<NetwBitBufferWriter> &writer,
    double value
) const {
    const int width = grid_bits();
    const double bounded = std::clamp(value, min_limit, max_limit);
    const int64_t code
        = int64_t(std::round((bounded - min_limit) / resolution_step));
    const int64_t top = (int64_t(1) << width) - 1;
    writer->put_bits(std::clamp<int64_t>(code, 0, top), width);
}

double NetwQuantizeFixed::decode_axis(
    const Ref<NetwBitBufferReader> &reader
) const {
    return min_limit + double(reader->get_bits(grid_bits())) * resolution_step;
}

void NetwQuantizeFixed::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    if (value.get_type() == Variant::VECTOR3) {
        const Vector3 vector = value;
        encode_axis(writer, vector.x);
        encode_axis(writer, vector.y);
        encode_axis(writer, vector.z);
    } else if (value.get_type() == Variant::VECTOR2) {
        const Vector2 vector = value;
        encode_axis(writer, vector.x);
        encode_axis(writer, vector.y);
    } else {
        encode_axis(writer, double(value));
    }
}

Variant NetwQuantizeFixed::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    if (type == Variant::VECTOR3) {
        const double x = decode_axis(reader);
        const double y = decode_axis(reader);
        const double z = decode_axis(reader);
        return Vector3(x, y, z);
    }
    if (type == Variant::VECTOR2) {
        const double x = decode_axis(reader);
        const double y = decode_axis(reader);
        return Vector2(x, y);
    }
    const double value = decode_axis(reader);
    return type == Variant::INT ? Variant(int64_t(std::round(value)))
                                : Variant(value);
}

bool NetwQuantizeFixed::supports_type(int type) const {
    return is_scalar_or_vector(type);
}

int NetwQuantizeFixed::bit_width(int type) const {
    return grid_bits() * component_count(type);
}

double NetwQuantizeFixed::max_error(int type) const {
    return vector_error(resolution_step * 0.5, type);
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

void NetwQuantizeAngle::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    const uint64_t level_count = uint64_t(1) << bit_count;
    double wrapped = std::fmod(double(value), TAU_VALUE);
    if (wrapped < 0.0) {
        wrapped += TAU_VALUE;
    }
    const uint64_t code
        = uint64_t(std::llround(wrapped / TAU_VALUE * double(level_count)))
        % level_count;
    writer->put_bits(int64_t(code), bit_count);
}

Variant NetwQuantizeAngle::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    const uint64_t level_count = uint64_t(1) << bit_count;
    double angle
        = double(reader->get_bits(bit_count)) / double(level_count) * TAU_VALUE;
    if (centered_on_zero && angle > PI_VALUE) {
        angle -= TAU_VALUE;
    }
    return type == Variant::INT ? Variant(int64_t(std::round(angle)))
                                : Variant(angle);
}

bool NetwQuantizeAngle::supports_type(int type) const {
    return type == Variant::FLOAT || type == Variant::INT;
}

int NetwQuantizeAngle::bit_width(int type) const {
    (void)type;
    return bit_count;
}

double NetwQuantizeAngle::max_error(int type) const {
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

void NetwQuantizeQuaternion::encode_component(
    const Ref<NetwBitBufferWriter> &writer,
    double value
) const {
    const int64_t top = (int64_t(1) << bit_count) - 1;
    const double span = QUATERNION_COMPONENT_LIMIT * 2.0;
    const double unit
        = std::clamp((value + QUATERNION_COMPONENT_LIMIT) / span, 0.0, 1.0);
    writer->put_bits(int64_t(std::round(unit * top)), bit_count);
}

double NetwQuantizeQuaternion::decode_component(
    const Ref<NetwBitBufferReader> &reader
) const {
    const int64_t top = (int64_t(1) << bit_count) - 1;
    const double unit = double(reader->get_bits(bit_count)) / double(top);
    return unit * QUATERNION_COMPONENT_LIMIT * 2.0 - QUATERNION_COMPONENT_LIMIT;
}

void NetwQuantizeQuaternion::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
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
    writer->put_bits(largest, 2);
    for (int index = 0; index < 4; ++index) {
        if (index != largest) {
            encode_component(writer, components[index]);
        }
    }
}

Variant NetwQuantizeQuaternion::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    (void)type;
    const int largest = int(reader->get_bits(2));
    std::array<double, 4> components = {0.0, 0.0, 0.0, 0.0};
    double length_squared = 0.0;
    for (int index = 0; index < 4; ++index) {
        if (index == largest) {
            continue;
        }
        components[index] = decode_component(reader);
        length_squared += components[index] * components[index];
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

bool NetwQuantizeQuaternion::supports_type(int type) const {
    return type == Variant::QUATERNION;
}

int NetwQuantizeQuaternion::bit_width(int type) const {
    (void)type;
    return 2 + bit_count * 3;
}

double NetwQuantizeQuaternion::max_error(int type) const {
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

NetwQuantizeTransform2D::NetwQuantizeTransform2D() {
    Ref<NetwQuantizeFixed> default_origin;
    default_origin.instantiate();
    origin_quantizer = default_origin;
    Ref<NetwQuantizeAngle> default_rotation;
    default_rotation.instantiate();
    rotation_quantizer = default_rotation;
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

void NetwQuantizeTransform2D::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    const Transform2D transform = value;
    origin_quantizer->write(writer, transform.get_origin());
    rotation_quantizer->write(writer, transform.get_rotation());
    if (scale_quantizer.is_valid()) {
        scale_quantizer->write(writer, transform.get_scale());
    }
}

Variant NetwQuantizeTransform2D::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    (void)type;
    const Vector2 origin = origin_quantizer->read(reader, Variant::VECTOR2);
    const double rotation = rotation_quantizer->read(reader, Variant::FLOAT);
    if (scale_quantizer.is_valid()) {
        const Vector2 scale = scale_quantizer->read(reader, Variant::VECTOR2);
        return Transform2D(rotation, scale, 0.0, origin);
    }
    return Transform2D(rotation, origin);
}

bool NetwQuantizeTransform2D::supports_type(int type) const {
    return type == Variant::TRANSFORM2D;
}

int NetwQuantizeTransform2D::bit_width(int type) const {
    (void)type;
    int total = origin_quantizer->bit_width(Variant::VECTOR2)
        + rotation_quantizer->bit_width(Variant::FLOAT);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->bit_width(Variant::VECTOR2);
    }
    return total;
}

double NetwQuantizeTransform2D::max_error(int type) const {
    (void)type;
    double total = origin_quantizer->max_error(Variant::VECTOR2)
        + rotation_quantizer->max_error(Variant::FLOAT);
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

NetwQuantizeTransform3D::NetwQuantizeTransform3D() {
    Ref<NetwQuantizeFixed> default_origin;
    default_origin.instantiate();
    origin_quantizer = default_origin;
    Ref<NetwQuantizeQuaternion> default_rotation;
    default_rotation.instantiate();
    rotation_quantizer = default_rotation;
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

void NetwQuantizeTransform3D::write(
    const Ref<NetwBitBufferWriter> &writer,
    const Variant &value
) {
    const Transform3D transform = value;
    origin_quantizer->write(writer, transform.origin);
    const Basis basis = transform.basis;
    rotation_quantizer->write(
        writer,
        basis.orthonormalized().get_rotation_quaternion()
    );
    if (scale_quantizer.is_valid()) {
        scale_quantizer->write(writer, basis.get_scale());
    }
}

Variant NetwQuantizeTransform3D::read(
    const Ref<NetwBitBufferReader> &reader,
    int type
) {
    (void)type;
    const Vector3 origin = origin_quantizer->read(reader, Variant::VECTOR3);
    const Quaternion rotation
        = rotation_quantizer->read(reader, Variant::QUATERNION);
    Basis basis(rotation);
    if (scale_quantizer.is_valid()) {
        const Vector3 scale = scale_quantizer->read(reader, Variant::VECTOR3);
        basis = basis.scaled_local(scale);
    }
    return Transform3D(basis, origin);
}

bool NetwQuantizeTransform3D::supports_type(int type) const {
    return type == Variant::TRANSFORM3D;
}

int NetwQuantizeTransform3D::bit_width(int type) const {
    (void)type;
    int total = origin_quantizer->bit_width(Variant::VECTOR3)
        + rotation_quantizer->bit_width(Variant::QUATERNION);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->bit_width(Variant::VECTOR3);
    }
    return total;
}

double NetwQuantizeTransform3D::max_error(int type) const {
    (void)type;
    double total = origin_quantizer->max_error(Variant::VECTOR3)
        + rotation_quantizer->max_error(Variant::QUATERNION);
    if (scale_quantizer.is_valid()) {
        total += scale_quantizer->max_error(Variant::VECTOR3);
    }
    return total;
}

} // namespace netw
