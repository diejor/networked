#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::repl {

class WatchBook {
    struct Stream {
        bool inited = false;
        godot::Array values;
        godot::LocalVector<int64_t> stamps;
        int64_t change_counter = 0;
        godot::HashMap<int64_t, int64_t> baselines;
    };

    godot::HashMap<int64_t, Stream> streams;

    Stream *stream_for(int64_t p_key);
    const Stream *stream_for(int64_t p_key) const;

public:
    static const int FIELD_LIMIT = 64;

    void poll(
        int64_t p_key,
        const godot::Array &p_values,
        const godot::Array &p_readable
    );

    void mask_for(
        int64_t p_key,
        int64_t p_peer,
        uint64_t &r_mask,
        godot::Array &r_values
    ) const;

    void commit(int64_t p_key, int64_t p_peer);

    void reset(int64_t p_key);

    void clear_baselines(int64_t p_key);

    void clear_peer(int64_t p_peer);

    void retain_baselines(
        int64_t p_key,
        const godot::PackedInt32Array &p_recipients
    );

    bool is_inited(int64_t p_key) const;

    void clear();
};

} // namespace netw::repl
