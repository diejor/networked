#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw::spawn {

class SpawnerRoster {
private:
    godot::HashMap<int64_t, godot::ObjectID> spawners;
    godot::HashMap<int64_t, godot::ObjectID> producers;

public:
    bool enrol(godot::Object *spawner);

    static int scene_index_for(godot::Object *spawner, godot::Node *node);

    godot::LocalVector<godot::Object *> live();

    int size() const;

    void note_producer(int64_t route, godot::Object *spawner);

    godot::Node *take_producer(int64_t route);

    int produced_count(godot::Object *spawner) const;

    void clear();
};

} // namespace netw::spawn
