#pragma once

#include "godot/class_db.hpp"
#include "godot/gdvirtual.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/serde.hpp"

namespace netw {

class NetwRecord : public Serde {
    GDCLASS(NetwRecord, Serde)

    godot::TypedArray<godot::StringName> iter_keys;
    int iter_index = 0;

protected:
    static void _bind_methods();

    virtual bool reserves_property(const godot::StringName &property) const;

public:
    virtual void set_value(
        const godot::StringName &property,
        const godot::Variant &value
    );
    virtual godot::Variant get_value(
        const godot::StringName &property,
        const godot::Variant &fallback = godot::Variant()
    ) const;
    virtual bool has_value(const godot::StringName &property) const;
    virtual godot::TypedArray<godot::StringName> get_property_names() const;

    bool is_empty() const;
    virtual godot::Dictionary to_dict() const;
    virtual void from_dict(const godot::Dictionary &data);

    bool iterate_init(const godot::Array &cursor);
    bool iterate_next(const godot::Array &cursor);
    godot::Variant iterate_get(const godot::Variant &cursor) const;

    bool _get(const godot::StringName &property, godot::Variant &r_ret) const;
    bool _set(const godot::StringName &property, const godot::Variant &value);
    void _get_property_list(godot::List<godot::PropertyInfo> *p_list) const;

    GDVIRTUAL2(_set_value, const godot::StringName &, const godot::Variant &)
    GDVIRTUAL2RC(
        godot::Variant,
        _get_value,
        const godot::StringName &,
        const godot::Variant &
    )
    GDVIRTUAL1RC(bool, _has_value, const godot::StringName &)
    GDVIRTUAL0RC(godot::TypedArray<godot::StringName>, _get_property_names)
    GDVIRTUAL0RC(godot::Dictionary, _to_dict)
    GDVIRTUAL1(_from_dict, const godot::Dictionary &)
};

class DictionaryRecord : public NetwRecord {
    GDCLASS(DictionaryRecord, NetwRecord)

    godot::Dictionary data;

protected:
    static void _bind_methods();

    bool reserves_property(const godot::StringName &property) const override;

public:
    DictionaryRecord();

    void set_data(const godot::Dictionary &values);
    godot::Dictionary get_data() const;

    godot::PackedByteArray serialize() override;
    void deserialize(const godot::PackedByteArray &bytes) override;

    void set_value(
        const godot::StringName &property,
        const godot::Variant &value
    ) override;
    godot::Variant get_value(
        const godot::StringName &property,
        const godot::Variant &fallback = godot::Variant()
    ) const override;
    bool has_value(const godot::StringName &property) const override;
    godot::TypedArray<godot::StringName> get_property_names() const override;
};

} // namespace netw
