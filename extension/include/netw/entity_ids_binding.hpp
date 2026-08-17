#pragma once

#include "godot/object.hpp"
#include "godot/rid.hpp"

namespace netw {

class NetwEntityIds : public godot::Object {
    GDCLASS(NetwEntityIds, godot::Object)

protected:
    static void _bind_methods();

public:
    static godot::RID mint();
    static bool is_minted(const godot::RID &p_entity);
    static bool retain(const godot::RID &p_entity);
    static void release(const godot::RID &p_entity);
    static int holders(const godot::RID &p_entity);
    static int outstanding();
};

} // namespace netw
