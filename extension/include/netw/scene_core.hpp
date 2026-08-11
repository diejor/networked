#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

// The scene column's record plane: what a scene column remembers between
// frames, which is a routing table keyed by scene identity and one in-flight
// request.
class NetwSceneCore : public RefCounted {
    GDCLASS(NetwSceneCore, RefCounted)

private:
    struct Row {
        int32_t event = 0;
        RID scene;
        LocalVector<Callable> callbacks;
    };

    LocalVector<Row> rows;

    int32_t next_request_id = 1;
    int32_t pending_request_id = 0;

    Row *row_for(int event, const RID &scene);

protected:
    static void _bind_methods();

public:
    void observe(
        const RID &scene,
        int event,
        const Callable &callback
    );

    void unobserve(
        const RID &scene,
        int event,
        const Callable &callback
    );

    int dispatch(
        const RID &scene,
        int event,
        bool present,
        const Variant &subject
    );

    int observer_count(const RID &scene, int event) const;
    void forget_scene(const RID &scene);

    int open_request();
    bool is_current(int request_id) const;
    void close_request();

    int get_pending_request_id() const;
    int get_next_request_id() const;
    void set_next_request_id(int value);

    void clear();
};

} // namespace netw
