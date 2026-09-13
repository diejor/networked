#include "netw/handle_ledger.hpp"

#include <vector>

using namespace godot;

namespace netw {

RID NetwHandleLedger::rid_create() {
    id_counter += 1;
    const RID rid = records.make_rid(Record{id_counter});
    by_id[id_counter] = rid;
    return rid;
}

bool NetwHandleLedger::rid_is_valid(const RID &rid) const {
    return records.owns(rid);
}

bool NetwHandleLedger::rid_free(const RID &rid) {
    const Record *record = records.get_or_null(rid);
    if (record == nullptr) {
        return false;
    }
    by_id.erase(record->id);
    records.free(rid);
    return true;
}

RID NetwHandleLedger::rid_from_id(int64_t id) const {
    const HashMap<int64_t, RID>::ConstIterator found = by_id.find(id);
    return found != by_id.end() ? found->value : RID();
}

int64_t NetwHandleLedger::id_count() const {
    return static_cast<int64_t>(by_id.size());
}

void NetwHandleLedger::clear() {
    const uint32_t count = records.get_rid_count();
    if (count > 0) {
        std::vector<RID> owned(count);
        records.fill_owned_buffer(owned.data());
        for (const RID &rid : owned) {
            records.free(rid);
        }
    }
    by_id.clear();
    id_counter = 0;
}

NetwHandleLedger::NetwHandleLedger() {
    records.set_description("netw::NetwHandleLedger::Record");
}

NetwHandleLedger::~NetwHandleLedger() {
    clear();
}

} // namespace netw
