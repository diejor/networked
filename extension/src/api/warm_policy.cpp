#include "netw/api/warm_policy.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"

using namespace godot;

namespace netw {

void WarmRequest::_bind_methods() {
    ClassDB::bind_static_method(
        "WarmRequest",
        D_METHOD("none"),
        &WarmRequest::none
    );
    ClassDB::bind_static_method(
        "WarmRequest",
        D_METHOD("all"),
        &WarmRequest::all
    );
    ClassDB::bind_static_method(
        "WarmRequest",
        D_METHOD("ids", "values"),
        &WarmRequest::ids
    );
    ClassDB::bind_static_method(
        "WarmRequest",
        D_METHOD("filter", "map"),
        &WarmRequest::filter
    );

    ClassDB::bind_method(D_METHOD("set_kind", "kind"), &WarmRequest::set_kind);
    ClassDB::bind_method(D_METHOD("get_kind"), &WarmRequest::get_kind);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::INT,
            "kind",
            PROPERTY_HINT_ENUM,
            "None,All,Ids,Filter"
        ),
        "set_kind",
        "get_kind"
    );

    ClassDB::bind_method(
        D_METHOD("set_id_list", "id_list"),
        &WarmRequest::set_id_list
    );
    ClassDB::bind_method(D_METHOD("get_id_list"), &WarmRequest::get_id_list);
    ADD_PROPERTY(
        PropertyInfo(
            Variant::ARRAY,
            "id_list",
            PROPERTY_HINT_ARRAY_TYPE,
            "StringName"
        ),
        "set_id_list",
        "get_id_list"
    );

    ClassDB::bind_method(
        D_METHOD("set_filter_map", "filter_map"),
        &WarmRequest::set_filter_map
    );
    ClassDB::bind_method(
        D_METHOD("get_filter_map"),
        &WarmRequest::get_filter_map
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::DICTIONARY, "filter_map"),
        "set_filter_map",
        "get_filter_map"
    );

    BIND_ENUM_CONSTANT(KIND_NONE);
    BIND_ENUM_CONSTANT(KIND_ALL);
    BIND_ENUM_CONSTANT(KIND_IDS);
    BIND_ENUM_CONSTANT(KIND_FILTER);
}

Ref<WarmRequest> WarmRequest::none() {
    return Ref<WarmRequest>(memnew(WarmRequest));
}

Ref<WarmRequest> WarmRequest::all() {
    Ref<WarmRequest> request(memnew(WarmRequest));
    request->set_kind(KIND_ALL);
    return request;
}

Ref<WarmRequest> WarmRequest::ids(const Array &values) {
    Ref<WarmRequest> request(memnew(WarmRequest));
    request->set_kind(KIND_IDS);
    TypedArray<StringName> names;
    for (int at = 0; at < values.size(); ++at) {
        names.push_back(StringName(values[at]));
    }
    request->set_id_list(names);
    return request;
}

Ref<WarmRequest> WarmRequest::filter(const Dictionary &map) {
    Ref<WarmRequest> request(memnew(WarmRequest));
    request->set_kind(KIND_FILTER);
    request->set_filter_map(map.duplicate());
    return request;
}

void WarmPolicy::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("plan_table", "table", "columns"),
        &WarmPolicy::plan_table
    );
    GDVIRTUAL_BIND(_plan_table, "table", "columns");
}

Ref<WarmRequest> WarmPolicy::plan_table(
    const StringName &table,
    const TypedArray<StringName> &columns
) {
    Ref<WarmRequest> planned;
    GDVIRTUAL_CALL(_plan_table, table, columns, planned);
    if (planned.is_valid()) {
        return planned;
    }
    return WarmRequest::all();
}

} // namespace netw
