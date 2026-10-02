#pragma once

#include "netw_test.h"

#include "godot/class_db.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/physics_stepper.hpp"

namespace netw_test {

class RecordingStepper : public netw::NetwPhysicsStepper {
    GDCLASS(RecordingStepper, netw::NetwPhysicsStepper)

public:
    enum Call : int {
        STEP = 0,
        SNAPSHOT = 1,
        RESTORE = 2,
    };

    struct Row {
        Call call = STEP;
        godot::RID space;
        int64_t tick = -1;
        double delta = 0.0;
    };

private:
    godot::LocalVector<Row> rows;
    bool willing = true;

protected:
    static void _bind_methods() {
    }

public:
    bool can_step() override {
        return willing;
    }

    void step(const godot::RID &p_space, double p_delta) override {
        rows.push_back({STEP, p_space, -1, p_delta});
    }

    void snapshot(const godot::RID &p_space, int64_t p_tick) override {
        rows.push_back({SNAPSHOT, p_space, p_tick, 0.0});
    }

    void restore(const godot::RID &p_space, int64_t p_tick) override {
        rows.push_back({RESTORE, p_space, p_tick, 0.0});
    }

    void refuse() {
        willing = false;
    }

    void forget() {
        rows.clear();
    }

    const godot::LocalVector<Row> &recorded() const {
        return rows;
    }

    int count_of(Call p_call) const {
        int seen = 0;
        for (uint32_t at = 0; at < rows.size(); ++at) {
            seen += rows[at].call == p_call ? 1 : 0;
        }
        return seen;
    }

    int advancing_steps() const {
        int seen = 0;
        int64_t newest = INT64_MIN;
        for (uint32_t at = 0; at + 1 < rows.size(); ++at) {
            if (rows[at].call != STEP || rows[at + 1].call != SNAPSHOT) {
                continue;
            }
            if (rows[at + 1].tick > newest) {
                newest = rows[at + 1].tick;
                seen += 1;
            }
        }
        return seen;
    }

    int replayed_steps() const {
        return count_of(STEP) - advancing_steps();
    }

    int64_t first_tick_of(Call p_call) const {
        for (uint32_t at = 0; at < rows.size(); ++at) {
            if (rows[at].call == p_call) {
                return rows[at].tick;
            }
        }
        return -1;
    }

    bool every_step_is_snapshotted() const {
        if (rows.is_empty()) {
            return false;
        }
        for (uint32_t at = 0; at < rows.size(); ++at) {
            if (rows[at].call != STEP) {
                continue;
            }
            if (at + 1 >= rows.size() || rows[at + 1].call != SNAPSHOT) {
                return false;
            }
        }
        return true;
    }
};

} // namespace netw_test
