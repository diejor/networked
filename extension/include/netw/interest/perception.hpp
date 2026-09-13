#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/interest/decl.hpp"
#include "netw/interest/engine.hpp"

namespace netw::interest {

class Perception {
public:
    enum Policy {
        HIDE = 0,
        SHOW = 1,
        CUSTOM = 2,
    };

private:
    godot::HashMap<int64_t, bool> visible;
    godot::HashMap<int64_t, godot::Array> armed;

public:
    bool set_visible(int64_t key, bool p_visible);

    bool is_known(int64_t key) const;
    void forget(int64_t key);
    void clear();

    void arm(int64_t key, const godot::Array &p_actions);
    godot::Array disarm(int64_t key);
    godot::PackedInt64Array armed_keys() const;

    godot::Dictionary resolve(
        const godot::Array &p_layers,
        const Decl *p_decl,
        const Engine &p_engine
    ) const;
};

} // namespace netw::interest
