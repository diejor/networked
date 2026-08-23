#include "netw/api/scene_mark.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwSceneMark::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("is_deny_default"),
        &NetwSceneMark::is_deny_default
    );
    ClassDB::bind_method(
        D_METHOD("deadline_or", "fallback"),
        &NetwSceneMark::deadline_or
    );

#define NETW_SCENE_MARK_PROPERTY(m_type, m_name)                               \
    ClassDB::bind_method(                                                      \
        D_METHOD("set_" #m_name, #m_name),                                     \
        &NetwSceneMark::set_##m_name                                           \
    );                                                                         \
    ClassDB::bind_method(                                                      \
        D_METHOD("get_" #m_name),                                              \
        &NetwSceneMark::get_##m_name                                           \
    );                                                                         \
    ADD_PROPERTY(                                                              \
        PropertyInfo(m_type, #m_name),                                         \
        "set_" #m_name,                                                        \
        "get_" #m_name                                                         \
    )

    NETW_SCENE_MARK_PROPERTY(Variant::BOOL, marked);
    NETW_SCENE_MARK_PROPERTY(Variant::BOOL, gated);
    NETW_SCENE_MARK_PROPERTY(Variant::BOOL, session_wide);
    NETW_SCENE_MARK_PROPERTY(Variant::BOOL, captured);
    NETW_SCENE_MARK_PROPERTY(Variant::STRING_NAME, pending_method);
    NETW_SCENE_MARK_PROPERTY(Variant::FLOAT, deadline);

#undef NETW_SCENE_MARK_PROPERTY
}

} // namespace netw
