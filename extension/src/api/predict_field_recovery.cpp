#include "netw/api/predict_field_recovery.hpp"

#include "godot/class_db.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace netw {

Ref<NetwPredictFieldRecovery> NetwPredictFieldRecovery::of(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    const StringName &p_key
) {
    Ref<NetwPredictFieldRecovery> out;
    out.instantiate();
    out->pool = p_pool;
    out->slot = p_slot;
    out->key = p_key;
    return out;
}

int64_t NetwPredictFieldRecovery::count_at(int p_column) const {
    if (pool == nullptr) {
        return 0;
    }
    const PackedInt64Array counts = pool->ledger_counts(slot, key);
    return p_column < counts.size() ? counts[p_column] : 0;
}

int64_t NetwPredictFieldRecovery::get_triggered() const {
    return count_at(0);
}

int64_t NetwPredictFieldRecovery::get_repaired() const {
    return count_at(1);
}

int64_t NetwPredictFieldRecovery::get_contracted() const {
    return count_at(2);
}

int64_t NetwPredictFieldRecovery::get_carried() const {
    return count_at(3);
}

int64_t NetwPredictFieldRecovery::get_declined() const {
    return count_at(4);
}

int64_t NetwPredictFieldRecovery::get_infidelity() const {
    return count_at(5);
}

void NetwPredictFieldRecovery::_bind_methods() {
#define NETW_LEDGER_READ(m_name, m_getter) \
    ClassDB::bind_method( \
        D_METHOD(#m_getter), \
        &NetwPredictFieldRecovery::m_getter \
    ); \
    ADD_PROPERTY( \
        PropertyInfo(Variant::INT, #m_name), \
        godot::String(), \
        #m_getter \
    );

    ClassDB::bind_method(
        D_METHOD("get_field"),
        &NetwPredictFieldRecovery::get_field
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::STRING_NAME, "field"),
        godot::String(),
        "get_field"
    );

    NETW_LEDGER_READ(triggered, get_triggered)
    NETW_LEDGER_READ(repaired, get_repaired)
    NETW_LEDGER_READ(contracted, get_contracted)
    NETW_LEDGER_READ(carried, get_carried)
    NETW_LEDGER_READ(declined, get_declined)
    NETW_LEDGER_READ(infidelity, get_infidelity)

#undef NETW_LEDGER_READ
}

} // namespace netw
