#pragma once

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwSnapshotBook : public RefCounted {
    GDCLASS(NetwSnapshotBook, RefCounted)

private:
    struct Column {
        StringName property;
        double interval = 0.0;
        double accum = 0.0;
    };

    LocalVector<Column> columns;
    Dictionary last_flushed;
    double default_interval = 0.0;

    int index_of(const StringName &property) const;

protected:
    static void _bind_methods();

public:
    void set_default_interval(double value);
    double get_default_interval() const;

    void declare(const StringName &property, double interval);
    bool is_empty() const;
    Array properties() const;

    Array advance(double delta);

    Dictionary changed(const Dictionary &current) const;
    bool differs(const Dictionary &current) const;

    void commit(const Dictionary &values);
    void adopt(const Dictionary &values);
};

} // namespace netw
