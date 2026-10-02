#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/code_row.hpp"

namespace netw_test {

struct StoreReader {
    netw::store::World world;
    uint32_t table = 0;
    netw::store::RowAddress at;
    int64_t base_tick = 0;

    void declare(
        const netw::table::SchemaRecord &p_schema,
        int64_t p_route,
        int64_t p_base_tick
    ) {
        table = world.table_for(1, netw::store::Family::VOLATILE, p_schema);
        at = world.add_row(table, uint64_t(p_route), 0, uint64_t(p_route), 0);
        base_tick = p_base_tick;
    }

    netw::store::Table &held() {
        return world.table(table);
    }

    const netw::store::Received *received(int p_sender) {
        const uint32_t found = held().find_sender(p_sender);
        return found == netw::store::Table::NO_SLOT
            ? nullptr
            : &held().senders[found].rows[at.row];
    }

    netw::store::Landing land(
        int p_sender,
        const godot::PackedByteArray &p_frame
    ) {
        return world.receive(at, p_sender, p_frame, base_tick, false);
    }

    uint64_t accepted(int p_sender, uint64_t p_token) {
        const netw::store::Received *rows = received(p_sender);
        return rows == nullptr || rows->token != p_token ? 0 : rows->accepted;
    }

    uint32_t kept(int p_sender) {
        const netw::store::Received *rows = received(p_sender);
        return rows == nullptr ? 0 : rows->held;
    }

    bool keeps(int p_sender, uint64_t p_revision) {
        const netw::store::Received *rows = received(p_sender);
        const godot::Variant *carried = nullptr;
        return rows != nullptr
            && held().received_image(*rows, p_revision, carried) != nullptr;
    }

    netw::wire::CodeRow image_at(int p_sender, uint64_t p_revision) {
        netw::wire::CodeRow out = netw::wire::CodeRow::for_plan(held().plan);
        const netw::store::Received *rows = received(p_sender);
        const godot::Variant *carried = nullptr;
        const uint64_t *image = rows == nullptr
            ? nullptr
            : held().received_image(*rows, p_revision, carried);
        if (image != nullptr) {
            out.assign(held().plan, image, carried);
        }
        return out;
    }

    netw::wire::CodeRow row() {
        netw::wire::CodeRow out;
        out.assign(
            held().plan,
            held().codes_of(at.row),
            held().carried_of(at.row)
        );
        return out;
    }

    uint64_t column(uint32_t p_column) {
        return row().read(held().plan.column(p_column), 0);
    }
};

} // namespace netw_test
