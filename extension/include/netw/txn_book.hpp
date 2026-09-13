#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwTxnBook {
    struct Txn {
        godot::PackedInt64Array addressed;
        int64_t deadline = 0;
    };

    godot::HashMap<int64_t, Txn> open_txns;
    int64_t next_id = 1;

    godot::LocalVector<int64_t> ids_in_order() const;

public:
    int64_t mint();
    void open(
        int64_t txn_id,
        const godot::PackedInt64Array &addressed,
        int64_t deadline
    );
    bool is_open(int64_t txn_id) const;
    bool admits(int64_t txn_id, int64_t sender) const;
    godot::PackedInt64Array addressed(int64_t txn_id) const;
    bool close(int64_t txn_id);

    godot::PackedInt64Array expire(int64_t current);
    godot::PackedInt64Array waiting_on(int64_t peer_id) const;
    godot::PackedInt64Array drain();

    int size() const;
};

} // namespace netw
