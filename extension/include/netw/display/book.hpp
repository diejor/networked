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
    PumpStats stats;
    godot::Callable went_dirty;

    Row &row_for(const godot::RID &p_entity);
    void forget(const godot::RID &p_entity);

public:
    ~Book();

    void set_went_dirty(const godot::Callable &callback);

    void enroll(const godot::RID &p_entity, int64_t p_route);

    Runtime *open_runtime(const godot::RID &p_entity);
    Runtime *runtime_of(const godot::RID &p_entity) const;
    Runtime *runtime_at(int64_t p_route) const;
    godot::LocalVector<Runtime *> runtimes() const;

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
};

} // namespace netw::display

VARIANT_ENUM_CAST(netw::display::Dirt);
