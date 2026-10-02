#pragma once

#include "godot/variant.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/engine.hpp"

namespace netw_test {

inline netw::predict::StateRow input_row(
    const netw::NetwPredictionEngine *p_pool,
    int64_t p_slot,
    const godot::Dictionary &p_values
) {
    netw::predict::StateRow out;
    const netw::predict::FieldCodec *codec = p_pool->input_codec_of(p_slot);
    if (codec != nullptr) {
        netw::predict::read_payload(*codec, p_values, out);
    }
    return out;
}

inline netw::predict::StateRow state_row(
    const netw::NetwPredictionEngine *p_pool,
    int64_t p_slot,
    const godot::Dictionary &p_values
) {
    netw::predict::StateRow out;
    const netw::predict::Wiring *wiring = p_pool->wiring_of(p_slot);
    if (wiring != nullptr) {
        netw::predict::read_payload(*wiring, p_values, out);
    }
    return out;
}

inline godot::Dictionary predicted_state(
    const netw::NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_key
) {
    netw::predict::StateRow read;
    const netw::predict::Wiring *wiring = p_pool->wiring_of(p_slot);
    if (wiring == nullptr || !p_pool->read_predicted(p_slot, p_key, read)) {
        return godot::Dictionary();
    }
    return netw::predict::payload_of(wiring->codec, read);
}

inline godot::Dictionary input_at(
    const netw::NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_key
) {
    netw::predict::StateRow read;
    const netw::predict::FieldCodec *codec = p_pool->input_codec_of(p_slot);
    if (codec == nullptr || !p_pool->read_command_at(p_slot, p_key, read)) {
        return godot::Dictionary();
    }
    return netw::predict::payload_of(*codec, read);
}

} // namespace netw_test
