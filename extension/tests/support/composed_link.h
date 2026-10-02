#pragma once

#include "netw_test.h"

#include "row_image_oracle.h"
#include "send_rig.h"
#include "store_reader.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/registry.hpp"
#include "netw/wire/snapshot_stream.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw_test {

constexpr int COMPOSED_WRITER = 3;
constexpr int COMPOSED_READER = 4;
constexpr uint64_t COMPOSED_EPOCH = 9;
constexpr uint32_t COMPOSED_SCHEMA = 0xC5C0;
constexpr int64_t COMPOSED_BASE_TICK = 500;
constexpr uint8_t COMPOSED_CHANNEL = 47;

enum class Landing : uint8_t {
    ACCEPTED,
    DUPLICATE,
    STALE,
    REFUSED_BASELINE,
    UNBOUND,
};

struct Packet {
    godot::PackedByteArray bytes;
    int writer = 0;
    uint64_t token = 0;
    uint64_t revision = 0;
};

inline const netw::wire::WireRegistry &composed_registry() {
    static const netw::wire::WireRegistry made = [] {
        netw::wire::WireRegistry out;
        netw::wire::ChannelDecl decl;
        decl.id = COMPOSED_CHANNEL;
        decl.name = godot::StringName("composed_probe");
        decl.delivery = netw::wire::Delivery::FITTED;
        out.register_channel(decl);
        return out;
    }();
    return made;
}

struct ComposedLink {
    netw::table::SchemaRecord schema;
    netw::wire::WirePlan plan;
    SessionSend send;
    netw::wire::StreamReaderBook readers;
    StoreReader store;
    int writer = COMPOSED_WRITER;
    RowImageLedger ledger;
    godot::Vector<Packet> flight;
    godot::Vector<netw::wire::ControlRecord> receipts;
    netw::wire::CodeRow target;
    int64_t route = 11;
    int64_t now_ms = 0;
    uint64_t token = 0;
    uint64_t consumer_refusals = 0;
    int64_t absolute_bytes = 0;
    int64_t written_bytes = 0;
    int64_t written_rows = 0;
    int64_t stepped_rows = 0;
    int64_t stepped_written = 0;
    int64_t stepped_absolute = 0;

    netw::wire::StreamLane lane_of(int64_t p_route) const {
        netw::wire::StreamLane made;
        made.route = p_route;
        made.ordinal = 0;
        made.family = netw::wire::StreamFamily::VOLATILE;
        return made;
    }

    RowOffer offer_for(int p_writer_peer) const {
        RowOffer made;
        made.route = route;
        made.comp = 0;
        made.channel = COMPOSED_CHANNEL;
        made.schema = &schema;
        made.life = int64_t(COMPOSED_EPOCH);
        made.tick = COMPOSED_BASE_TICK;
        offer_row(made, target);
        made.recipients.push_back(p_writer_peer);
        return made;
    }

    void stage(int p_writer_peer) {
        stage_offer(send.world, offer_for(p_writer_peer), COMPOSED_BASE_TICK);
    }

    void declare(const netw::table::SchemaRecord &p_schema) {
        schema = p_schema;
        plan = netw::wire::WirePlan::compile(schema);
        target = netw::wire::CodeRow::for_plan(plan);
        store.declare(schema, route, COMPOSED_BASE_TICK);
        stage(COMPOSED_WRITER);
    }

    void open(const netw::table::SchemaRecord &p_schema) {
        declare(p_schema);
        seat(COMPOSED_WRITER);
    }

    void seat(int p_writer_peer) {
        stage(p_writer_peer);
        const uint64_t request
            = send.stream_open(p_writer_peer, lane_of(route));
        uint64_t minted = 0;
        readers.open(
            COMPOSED_READER,
            lane_of(route),
            request,
            COMPOSED_EPOCH,
            COMPOSED_SCHEMA,
            minted
        );
        send.stream_ready(p_writer_peer, request, minted);
        token = minted;
        writer = p_writer_peer;
    }

    uint64_t confirmed_at(int p_writer_peer = COMPOSED_WRITER) const {
        return row_acked(send.world, lane_of(route), p_writer_peer);
    }

    bool quiet(int p_writer_peer = COMPOSED_WRITER) const {
        return !row_owed(send.world, lane_of(route), p_writer_peer);
    }

    uint64_t accepted_revision() {
        return readers.reaches(COMPOSED_READER, token)
            ? store.accepted(writer, token)
            : 0;
    }

    uint32_t ring_count() {
        return accepted_revision() == 0 ? 0 : store.kept(writer);
    }

    bool holds_nothing() {
        return accepted_revision() == 0;
    }

    netw::wire::CodeRow pair(uint64_t p_x, uint64_t p_y) const {
        netw::wire::CodeRow made = netw::wire::CodeRow::for_plan(plan);
        made.write(plan.column(0), 0, p_x);
        made.write(plan.column(1), 0, p_y);
        return made;
    }

    netw::wire::CodeRow row_of(const godot::Vector<uint64_t> &p_codes) const {
        netw::wire::CodeRow made = netw::wire::CodeRow::for_plan(plan);
        for (int32_t at = 0; at < p_codes.size(); ++at) {
            made.write(plan.column(uint32_t(at)), 0, p_codes[at]);
        }
        return made;
    }

