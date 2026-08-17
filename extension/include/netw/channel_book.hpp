#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/rid.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwChannelBook : public godot::RefCounted {
    GDCLASS(NetwChannelBook, godot::RefCounted)

private:
    struct Row {
        godot::Callable handler;
        bool defers = false;
        bool protocol = false;
    };

    godot::HashMap<int64_t, Row> rows;

    const Row *row_of(int64_t channel) const;

protected:
    static void _bind_methods();

public:
    bool register_channel(
        int64_t channel,
        const godot::Callable &handler,
        bool defers
    );

    bool register_protocol(int64_t channel, const godot::Callable &handler);

    bool unregister_channel(int64_t channel);

    godot::Callable handler_of(int64_t channel) const;

    godot::Callable protocol_handler_of(int64_t channel) const;

    godot::Error settle_protocol() const;

    bool defers(int64_t channel) const;

    int size() const;
    void clear();
};

} // namespace netw
