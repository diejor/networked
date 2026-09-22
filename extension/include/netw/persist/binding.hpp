#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {
class NetwMultiplayer;
} // namespace netw

namespace netw::persist {

struct BoundColumn {
    int index = -1;
    godot::StringName key;
    godot::ObjectID node;
    godot::StringName property;
};

struct Binding {
    godot::ObjectID root;
    godot::ObjectID handle;
    godot::RID database;
    godot::RID schema;
    godot::StringName record_id;
    godot::LocalVector<BoundColumn> columns;
    double interval = 0.0;
    double elapsed = 0.0;
    bool load_on_spawn = true;
    bool in_flight = false;
    bool departed = false;
    bool final_save = true;
    bool retiring = false;
    bool final_sent = false;
    godot::Error failed = godot::OK;
    godot::String failure;
    godot::Ref<NetwPromise> loading;
    godot::Dictionary baseline;
    godot::Dictionary reference;
    godot::Dictionary departure_values;
};

class Bindings {
    mutable godot::RID_PtrOwner<Binding> book;
    godot::HashMap<uint64_t, godot::RID> by_root;
    godot::HashSet<uint64_t> withheld;
    godot::LocalVector<godot::RID> order;
    NetwMultiplayer *session = nullptr;

    void forget(const godot::RID &p_binding);

public:
    ~Bindings();

    void bind(NetwMultiplayer *p_session);

    godot::RID compile(godot::Node *p_root);
    godot::RID find(godot::Node *p_root) const;
    godot::RID find_id(godot::ObjectID p_root) const;
    bool is_valid(const godot::RID &p_binding) const;
    Binding *at(const godot::RID &p_binding);
    const Binding *at(const godot::RID &p_binding) const;
    void release(const godot::RID &p_binding);
    void clear();

    const godot::LocalVector<godot::RID> &enrolled() const {
        return order;
    }

    void withhold(godot::Node *p_root);
    void publish(godot::Node *p_root);
    void unwithhold(godot::ObjectID p_root);
    bool withholds(godot::Node *p_root) const;
    bool awaits_load(const godot::RID &p_binding) const;

    godot::Dictionary live_values(const godot::RID &p_binding) const;
    bool is_dirty(const godot::RID &p_binding) const;
    bool has_unsaved_changes(const godot::RID &p_binding) const;
    void adopt(const godot::RID &p_binding, const godot::Dictionary &p_values);
    bool apply(const godot::RID &p_binding, const godot::Dictionary &p_values);
    void capture(const godot::RID &p_binding);
    void keep(const godot::RID &p_binding);
    void depart(const godot::RID &p_binding, bool p_save);

    godot::LocalVector<godot::RID> due(double p_delta);
};

} // namespace netw::persist
