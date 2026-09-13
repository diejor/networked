#include "netw/api/debug_join_config.hpp"

#include "godot/class_db.hpp"
#include "godot/object.hpp"

using namespace godot;

namespace netw {

void DebugJoinConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_username", "username"),
        &DebugJoinConfig::set_username
    );
    ClassDB::bind_method(
        D_METHOD("get_username"),
        &DebugJoinConfig::get_username
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "username"),
        "set_username",
        "get_username"
    );

    ClassDB::bind_method(
        D_METHOD("set_join_args", "join_args"),
        &DebugJoinConfig::set_join_args
    );
    ClassDB::bind_method(
        D_METHOD("get_join_args"),
        &DebugJoinConfig::get_join_args
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::ARRAY, "join_args"),
        "set_join_args",
        "get_join_args"
    );
}

void DebugJoinConfig::set_join_args(const Array &p_join_args) {
    join_args = p_join_args.duplicate(true);
}

Array DebugJoinConfig::get_join_args() const {
    return join_args.duplicate(true);
}

} // namespace netw
