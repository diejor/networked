#pragma once

#include <cstdint>

#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwHandleLedger {
    struct Record {
        int64_t id = 0;
    };

    mutable godot::RID_Owner<Record> records;
    godot::HashMap<int64_t, godot::RID> by_id;
    int64_t id_counter = 0;

public:
    NetwHandleLedger();
    ~NetwHandleLedger();

    godot::RID rid_create();
    bool rid_is_valid(const godot::RID &rid) const;
    bool rid_free(const godot::RID &rid);
    godot::RID rid_from_id(int64_t id) const;
    int64_t id_count() const;
    void clear();
};

} // namespace netw
