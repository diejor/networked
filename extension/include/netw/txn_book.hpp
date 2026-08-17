#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwTxnBook : public RefCounted {
    GDCLASS(NetwTxnBook, RefCounted)

private:
    struct Txn {
        PackedInt64Array addressed;
        int64_t deadline = 0;
    };

    HashMap<int64_t, Txn> open_txns;
    int64_t next_id = 1;

    LocalVector<int64_t> ids_in_order() const;

protected:
    static void _bind_methods();

public:
    int64_t mint();
    void open(
        int64_t txn_id,
        const PackedInt64Array &addressed,
        int64_t deadline
    );
    bool is_open(int64_t txn_id) const;
    bool admits(int64_t txn_id, int64_t sender) const;
    PackedInt64Array addressed(int64_t txn_id) const;
    bool close(int64_t txn_id);

    PackedInt64Array expire(int64_t current);
    PackedInt64Array waiting_on(int64_t peer_id) const;
    PackedInt64Array drain();

    int size() const;
};

} // namespace netw
