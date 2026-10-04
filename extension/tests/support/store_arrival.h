#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/predict/arrival.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/store/world.hpp"

namespace netw_test {

struct LandedLog {
    int64_t count = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    godot::LocalVector<godot::StringName> keys;
    godot::LocalVector<godot::Variant> values;

    static void note(
        void *p_context,
        const netw::store::Table &p_table,
        uint32_t p_row
    ) {
        LandedLog *log = static_cast<LandedLog *>(p_context);
        log->count += 1;
        log->tick = p_table.arrived_tick;
        log->ack = p_table.arrived_ack;
        log->keys.clear();
        log->values.clear();
        for (int column = 0; column < p_table.schema.column_count(); ++column) {
            log->keys.push_back(p_table.schema.at(column)->key);
            log->values.push_back(p_table.read_value(p_row, uint32_t(column)));
        }
    }

    void watch(const netw::repl::PropertyBindingPtr &p_binding) {
        p_binding->on_landed = &LandedLog::note;
        p_binding->landed_context = this;
    }

    godot::Variant value(const godot::StringName &p_key) const {
        for (uint32_t at = 0; at < keys.size(); ++at) {
            if (keys[at] == p_key) {
                return values[at];
            }
        }
        return godot::Variant();
    }
};

struct StoreArrival {
    godot::Ref<netw::NetwMultiplayer> session;
    netw::repl::PropertyBindingPtr binding;
    int sender = 2;
    int64_t ordinal = 0;

    explicit StoreArrival(const netw::repl::PropertyBindingPtr &p_binding)
        : binding(p_binding) {
        session.instantiate();
        session->store_admit(binding);
    }

    netw::store::World &world() {
        return session->store();
    }

    netw::store::RowAddress at(uint32_t p_lane) {
        return world().row_of(binding->id, p_lane);
    }

    netw::store::Landing land(
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        bool p_windowed,
        uint32_t p_lane = 0
    ) {
        return world()
            .receive(at(p_lane), sender, p_frame, p_base_tick, p_windowed);
    }

    bool settle(
        const godot::PackedByteArray &p_frame,
        int64_t p_base_tick,
        bool p_windowed,
        uint32_t p_lane = 0
    ) {
        const netw::store::RowAddress row = at(p_lane);
        const bool retained = p_lane == 1;
        if (!row.valid()
            || !binding->admits_arrival(
                world().table(row.table).schema,
                ordinal,
                retained
            )) {
            return false;
        }
        if (land(p_frame, p_base_tick, p_windowed, p_lane)
            != netw::store::Landing::ACCEPTED) {
            return false;
        }
        binding->settle_arrival();
        if (!retained) {
            binding->notify_landed(world().table(row.table), row.row);
        }
        return true;
    }

    netw::predict::Arrival arrival(uint32_t p_lane = 0) {
        const netw::store::RowAddress row = at(p_lane);
        return netw::predict::Arrival::of(world().table(row.table), row.row);
    }

    godot::Variant value(const godot::StringName &p_key, uint32_t p_lane = 0) {
        const netw::store::RowAddress row = at(p_lane);
        const netw::store::Table &table = world().table(row.table);
        for (int column = 0; column < table.schema.column_count(); ++column) {
            if (table.schema.at(column)->key == p_key) {
                return table.read_value(row.row, uint32_t(column));
            }
        }
        return godot::Variant();
    }
};

} // namespace netw_test
