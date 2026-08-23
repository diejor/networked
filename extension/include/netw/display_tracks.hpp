#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwDisplayTracks : public godot::RefCounted {
    GDCLASS(NetwDisplayTracks, godot::RefCounted)

private:
    godot::HashMap<godot::StringName, int32_t> key_index;
    godot::HashMap<godot::StringName, int32_t> name_index;
    godot::HashSet<godot::StringName> ambiguous;
    int32_t channels = 0;

protected:
    static void _bind_methods();

public:
    int declare(const godot::StringName &key, const godot::StringName &name);
    int by_key(const godot::StringName &key) const;
    int by_name(const godot::StringName &name) const;
    bool is_ambiguous(const godot::StringName &name) const;
    int ambiguous_count() const;
    int size() const;
    void clear();
};

} // namespace netw
