#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/interest/decl.hpp"
#include "netw/interest/engine.hpp"

namespace netw::interest {

class Leave {
public:
    using LayerExits = godot::LocalVector<godot::StringName>;
    using PerPeer = godot::HashMap<int64_t, LayerExits>;

private:
    godot::HashMap<int64_t, PerPeer> pending;
    godot::HashMap<int64_t, godot::HashSet<int64_t>> retained;

public:
    void record(int64_t key, int64_t peer, const godot::StringName &layer_id);

    godot::Array pending_layers(int64_t key, int64_t peer) const;

    void finish_sweep();

    bool is_retained(int64_t key, int64_t peer) const;
    void release(int64_t key, int64_t peer);

    void forget_entity(int64_t key);
    void forget_peer(int64_t peer);
    void clear();

    godot::Dictionary resolve(
        int64_t key,
        int64_t peer,
        const Decl *p_decl,
        const Engine &p_engine
    ) const;

    void commit(
        int64_t key,
        int64_t peer,
        const godot::Dictionary &p_decision,
        bool p_forced
    );
};

} // namespace netw::interest
