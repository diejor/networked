#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/display_decl.hpp"
#include "netw/display_runtime.hpp"
#include "netw/pump_stats.hpp"

namespace netw {

class NetwDisplayBook : public godot::RefCounted {
    GDCLASS(NetwDisplayBook, godot::RefCounted)

private:
    struct Row {
        int64_t route = 0;
        godot::Ref<NetwDisplayRuntime> runtime;
        godot::Ref<NetwDisplayDecl> decl;
    };

    godot::HashMap<godot::RID, Row> rows;
    godot::HashMap<int64_t, godot::RID> by_route;
    godot::LocalVector<godot::RID> order;
    godot::LocalVector<godot::RID> dirty;
    godot::Ref<NetwPumpStats> stats;

    Row &row_for(const godot::RID &p_entity);
    void forget(const godot::RID &p_entity);

protected:
    static void _bind_methods();

public:
    NetwDisplayBook();

    void enroll(const godot::RID &p_entity, int64_t p_route);

    void set_runtime(
        const godot::RID &p_entity,
        const godot::Ref<NetwDisplayRuntime> &p_runtime
    );
    godot::Ref<NetwDisplayRuntime> runtime_of(const godot::RID &p_entity)
        const;
    godot::Ref<NetwDisplayRuntime> runtime_at(int64_t p_route) const;
    godot::TypedArray<NetwDisplayRuntime> runtimes() const;

    void set_decl(
        const godot::RID &p_entity,
        const godot::Ref<NetwDisplayDecl> &p_decl
    );
    godot::Ref<NetwDisplayDecl> decl_of(const godot::RID &p_entity) const;

    godot::Ref<NetwPumpStats> get_stats() const;
    void set_stats(const godot::Ref<NetwPumpStats> &p_stats);

    godot::RID entity_at(int64_t p_route) const;
    int64_t route_of(const godot::RID &p_entity) const;

    void mark_dirty(const godot::RID &p_entity, int p_dirt);
    int write_param(
        const godot::RID &p_entity,
        const godot::Ref<NetwDisplayDecl> &p_decl,
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

} // namespace netw
