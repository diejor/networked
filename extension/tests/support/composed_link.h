#pragma once

#include "netw_test.h"

#include "row_image_oracle.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/control_record.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/snapshot_stream.hpp"
#include "netw/wire/stream_book.hpp"

namespace netw_test {

constexpr int COMPOSED_WRITER = 3;
constexpr int COMPOSED_READER = 4;
constexpr uint64_t COMPOSED_EPOCH = 9;
constexpr uint32_t COMPOSED_SCHEMA = 0xC5C0;
constexpr int64_t COMPOSED_BASE_TICK = 500;

enum class Landing : uint8_t {
    ACCEPTED,
    DUPLICATE,
    STALE,
    REFUSED_BASELINE,
    UNBOUND,
};

struct Packet {
    godot::PackedByteArray bytes;
    uint64_t token = 0;
    uint64_t revision = 0;
};

struct ComposedLink {
    netw::wire::WirePlan plan;
    netw::wire::StreamWriterBook writers;
    netw::wire::StreamReaderBook readers;
    RowImageLedger ledger;
    godot::Vector<Packet> flight;
    godot::Vector<netw::wire::ControlRecord> receipts;
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

    void open(const netw::wire::WirePlan &p_plan) {
        plan = p_plan;
        seat(COMPOSED_WRITER);
    }

    void seat(int p_writer_peer) {
        const uint64_t request = writers.open(
            p_writer_peer,
            lane_of(route),
            COMPOSED_EPOCH,
            COMPOSED_SCHEMA
        );
        uint64_t minted = 0;
        readers.open(
            COMPOSED_READER,
            lane_of(route),
            request,
            COMPOSED_EPOCH,
            COMPOSED_SCHEMA,
            minted
        );
        writers.ready(p_writer_peer, request, minted);
        token = minted;
    }

    netw::wire::SnapshotSender *sender(int p_writer_peer = COMPOSED_WRITER) {
        return writers.sender(p_writer_peer, lane_of(route));
    }

    netw::wire::SnapshotReceiver *receiver() {
        return readers.receiver(COMPOSED_READER, token);
    }

    uint64_t accepted_revision() {
        const netw::wire::SnapshotReceiver *reader = receiver();
        return reader == nullptr ? 0 : reader->accepted_at();
    }

    uint32_t ring_count() {
        const netw::wire::SnapshotReceiver *reader = receiver();
        return reader == nullptr ? 0 : reader->ring_count();
    }

