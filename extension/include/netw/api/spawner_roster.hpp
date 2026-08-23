#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwSpawnerRoster : public godot::RefCounted {
    GDCLASS(NetwSpawnerRoster, godot::RefCounted)

private:
    godot::HashMap<int64_t, godot::ObjectID> spawners;
    godot::HashMap<int64_t, godot::ObjectID> producers;

protected:
    static void _bind_methods();

public:
    bool enrol(godot::Object *spawner);

    godot::Array live();

    int size() const;

    void note_producer(int64_t route, godot::Object *spawner);

    godot::Object *take_producer(int64_t route);

    int produced_count(godot::Object *spawner) const;

    void clear();
};

} // namespace netw
