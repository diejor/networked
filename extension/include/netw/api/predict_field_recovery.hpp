#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPredictionEngine;

class NetwPredictFieldRecovery : public godot::RefCounted {
    GDCLASS(NetwPredictFieldRecovery, godot::RefCounted)

    NetwPredictionEngine *pool = nullptr;
    int64_t slot = -1;
    godot::StringName key;

    int64_t count_at(int p_column) const;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictFieldRecovery> of(
        NetwPredictionEngine *p_pool,
        int64_t p_slot,
        const godot::StringName &p_key
    );

    godot::StringName get_field() const {
        return key;
    }

    int64_t get_triggered() const;
    int64_t get_repaired() const;
    int64_t get_contracted() const;
    int64_t get_carried() const;
    int64_t get_declined() const;
    int64_t get_infidelity() const;
};

} // namespace netw