    bool holds_nothing() {
        const netw::wire::SnapshotReceiver *reader = receiver();
        return reader == nullptr || reader->accepted() == nullptr;
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

    uint64_t publish(
        const netw::wire::CodeRow &p_row,
        int p_writer_peer = COMPOSED_WRITER
    ) {
        const uint64_t sending
            = writers.token_of(p_writer_peer, lane_of(route));
        netw::wire::SnapshotSender *stream = sender(p_writer_peer);
        if (sending == 0 || stream == nullptr) {
            return 0;
        }
        stream->desire(p_row);
        if (stream->quiet()) {
            return 0;
        }
        const uint64_t revision = stream->reserve();
        const uint64_t distance = stream->distance_for(revision);
        const netw::wire::CodeRow *base
            = distance == 0 ? nullptr : stream->confirmed();
        netw::repl::SnapshotHeader header;
        header.token = sending;
        header.revision = revision;
        header.distance = distance;
        header.tick = COMPOSED_BASE_TICK;
        header.mask = distance == 0
            ? plan.full_mask()
            : netw::wire::CodeRow::changed_mask(plan, *base, p_row);
        Packet packet;
        packet.bytes = netw::repl::write_snapshot_row(
            header,
            COMPOSED_BASE_TICK,
            plan,
            p_row,
            base
        );
        packet.token = sending;
        packet.revision = revision;
        stream->expose(revision, p_row);
        ledger.stage(sending, revision, plan, p_row);
        flight.push_back(packet);
        writers.note_attempt(p_writer_peer, lane_of(route), now_ms);
        price(packet, header, p_row);
        return revision;
    }

    void price(
        const Packet &p_packet,
        const netw::repl::SnapshotHeader &p_header,
        const netw::wire::CodeRow &p_row
    ) {
        netw::repl::SnapshotHeader whole = p_header;
        whole.distance = 0;
        whole.mask = plan.full_mask();
        const godot::PackedByteArray forced = netw::repl::write_snapshot_row(
            whole,
            COMPOSED_BASE_TICK,
            plan,
            p_row,
            nullptr
        );
        written_bytes += p_packet.bytes.size();
        absolute_bytes += forced.size();
        written_rows += 1;
        if (p_header.distance != 0) {
            stepped_rows += 1;
            stepped_written += p_packet.bytes.size();
            stepped_absolute += forced.size();
        }
    }

    uint64_t repair(int p_writer_peer = COMPOSED_WRITER) {
        netw::wire::SnapshotSender *stream = sender(p_writer_peer);
        if (stream == nullptr || stream->quiet()) {
            return 0;
        }
        if (!writers.repair_due(p_writer_peer, lane_of(route), now_ms, 0)) {
            return 0;
        }
        const uint64_t sending
            = writers.token_of(p_writer_peer, lane_of(route));
        const uint64_t revision = stream->pinned_repair();
        netw::repl::SnapshotHeader header;
        header.token = sending;
        header.revision = revision;
        header.distance = 0;
        header.tick = COMPOSED_BASE_TICK;
        header.mask = plan.full_mask();
        Packet packet;
        packet.bytes = netw::repl::write_snapshot_row(
            header,
            COMPOSED_BASE_TICK,
            plan,
            stream->target(),
            nullptr
        );
        packet.token = sending;
        packet.revision = revision;
        ledger.stage(sending, revision, plan, stream->target());
        flight.push_back(packet);
        price(packet, header, stream->target());
        return revision;
    }

    Landing land(const Packet &p_packet, bool p_consumer_admits) {
        netw::repl::SnapshotHeader named;
        if (!netw::repl::name_snapshot_row(p_packet.bytes, named)) {
            return Landing::UNBOUND;
        }
        netw::wire::SnapshotReceiver *reader
            = readers.receiver(COMPOSED_READER, named.token);
        if (reader == nullptr) {
            return Landing::UNBOUND;
        }
        const netw::wire::RowAdmission admission
            = reader->admits(named.revision);
        if (admission == netw::wire::RowAdmission::DUPLICATE) {
            queue_receipt(named.token, reader->accepted_at());
            return Landing::DUPLICATE;
        }
        if (admission == netw::wire::RowAdmission::STALE) {
            return Landing::STALE;
        }
        const netw::wire::CodeRow *base = named.absolute()
            ? nullptr
            : reader->baseline(named.baseline_revision());
        if (!named.absolute() && base == nullptr) {
            return Landing::REFUSED_BASELINE;
        }
        netw::wire::CodeRow landed = netw::wire::CodeRow::for_plan(plan);
        netw::repl::SnapshotHeader read;
        netw::repl::SnapshotRefusal refusal = netw::repl::SnapshotRefusal::NONE;
        if (!netw::repl::read_snapshot_row(
                p_packet.bytes,
                COMPOSED_BASE_TICK,
                plan,
                read,
                landed,
                base,
                &refusal
            )) {
            return refusal == netw::repl::SnapshotRefusal::BASELINE_UNKNOWN
                ? Landing::REFUSED_BASELINE
                : Landing::UNBOUND;
        }
        if (!p_consumer_admits) {
            consumer_refusals += 1;
            return Landing::UNBOUND;
        }
        reader->commit(read.revision, landed);
        ledger.accept(named.token, read.revision, plan, landed);
        queue_receipt(named.token, read.revision);
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
            last = writers.receipt(
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
            netw::wire::SnapshotSender *stream = sender(p_writer_peer);
            if (stream != nullptr && stream->quiet()) {
                return true;
            }
            now_ms += 300;
            repair(p_writer_peer);
            deliver_all();
            apply_all_receipts(p_writer_peer);
        }
        netw::wire::SnapshotSender *stream = sender(p_writer_peer);
        return stream != nullptr && stream->quiet();
    }

    uint64_t column(int p_column) const {
        const netw::wire::SnapshotReceiver *reader
            = const_cast<netw::wire::StreamReaderBook &>(readers).receiver(
                COMPOSED_READER,
                token
            );
        const netw::wire::CodeRow *held
            = reader == nullptr ? nullptr : reader->accepted();
        return held == nullptr ? 0 : held->read(plan.column(p_column), 0);
    }

    bool oracle_is_clean() const {
        return ledger.audit().clean();
    }
};

} // namespace netw_test
