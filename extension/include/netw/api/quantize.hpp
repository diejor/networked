#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/wire/stream.hpp"

namespace netw {

class NetwQuantize : public godot::Resource {
    GDCLASS(NetwQuantize, godot::Resource)

protected:
    static void _bind_methods();

    GDVIRTUAL1RC(bool, _supports_type, godot::Variant::Type)
    GDVIRTUAL1RC(int, _bit_width, godot::Variant::Type)
    GDVIRTUAL1RC(int, _stride, godot::Variant::Type)
    GDVIRTUAL2RC(int64_t, _encode, godot::Variant, int)
    GDVIRTUAL2RC(
        godot::Variant,
        _decode,
        godot::PackedInt64Array,
        godot::Variant::Type
    )
    GDVIRTUAL1RC(double, _max_error, godot::Variant::Type)

public:
    virtual bool supports_type(godot::Variant::Type type) const;
    virtual int bit_width(godot::Variant::Type type) const;
    virtual int stride(godot::Variant::Type type) const;
    virtual int64_t encode(const godot::Variant &value, int element) const;
    virtual godot::Variant decode(
        const godot::PackedInt64Array &codes,
        godot::Variant::Type type
    ) const;
    virtual double max_error(godot::Variant::Type type) const;

    virtual bool write(
        netw::wire::WriteStream &stream,
        const godot::Variant &value
    );
    virtual bool read(
        netw::wire::ReadStream &stream,
        godot::Variant::Type type,
        godot::Variant &r_value
    );

    int64_t total_bits(godot::Variant::Type type) const;
    bool is_same_layout(const godot::Ref<NetwQuantize> &other) const;
};

class NetwQuantizeScalar : public NetwQuantize {
    GDCLASS(NetwQuantizeScalar, NetwQuantize)

    int bit_count = 8;
    double min_limit = -1.0;
    double max_limit = 1.0;

    int64_t top_code() const;
    int64_t encode_axis(double value) const;
    double decode_axis(int64_t code) const;

protected:
    static void _bind_methods();

public:
    void set_bit_count(int value);
    int get_bit_count() const;
    void set_min_limit(double value);
    double get_min_limit() const;
    void set_max_limit(double value);
    double get_max_limit() const;
    void set_resolution_step(double value);
    double get_resolution_step() const;
    godot::Ref<NetwQuantizeScalar> bits(int value);
    godot::Ref<NetwQuantizeScalar> step(double value);
    godot::Ref<NetwQuantizeScalar> limits(double minimum, double maximum);
    bool supports_type(godot::Variant::Type type) const override;
    int bit_width(godot::Variant::Type type) const override;
    int stride(godot::Variant::Type type) const override;
    int64_t encode(const godot::Variant &value, int element) const override;
    godot::Variant decode(
        const godot::PackedInt64Array &codes,
        godot::Variant::Type type
    ) const override;
    double max_error(godot::Variant::Type type) const override;
};

class NetwQuantizeAngle : public NetwQuantize {
    GDCLASS(NetwQuantizeAngle, NetwQuantize)

    int bit_count = 8;
    bool centered_on_zero = false;

protected:
    static void _bind_methods();

public:
    void set_bit_count(int value);
    int get_bit_count() const;
    void set_centered_on_zero(bool value);
    bool get_centered_on_zero() const;
    godot::Ref<NetwQuantizeAngle> bits(int value);
    godot::Ref<NetwQuantizeAngle> centered();
    bool supports_type(godot::Variant::Type type) const override;
    int bit_width(godot::Variant::Type type) const override;
    int stride(godot::Variant::Type type) const override;
    int64_t encode(const godot::Variant &value, int element) const override;
    godot::Variant decode(
        const godot::PackedInt64Array &codes,
        godot::Variant::Type type
    ) const override;
    double max_error(godot::Variant::Type type) const override;
};

class NetwQuantizeQuaternion : public NetwQuantize {
    GDCLASS(NetwQuantizeQuaternion, NetwQuantize)

    int bit_count = 10;

    int64_t encode_component(double value) const;
    double decode_component(int64_t code) const;

protected:
    static void _bind_methods();

public:
    void set_bit_count(int value);
    int get_bit_count() const;
    godot::Ref<NetwQuantizeQuaternion> bits(int value);
    bool supports_type(godot::Variant::Type type) const override;
    int bit_width(godot::Variant::Type type) const override;
    int stride(godot::Variant::Type type) const override;
    int64_t encode(const godot::Variant &value, int element) const override;
    godot::Variant decode(
        const godot::PackedInt64Array &codes,
        godot::Variant::Type type
    ) const override;
    double max_error(godot::Variant::Type type) const override;
};

class NetwQuantizeTransform2D : public NetwQuantize {
    GDCLASS(NetwQuantizeTransform2D, NetwQuantize)

    godot::Ref<NetwQuantize> origin_quantizer;
    godot::Ref<NetwQuantize> rotation_quantizer;
    godot::Ref<NetwQuantize> scale_quantizer;
    mutable godot::Ref<NetwQuantize> stock_origin;
    mutable godot::Ref<NetwQuantize> stock_rotation;

    const godot::Ref<NetwQuantize> &origin_or_stock() const;
    const godot::Ref<NetwQuantize> &rotation_or_stock() const;

protected:
    static void _bind_methods();

public:
    void set_origin_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_origin_quantizer() const;
    void set_rotation_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_rotation_quantizer() const;
    void set_scale_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_scale_quantizer() const;
    bool write(
        netw::wire::WriteStream &stream,
        const godot::Variant &value
    ) override;
    bool read(
        netw::wire::ReadStream &stream,
        godot::Variant::Type type,
        godot::Variant &r_value
    ) override;
    bool supports_type(godot::Variant::Type type) const override;
    int bit_width(godot::Variant::Type type) const override;
    int stride(godot::Variant::Type type) const override;
    double max_error(godot::Variant::Type type) const override;
};

class NetwQuantizeTransform3D : public NetwQuantize {
    GDCLASS(NetwQuantizeTransform3D, NetwQuantize)

    godot::Ref<NetwQuantize> origin_quantizer;
    godot::Ref<NetwQuantize> rotation_quantizer;
    godot::Ref<NetwQuantize> scale_quantizer;
    mutable godot::Ref<NetwQuantize> stock_origin;
    mutable godot::Ref<NetwQuantize> stock_rotation;

    const godot::Ref<NetwQuantize> &origin_or_stock() const;
    const godot::Ref<NetwQuantize> &rotation_or_stock() const;

protected:
    static void _bind_methods();

public:
    void set_origin_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_origin_quantizer() const;
    void set_rotation_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_rotation_quantizer() const;
    void set_scale_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_scale_quantizer() const;
    bool write(
        netw::wire::WriteStream &stream,
        const godot::Variant &value
    ) override;
    bool read(
        netw::wire::ReadStream &stream,
        godot::Variant::Type type,
        godot::Variant &r_value
    ) override;
    bool supports_type(godot::Variant::Type type) const override;
    int bit_width(godot::Variant::Type type) const override;
    int stride(godot::Variant::Type type) const override;
    double max_error(godot::Variant::Type type) const override;
};

} // namespace netw
