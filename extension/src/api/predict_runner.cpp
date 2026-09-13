#include "netw/api/predict_runner.hpp"

#include <algorithm>

#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/physics_stepper.hpp"
#include "netw/api/predict.hpp"
#include "netw/api/predict_slot_engine.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/predict/engine.hpp"
#include "netw/profile.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

struct OrderRow {
    int64_t key = 0;
    uint32_t seat = 0;
};

} // namespace

void NetwPredictRunner::seat_core(NetwMultiplayer *p_core) {
    core_id = gd::instance_id(p_core);
}

NetwMultiplayer *NetwPredictRunner::core() const {
    return Object::cast_to<NetwMultiplayer>(gd::instance_from_id(core_id));
}

int64_t NetwPredictRunner::index_of(NetwPredictSlotEngine *p_engine) const {
    for (uint32_t at = 0; at < engines.size(); ++at) {
        if (engines[at] == p_engine) {
            return int64_t(at);
        }
    }
    return -1;
}

int64_t NetwPredictRunner::engine_count() const {
    return int64_t(engines.size());
}

void NetwPredictRunner::register_engine(NetwPredictSlotEngine *p_engine) {
    if (p_engine == nullptr || index_of(p_engine) >= 0) {
        return;
    }
    engines.push_back(p_engine);
    sort_dirty = true;
}

void NetwPredictRunner::unregister_engine(NetwPredictSlotEngine *p_engine) {
    const int64_t at = index_of(p_engine);
    if (at < 0) {
        return;
    }
    engines.remove_at(uint32_t(at));
    sort_dirty = true;
}

bool NetwPredictRunner::resolve_pool_order() {
    NetwMultiplayer *service = core();
    if (service == nullptr) {
        return false;
    }
    NetwPredictionEngine *const pool = service->get_prediction_engine();
    if (pool == nullptr) {
        return false;
    }
    for (uint32_t at = 0; at < engines.size(); ++at) {
        NetwPredictSlotEngine *engine = engines[at];
        const int64_t slot = engine == nullptr
            ? -1
            : service->native_prediction_slot(engine->seated_entity());
        if (slot < 0) {
            NETW_TRACE(
                sys::PREDICTION,
                "predict runner: the pool cannot name engine %d of %d",
                int(at),
                int(engines.size())
            );
            by_slot.clear();
            return false;
        }
        by_slot[slot] = engines[at];
    }
    const PackedInt64Array order = pool->ordered_slots();
    for (int64_t at = 0; at < order.size(); ++at) {
        NetwPredictSlotEngine *const *held = by_slot.getptr(order[at]);
        if (held != nullptr && *held != nullptr) {
            sorted.push_back(*held);
        }
    }
    return sorted.size() == engines.size();
}

void NetwPredictRunner::rebuild_sorted() {
    NETW_ZONE_NC("predict runner rebuild order", colors::PREDICTION);
    by_slot.clear();
    sorted.clear();
    if (!resolve_pool_order()) {
        by_slot.clear();
        sorted.clear();
        LocalVector<OrderRow> rows;
        rows.reserve(engines.size());
        for (uint32_t at = 0; at < engines.size(); ++at) {
            NetwPredictSlotEngine *engine = engines[at];
            OrderRow row;
            row.key = engine == nullptr ? 0 : engine->order_key();
            row.seat = at;
            rows.push_back(row);
        }
        std::sort(
            rows.ptr(),
            rows.ptr() + rows.size(),
            [](const OrderRow &a, const OrderRow &b) {
                return a.key != b.key ? a.key < b.key : a.seat < b.seat;
            }
        );
        for (uint32_t at = 0; at < rows.size(); ++at) {
            sorted.push_back(engines[rows[at].seat]);
        }
    }
    sort_dirty = false;
}

