#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/resource.hpp"
#include "godot/variant.hpp"
#include "netw/api/bit_buffer.hpp"

namespace netw {

class NetwQuantize : public godot::Resource {
    GDCLASS(NetwQuantize, godot::Resource)

protected:
    static void _bind_methods();

    GDVIRTUAL2(_write, godot::Ref<NetwBitBufferWriter>, godot::Variant)
    GDVIRTUAL2R(godot::Variant, _read, godot::Ref<NetwBitBufferReader>, int)
    GDVIRTUAL1RC(bool, _supports_type, int)
    GDVIRTUAL1RC(int, _bit_width, int)
    GDVIRTUAL1RC(double, _max_error, int)

public:
    virtual void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    );
    virtual godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    );
    virtual bool supports_type(int type) const;
    virtual int bit_width(int type) const;
    virtual double max_error(int type) const;
    bool is_same_layout(const godot::Ref<NetwQuantize> &other) const;
};

class NetwQuantizeBits : public NetwQuantize {
    GDCLASS(NetwQuantizeBits, NetwQuantize)

    int bit_count = 8;
    double min_limit = -1.0;
    double max_limit = 1.0;

    int levels() const;
    void encode_axis(
        const godot::Ref<NetwBitBufferWriter> &writer,
        double value
    ) const;
    double decode_axis(const godot::Ref<NetwBitBufferReader> &reader) const;

protected:
    static void _bind_methods();

public:
    void set_bit_count(int value);
    int get_bit_count() const;
    void set_min_limit(double value);
    double get_min_limit() const;
    void set_max_limit(double value);
    double get_max_limit() const;
    godot::Ref<NetwQuantizeBits> bits(int value);
    godot::Ref<NetwQuantizeBits> limits(double minimum, double maximum);
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
};

class NetwQuantizeFixed : public NetwQuantize {
    GDCLASS(NetwQuantizeFixed, NetwQuantize)

    double resolution_step = 0.5;
    double min_limit = -2048.0;
    double max_limit = 2048.0;

    int grid_bits() const;
    void encode_axis(
        const godot::Ref<NetwBitBufferWriter> &writer,
        double value
    ) const;
    double decode_axis(const godot::Ref<NetwBitBufferReader> &reader) const;

protected:
    static void _bind_methods();

public:
    void set_resolution_step(double value);
    double get_resolution_step() const;
    void set_min_limit(double value);
    double get_min_limit() const;
    void set_max_limit(double value);
    double get_max_limit() const;
    godot::Ref<NetwQuantizeFixed> step(double value);
    godot::Ref<NetwQuantizeFixed> limits(double minimum, double maximum);
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
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
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
};

class NetwQuantizeQuaternion : public NetwQuantize {
    GDCLASS(NetwQuantizeQuaternion, NetwQuantize)

    int bit_count = 10;

    void encode_component(
        const godot::Ref<NetwBitBufferWriter> &writer,
        double value
    ) const;
    double decode_component(
        const godot::Ref<NetwBitBufferReader> &reader
    ) const;

protected:
    static void _bind_methods();

public:
    void set_bit_count(int value);
    int get_bit_count() const;
    godot::Ref<NetwQuantizeQuaternion> bits(int value);
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
};

class NetwQuantizeTransform2D : public NetwQuantize {
    GDCLASS(NetwQuantizeTransform2D, NetwQuantize)

    godot::Ref<NetwQuantize> origin_quantizer;
    godot::Ref<NetwQuantize> rotation_quantizer;
    godot::Ref<NetwQuantize> scale_quantizer;

protected:
    static void _bind_methods();

public:
    NetwQuantizeTransform2D();
    void set_origin_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_origin_quantizer() const;
    void set_rotation_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_rotation_quantizer() const;
    void set_scale_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_scale_quantizer() const;
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
};

class NetwQuantizeTransform3D : public NetwQuantize {
    GDCLASS(NetwQuantizeTransform3D, NetwQuantize)

    godot::Ref<NetwQuantize> origin_quantizer;
    godot::Ref<NetwQuantize> rotation_quantizer;
    godot::Ref<NetwQuantize> scale_quantizer;

protected:
    static void _bind_methods();

public:
    NetwQuantizeTransform3D();
    void set_origin_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_origin_quantizer() const;
    void set_rotation_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_rotation_quantizer() const;
    void set_scale_quantizer(const godot::Ref<NetwQuantize> &value);
    godot::Ref<NetwQuantize> get_scale_quantizer() const;
    void write(
        const godot::Ref<NetwBitBufferWriter> &writer,
        const godot::Variant &value
    ) override;
    godot::Variant read(
        const godot::Ref<NetwBitBufferReader> &reader,
        int type
    ) override;
    bool supports_type(int type) const override;
    int bit_width(int type) const override;
    double max_error(int type) const override;
};

} // namespace netw
