#include "netw/api/staged_writes.hpp"

#include "godot/class_db.hpp"

namespace netw {

using namespace godot;

bool NetwStagedWrites::is_valid() const {
    return !keys.is_empty() && keys.size() == values.size();
}

Dictionary NetwStagedWrites::header() const {
    Dictionary out;
    out[StringName("ordinal")] = ordinal;
    out[StringName("tick")] = tick;
    out[StringName("ack")] = ack;
    out[StringName("payload")] = row;
    out[StringName("whole")] = whole;
    out[StringName("samples")] = samples;
    return out;
}

void NetwStagedWrites::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_valid"), &NetwStagedWrites::is_valid);
    ClassDB::bind_method(D_METHOD("header"), &NetwStagedWrites::header);

#define NETW_STAGED_PROPERTY(m_type, m_name)                                   \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwStagedWrites::get_##m_name                                        \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwStagedWrites::set_##m_name                                        \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

    NETW_STAGED_PROPERTY(Variant::INT, ordinal);
    NETW_STAGED_PROPERTY(Variant::INT, tick);
    NETW_STAGED_PROPERTY(Variant::INT, ack);
    NETW_STAGED_PROPERTY(Variant::ARRAY, keys);
    NETW_STAGED_PROPERTY(Variant::ARRAY, values);
    NETW_STAGED_PROPERTY(Variant::DICTIONARY, row);
    NETW_STAGED_PROPERTY(Variant::BOOL, whole);
    NETW_STAGED_PROPERTY(Variant::ARRAY, samples);

#undef NETW_STAGED_PROPERTY
}

} // namespace netw
