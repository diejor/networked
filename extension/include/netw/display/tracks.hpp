#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/held_bytes.hpp"

namespace netw::display {

class Tracks {
private:
    godot::LocalVector<godot::StringName> keys;
    godot::LocalVector<godot::StringName> names;

    int first_named(const godot::StringName &name) const;
    int named_count(const godot::StringName &name) const;

public:
    int declare(const godot::StringName &key, const godot::StringName &name);
    int by_key(const godot::StringName &key) const;
    int by_name(const godot::StringName &name) const;
    bool is_ambiguous(const godot::StringName &name) const;
    int ambiguous_count() const;
    int size() const;
    void clear();

    int64_t held_bytes() const {
        return held::vector(keys) + held::vector(names);
    }
};

} // namespace netw::display
