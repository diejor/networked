#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "netw/api/prediction.hpp"
#include "netw/prediction_core.hpp"

namespace netw {

class NetwPredictFold : public godot::RefCounted {
    GDCLASS(NetwPredictFold, godot::RefCounted)

    Fold decided;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPredictFold> of(
        int64_t p_label,
        bool p_fresh,
        NetwPrediction::DriveKind p_kind
    );

    int64_t label() const {
        return decided.label;
    }

    bool fresh() const {
        return decided.fresh;
    }

    NetwPrediction::DriveKind kind() const {
        return static_cast<NetwPrediction::DriveKind>(int(decided.kind));
    }
};

namespace prediction_core {

godot::Ref<NetwPredictFold> predict_fold(
    int64_t latest_input_tick,
    int64_t last_driven_input_tick,
    int64_t frame_tick
);

} // namespace prediction_core

} // namespace netw
