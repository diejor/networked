#pragma once

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/variant.hpp"

namespace netw {

class SettleQueue {
public:
    static constexpr int MAX_PASSES = 8;

private:
    struct Row {
        godot::StringName key;
        godot::Callable fn;
        int pumps = 0;
    };

    bool has_due() const;

    godot::LocalVector<Row> rows;

public:
    void schedule(const godot::Callable &fn, const godot::StringName &key);

    void schedule_after(
        const godot::Callable &fn,
        const godot::StringName &key,
        int p_pumps
    );

    void advance_windows();

    void cancel(const godot::StringName &key);

    bool is_empty() const;
    int size() const;
    bool has(const godot::StringName &key) const;

    godot::PackedStringArray drain();

    void clear();
};

} // namespace netw
