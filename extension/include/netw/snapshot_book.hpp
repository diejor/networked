#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class SnapshotBook {
    struct Column {
        godot::StringName property;
        double interval = 0.0;
        double accum = 0.0;
    };

    godot::LocalVector<Column> columns;
    godot::Dictionary last_flushed;
    double default_interval = 0.0;

    int index_of(const godot::StringName &property) const;

public:
    void set_default_interval(double value);
    double get_default_interval() const;

    void declare(const godot::StringName &property, double interval);
    bool is_empty() const;
    godot::Array properties() const;

    godot::Array advance(double delta);

    godot::Dictionary changed(const godot::Dictionary &current) const;
    bool differs(const godot::Dictionary &current) const;

    void commit(const godot::Dictionary &values);
    void adopt(const godot::Dictionary &values);
};

} // namespace netw
