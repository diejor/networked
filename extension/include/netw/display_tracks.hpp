#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwDisplayTracks : public RefCounted {
    GDCLASS(NetwDisplayTracks, RefCounted)

private:
    HashMap<StringName, int32_t> key_index;
    HashMap<StringName, int32_t> name_index;
    HashSet<StringName> ambiguous;
    int32_t channels = 0;

protected:
    static void _bind_methods();

public:
    int declare(const StringName &key, const StringName &name);
    int by_key(const StringName &key) const;
    int by_name(const StringName &name) const;
    bool is_ambiguous(const StringName &name) const;
    int ambiguous_count() const;
    int size() const;
    void clear();
};

} // namespace netw
