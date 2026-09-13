#pragma once

#include "godot/gdvirtual.hpp"
#include "godot/ref_counted.hpp"
#include "godot/resource.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {

class WarmRequest : public godot::RefCounted {
    GDCLASS(WarmRequest, godot::RefCounted)

public:
    enum Kind {
        KIND_NONE,
        KIND_ALL,
        KIND_IDS,
        KIND_FILTER,
    };

private:
    Kind kind = KIND_NONE;
    godot::TypedArray<godot::StringName> id_list;
    godot::Dictionary filter_map;

protected:
    static void _bind_methods();

public:
    static godot::Ref<WarmRequest> none();
    static godot::Ref<WarmRequest> all();
    static godot::Ref<WarmRequest> ids(const godot::Array &values);
    static godot::Ref<WarmRequest> filter(const godot::Dictionary &map);

    void set_kind(Kind p_kind) {
        kind = p_kind;
    }
    Kind get_kind() const {
        return kind;
    }

    void set_id_list(const godot::TypedArray<godot::StringName> &p_id_list) {
        id_list = p_id_list;
    }
    godot::TypedArray<godot::StringName> get_id_list() const {
        return id_list;
    }

    void set_filter_map(const godot::Dictionary &p_filter_map) {
        filter_map = p_filter_map;
    }
    godot::Dictionary get_filter_map() const {
        return filter_map;
    }
};

class WarmPolicy : public godot::Resource {
    GDCLASS(WarmPolicy, godot::Resource)

protected:
    static void _bind_methods();

    GDVIRTUAL2R(
        godot::Ref<WarmRequest>,
        _plan_table,
        godot::StringName,
        godot::TypedArray<godot::StringName>
    )

public:
    virtual godot::Ref<WarmRequest> plan_table(
        const godot::StringName &table,
        const godot::TypedArray<godot::StringName> &columns
    );
};

} // namespace netw

VARIANT_ENUM_CAST(netw::WarmRequest::Kind);
