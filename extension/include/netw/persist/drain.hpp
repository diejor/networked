#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw {
class NetwMultiplayer;
} // namespace netw

namespace netw::persist {

class Drain {
    godot::ObjectID session_id;
    godot::TypedArray<godot::Object> engines;
    godot::LocalVector<godot::ObjectID> databases;
    int32_t engine_at = 0;
    uint32_t database_at = 0;

    netw::NetwMultiplayer *session() const;
    void note_database(const godot::Variant &p_database);
    bool flush_next_engine(const godot::Callable &p_resume);
    bool drain_next_database(const godot::Callable &p_resume);

public:
    void open(netw::NetwMultiplayer *p_session);

    bool advance(const godot::Callable &p_resume);
};

} // namespace netw::persist
