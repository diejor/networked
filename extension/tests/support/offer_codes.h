#pragma once

#include "netw_reset.h"

#include <vector>

#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/object_port.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/stream_book.hpp"
#include "netw/wire/value_row.hpp"

namespace netw_test {

struct RowOffer {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    const netw::table::SchemaRecord *schema = nullptr;
    const uint64_t *codes = nullptr;
    const godot::Variant *carried = nullptr;
    godot::LocalVector<int> recipients;
    float priority = 1.0f;
    bool masked = false;
    bool reliable = false;
    bool windowed = false;
    uint32_t window = 0;
    int64_t heartbeat = 0;
    int64_t tick = -1;
    int64_t ack = -1;
    int64_t life = 0;
    uint8_t marks = 0;
    netw::wire::StreamTenure tenure;

    const netw::table::SchemaRecord &declared() const {
        static const netw::table::SchemaRecord unplannable;
        return schema != nullptr ? *schema : unplannable;
    }

    netw::wire::StreamFamily family() const {
        if (windowed) {
            return netw::wire::StreamFamily::WINDOW;
        }
        return reliable ? netw::wire::StreamFamily::RETAINED
                        : netw::wire::StreamFamily::VOLATILE;
    }

    netw::wire::StreamLane lane() const {
        netw::wire::StreamLane made;
        made.route = route;
        made.ordinal = comp;
        made.family = family();
        return made;
    }
};

inline std::vector<netw::wire::CodeRow *> &offer_code_pool() {
    static std::vector<netw::wire::CodeRow *> pool;
    return pool;
}

inline void clear_offer_codes() {
    for (netw::wire::CodeRow *held : offer_code_pool()) {
        delete held;
    }
    offer_code_pool().clear();
}

inline const bool offer_codes_hooked = register_reset_hook(&clear_offer_codes);

inline bool offer_values(RowOffer &r_offer, const godot::Array &p_values) {
    r_offer.codes = nullptr;
    r_offer.carried = nullptr;
    netw::wire::CodeRow *held = new netw::wire::CodeRow();
    offer_code_pool().push_back(held);
    if (!netw::wire::encode_scalar_row(r_offer.declared(), p_values, *held)) {
        return false;
    }
    r_offer.codes = held->word_data();
    r_offer.carried = held->value_data();
    return true;
}

inline bool offer_value(RowOffer &r_offer, const godot::Variant &p_value) {
    godot::Array values;
    values.push_back(p_value);
    return offer_values(r_offer, values);
}

inline bool offer_row(RowOffer &r_offer, const netw::wire::CodeRow &p_row) {
    netw::wire::CodeRow *held = new netw::wire::CodeRow();
    offer_code_pool().push_back(held);
    held->copy_from(p_row);
    r_offer.codes = held->word_data();
    r_offer.carried = held->value_data();
    return true;
}

inline godot::Array lane_values(
    const netw::repl::PropertyBinding *p_binding,
    int64_t p_lane
) {
    godot::Array values;
    godot::Node *node = p_binding->node();
    for (const netw::repl::PropertyColumn &column :
         p_binding->get_set()->columns) {
        if (column.lane == p_lane) {
            values.push_back(netw::port_get(node, column.get_key()));
        }
    }
    return values;
}

} // namespace netw_test
