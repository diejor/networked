#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwCallPark : public godot::RefCounted {
    GDCLASS(NetwCallPark, godot::RefCounted)

public:
    static constexpr int BUDGET = 100;

private:
    struct Row {
        int64_t id = 0;
        int64_t sender = 0;
        int64_t route = 0;
        int64_t deadline = 0;
        bool active = true;
    };

    godot::LocalVector<Row> rows;
    int64_t next_id = 1;
    int64_t refused_calls = 0;

protected:
    static void _bind_methods();

public:
    int64_t park(int64_t sender, int64_t route, int64_t deadline);
    bool resolve(int64_t id);
    void sweep(int64_t now);

    int active_count(int64_t sender) const;
    int size() const;
    int64_t refused() const { return refused_calls; }
    int64_t route_of(int64_t id) const;

    void clear();
};

} // namespace netw