const LocalVector<NetwPredictSlotEngine *> &NetwPredictRunner::phase(
    int p_phase
) {
    if (sort_dirty) {
        rebuild_sorted();
    }
    NetwMultiplayer *service = core();
    if (service == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict runner: pass %d has no session, stepping %d in key order",
            p_phase,
            int(sorted.size())
        );
        return sorted;
    }
    if (by_slot.size() != engines.size()) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict runner: pass %d over %d of %d named, stepping every one",
            p_phase,
            int(by_slot.size()),
            int(engines.size())
        );
        return sorted;
    }
    NetwPredictionEngine *const pool = service->get_prediction_engine();
    if (pool == nullptr) {
        NETW_TRACE(
            sys::PREDICTION,
            "predict runner: pass %d has no pool, stepping %d in key order",
            p_phase,
            int(sorted.size())
        );
        return sorted;
    }
    phase_roster.clear();
    const PackedInt64Array slots = pool->pass_slots(p_phase);
    for (int64_t at = 0; at < slots.size(); ++at) {
        NetwPredictSlotEngine *const *held = by_slot.getptr(slots[at]);
        if (held != nullptr && *held != nullptr) {
            phase_roster.push_back(*held);
        }
    }
    return phase_roster;
}

void NetwPredictRunner::tick_step(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict runner tick step", colors::PREDICTION);
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_ISLAND_TICK);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->prepare_island(int(NetwPredict::SCHEDULE_TICK));
            }
        }
    }
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_JOINT);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->joint_pass(p_timing);
            }
        }
    }
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_TICK);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->network_tick(p_timing);
            }
        }
    }
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_STEPPED);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->simulate_stepped(p_timing);
            }
        }
    }
    step_stepped_spaces(p_timing);
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_STEPPED);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->finalize_stepped_state();
            }
        }
    }
}

void NetwPredictRunner::step_stepped_spaces(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict runner step spaces", colors::PREDICTION);
    NetwMultiplayer *service = core();
    if (service == nullptr) {
        return;
    }
    LocalVector<int64_t> driven;
    for (uint32_t at = 0; at < engines.size(); ++at) {
        NetwPredictSlotEngine *engine = engines[at];
        if (engine == nullptr
            || !engine->uses_schedule(NetwPredict::SCHEDULE_STEPPED)) {
            continue;
        }
        const NetwMultiplayer::EntitySpace held
            = service->entity_space_of(engine->seated_entity());
        const Ref<NetwPhysicsStepper> stepper
            = service->predict_get_stepper(held.space);
        if (stepper.is_null() || driven.has(held.space.get_id())) {
            continue;
        }
        driven.push_back(held.space.get_id());
        service->predict_stepper_hold(held.space, held.dimension);
        stepper->step(held.space, p_timing.get_ticktime());
        stepper->snapshot(held.space, p_timing.get_tick());
    }
}

void NetwPredictRunner::frame_step(const NetwPredictTiming &p_timing) {
    NETW_ZONE_NC("predict runner frame step", colors::PREDICTION);
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_ISLAND_FRAME);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->prepare_island(int(NetwPredict::SCHEDULE_FRAME));
            }
        }
    }
    {
        const LocalVector<NetwPredictSlotEngine *> &roster
            = phase(NetwPredictionEngine::PASS_FRAME);
        for (uint32_t at = 0; at < roster.size(); ++at) {
            NetwPredictSlotEngine *engine = roster[at];
            if (engine != nullptr) {
                engine->simulate_frame(p_timing);
            }
        }
    }
}

void NetwPredictRunner::before_frame_step() {
    NETW_ZONE_NC("predict runner finalize frame", colors::PREDICTION);
    const LocalVector<NetwPredictSlotEngine *> &roster
        = phase(NetwPredictionEngine::PASS_FINALIZE_FRAME);
    for (uint32_t at = 0; at < roster.size(); ++at) {
        NetwPredictSlotEngine *engine = roster[at];
        if (engine != nullptr) {
            engine->finalize_frame_state();
        }
    }
}

} // namespace netw
