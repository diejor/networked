#include "netw/api/netw_multiplayer.hpp"

#if defined(DEBUG_ENABLED)

#include "godot/engine.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/predict_slot_engine.hpp"

namespace netw {

using namespace godot;

void NetwMultiplayer::diag_poll() {
    if (prediction_rings == nullptr) {
        if (prediction_rings_decided) {
            return;
        }
        prediction_rings_decided = true;
        if (!diag::PredictionRings::wanted()) {
            return;
        }
        prediction_rings = std::make_unique<diag::PredictionRings>();
        prediction_rings->attach_sketch();
    }
    const uint64_t frame = Engine::get_singleton()->get_process_frames();
    if (frame == diag_polled_frame) {
        return;
    }
    diag_polled_frame = frame;
    diag_gather(prediction_rings->sources);
    prediction_rings->poll(*this);
}

void NetwMultiplayer::diag_gather(LocalVector<diag::RingSource> &r_sources
) const {
    r_sources.clear();
    r_sources.reserve(predict_engines.size());
    for (const KeyValue<int64_t, NetwPredictSlotEngine *> &row :
         predict_engines) {
        NetwPredictSlotEngine *engine = row.value;
        NetwEntity *entity = engine != nullptr ? engine->entity.ptr() : nullptr;
        Node *owner = entity != nullptr ? entity->get_owner() : nullptr;
        const int64_t slot = engine != nullptr ? engine->native_slot() : -1;
        if (owner == nullptr || slot < 0) {
            continue;
        }
        diag::RingSource source;
        source.key = row.key;
        source.entity = entity;
        source.slot = slot;
        source.owner = owner;
        r_sources.push_back(source);
    }
}

} // namespace netw

#endif
