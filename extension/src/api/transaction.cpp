#include "netw/api/transaction.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwTransaction::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("queue_upsert", "table", "id", "data"),
        &NetwTransaction::queue_upsert
    );
}

void NetwTransaction::queue_upsert(
    const StringName &p_table,
    const StringName &p_id,
    const Dictionary &p_data
) {
    Dictionary row;
    row["table"] = p_table;
    row["id"] = p_id;
    row["data"] = p_data;
    rows.push_back(row);
}

} // namespace netw
