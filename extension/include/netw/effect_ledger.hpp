#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwEffectLedger {
    struct Entry {
        godot::Callable revert;
        int64_t deadline_tick = 0;
    };

    struct Watcher {
        godot::Callable confirmed;
        godot::Callable denied;
    };

    godot::HashMap<godot::StringName, Entry> entries;
    godot::HashMap<godot::StringName, Watcher> watchers;

    void resolve(const godot::StringName &p_key, bool p_keep);

public:
    void arm(
        const godot::StringName &p_key,
        const godot::Callable &p_revert,
        int64_t p_deadline_tick
    );
    bool watch(
        const godot::StringName &p_key,
        const godot::Callable &p_confirmed,
        const godot::Callable &p_denied
    );
    void adopt(const godot::StringName &p_key);
    void discard(const godot::StringName &p_key);
    bool pending(const godot::StringName &p_key) const;
    int64_t count() const;
    void sweep(int64_t p_tick);
    void clear();
};

} // namespace netw