    uint64_t pass(int p_writer_peer) {
        godot::LocalVector<RowOffer> offers;
        offers.push_back(offer_for(p_writer_peer));
        const netw::repl::SessionResult result = send.run(
            composed_registry(),
            offers,
            int64_t(1) << 20,
            COMPOSED_BASE_TICK,
            now_ms
        );
        uint64_t revision = 0;
        for (uint32_t at = 0; at < result.sends.size(); ++at) {
            const netw::repl::RowSend &sent = result.sends[at];
            if (sent.peer != p_writer_peer) {
                continue;
            }
            Packet packet;
            packet.bytes = sent.bytes;
            packet.writer = p_writer_peer;
            packet.token = sent.token;
            packet.revision = sent.revision;
            if (!sent.resend) {
                ledger.stage(sent.token, sent.revision, plan, target);
            }
            flight.push_back(packet);
            price(packet);
            revision = sent.revision;
        }
        return revision;
    }

    uint64_t publish(
        const netw::wire::CodeRow &p_row,
        int p_writer_peer = COMPOSED_WRITER
    ) {
        target.copy_from(p_row);
        return pass(p_writer_peer);
    }

    void price(const Packet &p_packet) {
        netw::repl::SnapshotHeader named;
        if (!netw::repl::name_snapshot_row(p_packet.bytes, named)) {
            return;
        }
        netw::repl::SnapshotHeader whole = named;
        whole.distance = 0;
        whole.mask = plan.full_mask();
        whole.tick = COMPOSED_BASE_TICK;
        const godot::PackedByteArray forced = netw::repl::write_snapshot_row(
            whole,
            COMPOSED_BASE_TICK,
            plan,
            target,
            nullptr
        );
        written_bytes += p_packet.bytes.size();
        absolute_bytes += forced.size();
        written_rows += 1;
        if (named.distance != 0) {
            stepped_rows += 1;
            stepped_written += p_packet.bytes.size();
            stepped_absolute += forced.size();
        }
    }

    uint64_t repair(int p_writer_peer = COMPOSED_WRITER) {
        return pass(p_writer_peer);
    }

    Landing land(const Packet &p_packet, bool p_consumer_admits) {
        netw::repl::SnapshotHeader named;
        if (!netw::repl::name_snapshot_row(p_packet.bytes, named)) {
            return Landing::UNBOUND;
        }
        if (!readers.reaches(COMPOSED_READER, named.token)) {
            return Landing::UNBOUND;
        }
        uint64_t accepted = 0;
        const netw::wire::CodeRow *base = nullptr;
        const netw::store::Landing admitted = store.world.admits(
            store.at,
            p_packet.writer,
            p_packet.bytes,
            false,
            accepted,
            base
        );
        if (admitted == netw::store::Landing::DUPLICATE) {
            queue_receipt(named.token, accepted);
            return Landing::DUPLICATE;
        }
        if (admitted == netw::store::Landing::STALE) {
            return Landing::STALE;
        }
        if (admitted == netw::store::Landing::BASELINE_UNKNOWN) {
            return Landing::REFUSED_BASELINE;
        }
        if (admitted != netw::store::Landing::ACCEPTED) {
            return Landing::UNBOUND;
        }
        if (!p_consumer_admits) {
            consumer_refusals += 1;
            return Landing::UNBOUND;
        }
        if (store.land(p_packet.writer, p_packet.bytes)
            != netw::store::Landing::ACCEPTED) {
            return Landing::UNBOUND;
        }
        ledger.accept(named.token, named.revision, plan, store.row());
        queue_receipt(named.token, named.revision);
        return Landing::ACCEPTED;
    }

    void queue_receipt(uint64_t p_token, uint64_t p_revision) {
        netw::wire::ControlRecord record;
        record.tag = netw::wire::ControlTag::ACCEPT;
        netw::wire::ControlAcceptEntry entry;
        entry.token = p_token;
        entry.revision = p_revision;
        record.receipts.push_back(entry);
        receipts.push_back(record);
    }

    Landing deliver_at(int32_t p_index, bool p_consumer_admits = true) {
        const Packet packet = flight[p_index];
        flight.remove_at(p_index);
        return land(packet, p_consumer_admits);
    }

    void drop_at(int32_t p_index) {
        flight.remove_at(p_index);
    }

    Landing deliver_all() {
        Landing last = Landing::ACCEPTED;
        while (!flight.is_empty()) {
            last = deliver_at(0);
        }
        return last;
    }

    netw::wire::ReceiptVerdict apply_receipt_at(
        int32_t p_index,
        int p_writer_peer = COMPOSED_WRITER
    ) {
        const netw::wire::ControlRecord record = receipts[p_index];
        receipts.remove_at(p_index);
        netw::wire::ReceiptVerdict last = netw::wire::ReceiptVerdict::IGNORED;
        for (uint32_t at = 0; at < record.receipts.size(); ++at) {
            last = send.stream_receipt(
                p_writer_peer,
                record.receipts[at].token,
                record.receipts[at].revision
            );
        }
        return last;
    }

    void apply_all_receipts(int p_writer_peer = COMPOSED_WRITER) {
        while (!receipts.is_empty()) {
            apply_receipt_at(0, p_writer_peer);
        }
    }

    void drop_receipts() {
        receipts.clear();
    }

    bool settle(int p_rounds, int p_writer_peer = COMPOSED_WRITER) {
        for (int round = 0; round < p_rounds; ++round) {
            if (quiet(p_writer_peer)) {
                return true;
            }
            now_ms += 300;
            repair(p_writer_peer);
            deliver_all();
            apply_all_receipts(p_writer_peer);
        }
        return quiet(p_writer_peer);
    }

    uint64_t column(int p_column) {
        return store.column(uint32_t(p_column));
    }

    bool oracle_is_clean() const {
        return ledger.audit().clean();
    }
};

} // namespace netw_test
