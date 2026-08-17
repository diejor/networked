#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/interest_decl.hpp"
#include "netw/interest_engine.hpp"

namespace netw {

class NetwInterestPerception : public godot::RefCounted {
    GDCLASS(NetwInterestPerception, godot::RefCounted)

public:
    enum Policy {
        HIDE = 0,
        SHOW = 1,
        CUSTOM = 2,
    };

private:
    godot::HashMap<int64_t, bool> visible;
    godot::HashMap<int64_t, godot::Array> armed;

protected:
    static void _bind_methods();

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
        const godot::Ref<NetwInterestDecl> &p_decl,
        const godot::Ref<NetwInterestEngine> &p_engine
    ) const;
};

} // namespace netw
