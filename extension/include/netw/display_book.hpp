#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/display_decl.hpp"
#include "netw/display_runtime.hpp"

namespace netw {

using namespace godot;

class NetwDisplayBook : public RefCounted {
    GDCLASS(NetwDisplayBook, RefCounted)

private:
    struct Row {
        int64_t route = 0;
        Ref<NetwDisplayRuntime> runtime;
        Ref<NetwDisplayDecl> decl;
    };

    HashMap<RID, Row> rows;
    HashMap<int64_t, RID> by_route;
    LocalVector<RID> order;
    LocalVector<RID> dirty;

    Row &row_for(const RID &p_entity);
    void forget(const RID &p_entity);

protected:
    static void _bind_methods();

public:
    void enroll(const RID &p_entity, int64_t p_route);

    void set_runtime(
        const RID &p_entity,
        const Ref<NetwDisplayRuntime> &p_runtime
    );
    Ref<NetwDisplayRuntime> runtime_of(const RID &p_entity) const;
    Ref<NetwDisplayRuntime> runtime_at(int64_t p_route) const;
    TypedArray<NetwDisplayRuntime> runtimes() const;

    void set_decl(const RID &p_entity, const Ref<NetwDisplayDecl> &p_decl);
    Ref<NetwDisplayDecl> decl_of(const RID &p_entity) const;

    RID entity_at(int64_t p_route) const;
    int64_t route_of(const RID &p_entity) const;

    void mark_dirty(const RID &p_entity, int p_dirt);
    int write_param(
        const RID &p_entity,
        const Ref<NetwDisplayDecl> &p_decl,
        int p_param,
        const Variant &p_value
    );
    void clear_dirty(const RID &p_entity);
    bool is_dirty(const RID &p_entity) const;
    TypedArray<RID> take_dirty();

    void drop(const RID &p_entity);
    void drop_route(int64_t p_route);
    void clear();
    int size() const;
};

} // namespace netw
