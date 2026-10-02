#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/pump_stats.hpp"
#include "netw/display/runtime.hpp"

namespace netw::display {

class Book : public godot::RefCounted {
private:
    struct Row {
        int64_t route = 0;
        Runtime *runtime = nullptr;
        Decl decl;
    };

    godot::HashMap<godot::RID, Row> rows;
    godot::HashMap<int64_t, godot::RID> by_route;
    godot::LocalVector<godot::RID> order;
    godot::LocalVector<godot::RID> dirty;
    godot::LocalVector<Runtime *> live;
    godot::LocalVector<Runtime *> pumping;
    godot::LocalVector<Runtime *> recording;
    PumpStats stats;
    godot::Callable went_dirty;

    Row &row_for(const godot::RID &p_entity);
    void forget(const godot::RID &p_entity);
    void release(Runtime *p_runtime);

public:
    ~Book();

    void set_went_dirty(const godot::Callable &callback);

    void enroll(const godot::RID &p_entity, int64_t p_route);

    Runtime *open_runtime(const godot::RID &p_entity);
    Runtime *runtime_of(const godot::RID &p_entity) const;
    Runtime *runtime_at(int64_t p_route) const;
    godot::LocalVector<Runtime *> runtimes() const;

    void wake(Runtime *p_runtime);
    void wake_route(int64_t p_route);
    void wake_all();
    godot::LocalVector<Runtime *> &pump_set() {
        return pumping;
    }
    godot::LocalVector<Runtime *> &record_set() {
        return recording;
    }

    void set_decl(const godot::RID &p_entity, const Decl &p_decl);
    Decl decl_of(const godot::RID &p_entity) const;
    Decl *decl_ptr(const godot::RID &p_entity);

    PumpStats &get_stats();
    const PumpStats &get_stats() const;

    godot::RID entity_at(int64_t p_route) const;
    int64_t route_of(const godot::RID &p_entity) const;

    void mark_dirty(const godot::RID &p_entity, int p_dirt);
    int write_param(
        const godot::RID &p_entity,
        Decl &p_fallback,
        int p_param,
        const godot::Variant &p_value
    );
    void clear_dirty(const godot::RID &p_entity);
    bool is_dirty(const godot::RID &p_entity) const;
    godot::TypedArray<godot::RID> take_dirty();

    void drop(const godot::RID &p_entity);
    void drop_route(int64_t p_route);
    void clear();
    int size() const;

    int64_t held_bytes() const {
        int64_t bytes = held::map(rows) + held::map(by_route)
            + held::vector(order) + held::vector(dirty) + held::vector(live)
            + held::vector(pumping) + held::vector(recording);
        for (const auto &pair : rows) {
            if (pair.value.runtime != nullptr) {
                bytes += pair.value.runtime->held_bytes();
            }
        }
        return bytes;
    }
};

} // namespace netw::display

VARIANT_ENUM_CAST(netw::display::Dirt);
