#include "netw/repl/session_send.hpp"

#include <algorithm>

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/store/schedule.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/frame.hpp"

namespace netw::repl {

using namespace godot;

namespace {

wire::StreamLane lane_at(const store::Table &p_table, uint32_t p_row) {
    wire::StreamLane lane;
    lane.route = p_table.routes[p_row];
    lane.ordinal = p_table.ordinals[p_row];
    lane.family = wire::StreamFamily(uint8_t(p_table.family));
    return lane;
}

bool stream_is(
    const store::PeerRows &p_rows,
    uint32_t p_row,
    store::Stream p_stream
) {
    return p_rows.stream[p_row] == uint8_t(p_stream);
}

} // namespace

void SessionSend::queue_close(int p_peer, uint64_t p_token) {
    control.drop_token(p_peer, p_token);
    wire::ControlRecord closing;
    closing.tag = wire::ControlTag::CLOSE;
    closing.token = p_token;
    control.queue(p_peer, closing);
}

void SessionSend::queue_closes(const LocalVector<wire::ClosedLane> &p_closed) {
    for (uint32_t at = 0; at < p_closed.size(); ++at) {
        if (p_closed[at].token != 0) {
            queue_close(p_closed[at].peer, p_closed[at].token);
        }
    }
}

void SessionSend::park_tenures(int64_t p_route, uint64_t p_tenure) {
    if (world != nullptr) {
        queue_closes(world->park_tenures_other_than(p_route, p_tenure));
    }
}

void SessionSend::close_anchors(
    int64_t p_route,
    uint64_t p_anchor,
    uint64_t p_author
) {
    if (world != nullptr) {
        queue_closes(
            world->close_anchors_other_than(p_route, p_anchor, p_author)
        );
    }
}

void SessionSend::queue_open(
    int p_peer,
    const store::Table &p_table,
    uint32_t p_row,
    uint64_t p_request
) {
    const wire::StreamTenure &tenure = p_table.tenures[p_row];
    wire::ControlRecord asking;
    asking.tag = wire::ControlTag::OPEN;
    asking.request = p_request;
    asking.route = p_table.routes[p_row];
    asking.ordinal = p_table.ordinals[p_row];
    asking.family = wire::StreamFamily(uint8_t(p_table.family));
    asking.epoch = uint64_t(world->epoch_of(p_table.slots[p_row]));
    asking.tenure = tenure.tenure;
    asking.anchor = tenure.anchor;
    asking.anchor_author = tenure.anchor_author;
    asking.schema = uint32_t(p_table.schema.shape_hash);
    control.queue(p_peer, asking);
}

uint64_t SessionSend::stream_open(
    int p_peer,
    const wire::StreamLane &p_lane,
    const wire::StreamTenure &p_tenure
) {
    return world != nullptr ? world->stream_open(p_peer, p_lane, p_tenure)
                            : 0;
}

wire::ReadyVerdict SessionSend::stream_ready(
    int p_peer,
    uint64_t p_request,
    uint64_t p_token
) {
    return world != nullptr ? world->stream_ready(p_peer, p_request, p_token)
                            : wire::ReadyVerdict::UNKNOWN;
}

wire::ReceiptVerdict SessionSend::stream_receipt(
    int p_peer,
    uint64_t p_token,
    uint64_t p_revision
) {
    return world != nullptr
        ? world->stream_receipt(p_peer, p_token, p_revision)
        : wire::ReceiptVerdict::IGNORED;
}

bool SessionSend::stream_reset(
    int p_peer,
    uint64_t p_request,
    uint64_t p_token
) {
    return world != nullptr && world->stream_reset(p_peer, p_request, p_token);
}

uint64_t SessionSend::token_of(
    int p_peer,
    const wire::StreamLane &p_lane
) const {
    return world != nullptr ? world->token_of(p_peer, p_lane) : 0;
}

LocalVector<wire::ParkedOpen> SessionSend::expire_parks(int64_t p_now_ms) {
    LocalVector<wire::ParkedOpen> out;
    const LocalVector<int> parked = readers.parked_peers();
    for (uint32_t at = 0; at < parked.size(); ++at) {
        const int64_t interval
            = wire::repair_interval_ms(int64_t(link.rtt_ms(parked[at])));
        const LocalVector<wire::ParkedOpen> expired
            = readers.expire_parks(parked[at], p_now_ms, interval);
        for (uint32_t which = 0; which < expired.size(); ++which) {
            out.push_back(expired[which]);
        }
    }
    return out;
}

PackedByteArray SessionSend::price(
    const RowSend &p_send,
    int64_t p_base_tick
) const {
    if (!stage_wanted || !stage.is_valid()) {
        return p_send.bytes;
    }
    return stage.call(
        int64_t(p_send.peer),
        p_base_tick,
        p_send.route,
        int64_t(p_send.comp),
        int64_t(p_send.mask),
        p_send.bytes
    );
}

bool SessionSend::stream_seated(
    uint32_t p_peer,
    uint32_t p_table,
    uint32_t p_row,
    int64_t p_now_ms
) {
    store::Table &table = world->table(p_table);
    store::PeerRows &rows = table.peers[p_peer];
    const int id = world->peer_id(p_peer);
    const wire::StreamLane lane = lane_at(table, p_row);
    const bool parked = stream_is(rows, p_row, store::Stream::DORMANT);
    const bool moved = !stream_is(rows, p_row, store::Stream::NONE)
        && !(rows.opened[p_row] == table.tenures[p_row]);
    const store::RowAddress at{p_table, p_row};
    if ((parked || moved)
        && !world->stream_resume(p_peer, at, table.tenures[p_row])) {
        uint64_t token = 0;
        world->stream_close(id, lane, token);
        if (token != 0) {
            queue_close(id, token);
        }
    }
    if (stream_is(rows, p_row, store::Stream::READY)) {
        return true;
    }
    const bool asked = stream_is(rows, p_row, store::Stream::OPENING)
        && rows.request[p_row] != 0;
    if (asked
        && p_now_ms - rows.repair_at[p_row]
            < wire::repair_interval_ms(int64_t(link.rtt_ms(id)))) {
        return false;
    }
    const uint64_t request
        = world->stream_open(id, lane, table.tenures[p_row]);
    if (request != 0) {
        queue_open(id, table, p_row, request);
        rows.repair_at[p_row] = p_now_ms;
    }
    return false;
}

bool SessionSend::classify(
    uint32_t p_peer,
    uint32_t p_table,
    uint32_t p_row,
    bool p_due,
    int64_t p_now_ms,
    RowForm &r_form,
    SessionResult &r_out
) {
#if defined(NETW_TESTS)
    rows_touched += 1;
#endif
    store::Table &table = world->table(p_table);
    const uint8_t flags = table.flags[p_row];
    if ((flags & store::Table::UNGATHERED) != 0) {
        return false;
    }
    if ((flags & store::Table::UNENCODED) != 0) {
        r_out.ungathered += 1;
        return false;
    }
    if (!stream_seated(p_peer, p_table, p_row, p_now_ms)) {
        r_out.deferred += 1;
        return false;
    }
    const store::PeerRows &rows = table.peers[p_peer];
    if (table.family == store::Family::WINDOW) {
        WindowRing *ring = lanes.open_window(
            table.routes[p_row],
            table.ordinals[p_row],
            table.schema,
            table.window
        );
        if (ring == nullptr || ring->held() == 0) {
            r_out.caught_up += 1;
            return false;
        }
        r_form = RowForm::WINDOW;
        return true;
    }
    if (!rows.owed.get(p_row)) {
        if (!p_due || rows.acked_rev[p_row] == 0) {
            r_out.caught_up += 1;
            return false;
        }
        r_form = RowForm::REFRESH;
        return true;
    }
    const uint64_t exposed = rows.exposed_rev[p_row];
    const store::Flight *newest = exposed > rows.acked_rev[p_row]
        ? world->flight_of(p_peer, rows.token[p_row], exposed)
        : nullptr;
    if (newest == nullptr || !table.holds_image(p_row, newest->image)) {
        r_form = RowForm::FRESH;
        return true;
    }
    const int64_t wait = wire::repair_interval_ms(
        int64_t(link.rtt_ms(world->peer_id(p_peer)))
    );
    if (p_now_ms - rows.repair_at[p_row] < wait) {
        r_out.caught_up += 1;
        return false;
    }
    r_form = RowForm::REPAIR;
    return true;
}

bool SessionSend::unfitted(
    const wire::WireRegistry &p_registry,
    const store::Table &p_table
) const {
    if (p_table.family == store::Family::RETAINED) {
        return true;
    }
    const wire::ChannelDecl *decl = p_registry.find_channel(p_table.channel);
    return decl != nullptr && decl->delivery == wire::Delivery::IMMEDIATE;
}

void SessionSend::record_windows(int64_t p_base_tick) {
    NETW_ZONE_NC("Record windows", colors::WIRE);
    for (uint32_t index = 0; index < world->table_count(); ++index) {
        store::Table &table = world->table(index);
        if (table.family != store::Family::WINDOW) {
            continue;
        }
        for (uint32_t row = 0; row < table.row_count(); ++row) {
            if (table.routes[row] <= 0
                || table.sources[row] != uint8_t(store::Source::LOCAL)
                || (table.flags[row]
                    & (store::Table::UNGATHERED | store::Table::UNENCODED))
                    != 0) {
                continue;
            }
            bool reaches = false;
            for (uint32_t peer = 0; peer < table.peers.size() && !reaches;
                 ++peer) {
                reaches = world->peer_joined(peer)
                    && table.peers[peer].sendable.get(row);
            }
            if (!reaches) {
                continue;
            }
            world->stamp(index, row);
            WindowRing *ring = lanes.open_window(
                table.routes[row],
                table.ordinals[row],
                table.schema,
                table.window
            );
            if (ring == nullptr || !ring->valid()) {
                continue;
            }
            image.assign(
                ring->plan(),
                table.codes_of(row),
                table.carried_of(row)
            );
            const int64_t tick = table.frame_ticks[row] >= 0
                ? table.frame_ticks[row]
                : p_base_tick;
            ring->record(tick, image);
        }
    }
}

bool SessionSend::encode(
    uint32_t p_peer,
    uint32_t p_table,
    uint32_t p_row,
    RowForm p_form,
    int64_t p_base_tick,
    RowSend &r_send,
    SessionResult &r_out
) {
    world->stamp(p_table, p_row);
    store::Table &table = world->table(p_table);
    const store::PeerRows &rows = table.peers[p_peer];
    const wire::WirePlan &plan = table.plan;
    image.assign(plan, table.codes_of(p_row), table.carried_of(p_row));
    r_send = RowSend();
    r_send.route = table.routes[p_row];
    r_send.comp = table.ordinals[p_row];
    r_send.channel = table.channel;
    r_send.peer = world->peer_id(p_peer);
    r_send.table = p_table;
    r_send.row = p_row;
    r_send.form = p_form;
    r_send.token = rows.token[p_row];
    r_send.reliable = table.family == store::Family::RETAINED;
    r_send.windowed = table.family == store::Family::WINDOW;

    SnapshotHeader header;
    header.token = r_send.token;
    header.reconcile_ack = table.reconcile_acks[p_row];
    header.tick = table.frame_ticks[p_row];
    if (p_form == RowForm::WINDOW) {
        WindowRing *ring = lanes.open_window(
            table.routes[p_row],
            table.ordinals[p_row],
            table.schema,
            table.window
        );
        const LocalVector<WindowSample> samples = ring->pending();
        header.revision = rows.next_rev[p_row];
        header.tick = -1;
        r_send.revision = header.revision;
        r_send.mask = ring->plan().full_mask();
        r_send.whole = true;
        r_send.sample_count = samples.size();
        r_send.bytes = write_snapshot_window(
            stream,
            header,
            p_base_tick,
            ring->plan(),
            samples
        );
    } else {
        const store::Flight *pinned = p_form == RowForm::REPAIR
            ? world->pinned_of(p_peer, rows.token[p_row])
            : nullptr;
        r_send.resend = pinned != nullptr
            && table.holds_image(p_row, pinned->image);
        header.revision
            = r_send.resend ? pinned->revision : rows.next_rev[p_row];
        const uint64_t acked = rows.acked_rev[p_row];
        const uint64_t span = header.revision - acked;
        const bool deltas
            = p_form == RowForm::FRESH || p_form == RowForm::REFRESH;
        header.distance = deltas && acked != 0
                && header.revision > acked
                && span <= wire::SNAPSHOT_DELTA_SPAN
            ? span
            : 0;
        if (!header.absolute()) {
            baseline.assign(
                plan,
                rows.baseline.ptr() + uint64_t(p_row) * table.words,
                table.carried != 0 ? rows.baseline_carried.ptr()
                        + uint64_t(p_row) * table.carried
                                   : nullptr
            );
        }
        header.mask = header.absolute()
            ? plan.full_mask()
            : wire::CodeRow::changed_mask(plan, baseline, image);
        r_send.revision = header.revision;
        r_send.mask = header.mask;
        r_send.masked = !header.absolute();
        r_send.whole = header.mask == plan.full_mask();
        r_send.bytes = write_snapshot_row(
            stream,
            header,
            p_base_tick,
            plan,
            image,
            header.absolute() ? nullptr : &baseline
        );
    }
    if (r_send.bytes.is_empty()) {
        r_out.staged_out += 1;
        return false;
    }
    r_send.bytes = price(r_send, p_base_tick);
    if (r_send.bytes.is_empty()) {
        r_out.staged_out += 1;
        return false;
    }
    r_send.bits = int64_t(r_send.bytes.size()) * 8;
    r_send.framed = wire::frame_size(r_send.route, r_send.bytes.size());
    return true;
}

void SessionSend::commit_send(
    uint32_t p_peer,
    const RowSend &p_send,
    int64_t p_base_tick,
    int64_t p_now_ms
) {
    store::Table &table = world->table(p_send.table);
    store::PeerRows &rows = table.peers[p_peer];
    const store::RowAddress at{p_send.table, p_send.row};
    if (!p_send.resend) {
        image.assign(
            table.plan,
            table.codes_of(p_send.row),
            table.carried_of(p_send.row)
        );
        world->mint(at, p_peer, image, p_send.form == RowForm::REPAIR);
    }
    rows.repair_at[p_send.row] = p_now_ms;
    rows.acc[p_send.row] = 0.0f;
    if (p_send.form == RowForm::REPAIR) {
        return;
    }
    rows.last_sent[p_send.row] = p_base_tick;
    rows.due.set(p_send.row, false);
    world->arm_refresh(
        at,
        p_peer,
        table.heartbeat > 0 ? p_base_tick + table.heartbeat : -1
    );
}

void SessionSend::pass_peer(
    const wire::WireRegistry &p_registry,
    uint32_t p_peer,
    int64_t p_max_bits,
    int64_t p_base_tick,
    int64_t p_now_ms,
    SessionResult &r_out
) {
    const int id = world->peer_id(p_peer);
    NETW_ZONE_NC("Send to peer", colors::WIRE);
    NETW_ZONE_TEXT_F("peer %d", id);
    candidates.clear();
    PeerAccount &account = refill(id, p_max_bits, p_base_tick);
    const int64_t bytes_before = account.sent_bytes;
    const uint32_t rows_before = r_out.sends.size();
    if (NETW_PROFILE_CONNECTED()) {
        int64_t owed = 0;
        for (uint32_t index = 0; index < world->table_count(); ++index) {
            const store::Table &table = world->table(index);
            if (p_peer < table.peers.size()) {
                owed += table.peers[p_peer].owed.population();
            }
        }
        NETW_PLOT(
            profile::plot_name(
                profile::names::ROWS_OWED,
                id,
                profile::PlotFormat::NUMBER,
                colors::WIRE
            ),
            owed
        );
        NETW_PLOT(
            profile::plot_name(
                profile::names::BYTE_BUDGET,
                id,
                profile::PlotFormat::MEMORY,
                colors::TRANSPORT
            ),
            account.balance
        );
    }
    uint32_t order = 0;
    {
        NETW_ZONE_NC("Send gather", colors::WIRE);
        world->come_due(p_peer, p_base_tick);
        const auto consider = [&](uint32_t p_table,
                                  uint32_t p_row,
                                  bool p_due) {
            NETW_ZONE_LEAF_F(
                "Send consider row",
                colors::WIRE,
                profile::SUBSYSTEM_WIRE,
                "entity %lld",
                static_cast<long long>(world->table(p_table).routes[p_row])
            );
            RowForm form = RowForm::FRESH;
            if (!classify(
                    p_peer,
                    p_table,
                    p_row,
                    p_due,
                    p_now_ms,
                    form,
                    r_out
                )) {
                return;
            }
            store::Table &table = world->table(p_table);
            if (unfitted(p_registry, table)) {
                RowSend send;
                if (!encode(
                        p_peer,
                        p_table,
                        p_row,
                        form,
                        p_base_tick,
                        send,
                        r_out
                    )) {
                    return;
                }
                commit_send(p_peer, send, p_base_tick, p_now_ms);
                account.balance -= double(send.framed);
                account.sent_bytes += send.framed;
                r_out.sends.push_back(send);
                return;
            }
            store::PeerRows &rows = table.peers[p_peer];
            rows.acc[p_row] += store::importance(
                *world,
                table,
                p_row,
                p_base_tick,
                form == RowForm::REFRESH
            );
            Candidate made;
            made.table = p_table;
            made.row = p_row;
            made.order = order++;
            made.acc = rows.acc[p_row];
            made.form = form;
            candidates.push_back(made);
        };
        for (uint32_t index = 0; index < world->table_count(); ++index) {
            store::Table &table = world->table(index);
            if (p_peer >= table.peers.size()) {
                continue;
            }
            const store::PeerRows &rows = table.peers[p_peer];
            const bool window = table.family == store::Family::WINDOW;
            for (uint32_t word = 0; word < rows.sendable.words.size(); ++word) {
                uint64_t bits = rows.sendable.words[word]
                    & (window ? ~uint64_t(0) : rows.owed.words[word]);
                while (bits != 0) {
                    const uint32_t row = word * 64 + store::Bits::lowest(bits);
                    bits &= bits - 1;
                    consider(index, row, rows.due.get(row));
                }
            }
        }
        const LocalVector<uint64_t> &due = world->due_lanes(p_peer);
        for (uint32_t at = 0; at < due.size(); ++at) {
            const store::RowAddress row = world->lane_row(due[at]);
            if (!row.valid()) {
                continue;
            }
            store::Table &table = world->table(row.table);
            store::PeerRows &rows = table.peers[p_peer];
            const bool parked = rows.stream[row.row]
                == uint8_t(store::Stream::DORMANT);
            if (rows.refresh_at[row.row] < 0
                || (!rows.sendable.get(row.row) && !parked)) {
                rows.due.set(row.row, false);
                continue;
            }
            if (!rows.sendable.get(row.row)) {
                continue;
            }
            if (rows.owed.get(row.row)
                || table.family == store::Family::WINDOW) {
                continue;
            }
            consider(row.table, row.row, true);
        }
        offer_nodeless(p_peer, id, p_base_tick, order);
    }
    const auto later = [](const Candidate &p_left, const Candidate &p_right) {
        const bool left_refresh = p_left.form == RowForm::REFRESH;
        const bool right_refresh = p_right.form == RowForm::REFRESH;
        if (left_refresh != right_refresh) {
            return left_refresh;
        }
        if (p_left.acc != p_right.acc) {
            return p_left.acc < p_right.acc;
        }
        return p_left.order > p_right.order;
    };
    Candidate *first = candidates.ptr();
    Candidate *last = first + candidates.size();
    {
        NETW_ZONE_NC("Send order", colors::WIRE);
        std::make_heap(first, last, later);
    }
    const double least_floor = std::min(
        account.fill,
        double(LinkGovernor::FLOOR_BITS) / 8.0
    );
    const double row_floor
        = std::max(account.fill * ROW_FLOOR_SHARE, least_floor);
    const double lent
        = account.balance < row_floor ? row_floor - account.balance : 0.0;
    account.balance += lent;
    {
        NETW_ZONE_NC("Send encode", colors::WIRE);
        account.deferred = 0;
        const int64_t datagram = p_max_bits / 8;
        int64_t room = datagram;
        uint32_t misses = 0;
        while (last != first && account.balance > 0.0
               && misses < MISSES_PER_TICK) {
            std::pop_heap(first, last, later);
            --last;
            const Candidate held = *last;
            if (held.nodeless) {
                if (!take_nodeless(held, id, account)) {
                    misses += 1;
                    r_out.deferred += 1;
                    account.deferred += 1;
                }
                continue;
            }
            NETW_ZONE_LEAF_F(
                "Send encode row",
                colors::WIRE,
                profile::SUBSYSTEM_WIRE,
                "entity %lld",
                static_cast<long long>(
                    world->table(held.table).routes[held.row]
                )
            );
            RowSend send;
            if (!encode(
                    p_peer,
                    held.table,
                    held.row,
                    held.form,
                    p_base_tick,
                    send,
                    r_out
                )) {
                continue;
            }
            const bool opens = send.framed > room && room < datagram
                && account.balance >= double(datagram);
            if (double(send.framed) > account.balance
                || (send.framed > room && room < datagram && !opens)) {
                misses += 1;
                r_out.deferred += 1;
                account.deferred += 1;
                continue;
            }
            if (opens) {
                room = datagram;
            }
            commit_send(p_peer, send, p_base_tick, p_now_ms);
            room -= send.framed;
            account.balance -= double(send.framed);
            account.sent_bytes += send.framed;
            r_out.sent_bits += send.bits;
            r_out.sends.push_back(send);
        }
        const uint32_t waiting = uint32_t(last - first);
        r_out.deferred += waiting;
        account.deferred += waiting;
        emit_nodeless(id, p_max_bits, account, r_out);
    }
    account.balance -= lent;
    bound_debt(account, account.fill);
    world->settle_due(p_peer);
    if (NETW_PROFILE_CONNECTED()) {
        NETW_PLOT(
            profile::plot_name(
                profile::names::ROWS_SENT,
                id,
                profile::PlotFormat::NUMBER,
                colors::WIRE
            ),
            int64_t(r_out.sends.size() - rows_before)
        );
        NETW_PLOT(
            profile::plot_name(
                profile::names::BYTES_SENT,
                id,
                profile::PlotFormat::MEMORY,
                colors::TRANSPORT
            ),
            account.sent_bytes - bytes_before
        );
    }
}

void SessionSend::offer_nodeless(
    uint32_t p_index,
    int p_peer,
    int64_t p_tick,
    uint32_t &r_order
) {
    store::Arrays &arrays = world->arrays;
    if (table_picks.size() < arrays.table_count()) {
        table_picks.resize(arrays.table_count());
    }
    for (uint32_t index = 0; index < arrays.table_count(); ++index) {
        table_picks[index].clear();
    }
    if (!tables_published) {
        return;
    }
    for (uint32_t index = 0; index < arrays.table_count(); ++index) {
        store::ArrayTable &held = arrays.table_at(index);
        if (held.tick < 0 || held.row_count() == 0
            || arrays.wire_id(held.rid) == 0) {
            continue;
        }
        store::ArrayPeer &peer = arrays.peer_of(held, p_peer);
        const auto offer = [&](uint32_t p_row, RowForm p_form) {
            const uint32_t slot = held.slots[p_row];
            if (slot != store::Table::NO_SLOT
                && !world->reaches(slot, p_index)) {
                return;
            }
            const int64_t changed = held.changed_ticks[p_row];
            const bool moving = p_form != RowForm::REFRESH && changed >= 0
                && p_tick - changed < store::MOVING_TICKS;
            peer.acc[p_row] += store::class_weight(
                moving ? store::RowClass::MOVING : store::RowClass::RESTING
            );
            Candidate made;
            made.table = index;
            made.row = p_row;
            made.order = r_order++;
            made.acc = peer.acc[p_row];
            made.form = p_form;
            made.nodeless = true;
            candidates.push_back(made);
        };
        for (uint32_t word = 0; word < peer.owed.words.size(); ++word) {
            uint64_t bits = peer.owed.words[word];
            while (bits != 0) {
                offer(word * 64 + store::Bits::lowest(bits), RowForm::FRESH);
                bits &= bits - 1;
            }
        }
        if (!held.dirty) {
            continue;
        }
        arrays.refresh_rows(held, peer, refreshed);
        for (const uint32_t row : refreshed) {
            offer(row, RowForm::REFRESH);
        }
    }
}

bool SessionSend::take_nodeless(
    const Candidate &p_held,
    int p_peer,
    PeerAccount &r_account
) {
    (void)p_peer;
    store::Arrays &arrays = world->arrays;
    const store::ArrayTable &held = arrays.table_at(p_held.table);
    LocalVector<uint32_t> &picks = table_picks[p_held.table];
    const int64_t price = (held.row_bits + 7) / 8
        + (picks.is_empty() ? arrays.frame_head_bytes() : 0);
    if (double(price) > r_account.balance) {
        return false;
    }
    picks.push_back(p_held.row);
    r_account.balance -= double(price);
    r_account.sent_bytes += price;
    return true;
}

void SessionSend::emit_nodeless(
    int p_peer,
    int64_t p_max_bits,
    PeerAccount &r_account,
    SessionResult &r_out
) {
    store::Arrays &arrays = world->arrays;
    if (!tables_published) {
        return;
    }
    const uint32_t count = MIN(table_picks.size(), arrays.table_count());
    for (uint32_t index = 0; index < count; ++index) {
        store::ArrayTable &held = arrays.table_at(index);
        LocalVector<uint32_t> &picks = table_picks[index];
        if ((picks.is_empty() && !held.dirty) || held.tick < 0
            || held.row_count() == 0 || arrays.wire_id(held.rid) == 0) {
            continue;
        }
        const int64_t charged = picks.is_empty()
            ? 0
            : int64_t(picks.size()) * ((held.row_bits + 7) / 8)
                + arrays.frame_head_bytes();
        picks.sort();
        TypedArray<PackedByteArray> frames;
        arrays.encode_picked(
            held,
            arrays.peer_of(held, p_peer),
            picks,
            int(p_max_bits / 8),
            frames
        );
        int64_t actual = 0;
        for (int at = 0; at < frames.size(); ++at) {
            TableFrame made;
            made.peer = p_peer;
            made.reliable = held.reliable;
            made.bytes = frames[at];
            actual += made.bytes.size();
            r_out.tables.push_back(made);
        }
        r_account.balance -= double(actual - charged);
        r_account.sent_bytes += actual - charged;
        picks.clear();
    }
}

double SessionSend::configured_rate(int p_peer, int64_t p_max_bits) const {
    const PeerAccount *held = accounts.getptr(p_peer);
    if (held != nullptr && held->rate > 0.0) {
        return held->rate;
    }
    if (session_rate > 0.0) {
        return session_rate;
    }
    return double(p_max_bits) / 8.0 * double(tickrate);
}

PeerAccount &SessionSend::refill(
    int p_peer,
    int64_t p_max_bits,
    int64_t p_tick
) {
    PeerAccount &held = accounts[p_peer];
    held.fill = fill_of(p_peer, p_max_bits);
    const int64_t elapsed = held.filled ? p_tick - held.filled_tick : 1;
    held.balance += held.fill * double(elapsed > 1 ? elapsed : 1);
    const double banked = held.fill * BANKED_TICKS;
    if (held.balance > banked) {
        held.balance = banked;
    }
    held.filled = true;
    held.filled_tick = p_tick;
    return held;
}

void SessionSend::set_peer_rate(int p_peer, double p_bytes_per_second) {
    accounts[p_peer].rate = p_bytes_per_second;
}

void SessionSend::charge(int p_peer, int64_t p_bytes) {
    PeerAccount &held = accounts[p_peer];
    held.balance -= double(p_bytes);
    held.sent_bytes += p_bytes;
    bound_debt(held, fill_of(p_peer, row_budget_bits));
}

void SessionSend::bound_debt(PeerAccount &r_account, double p_fill) {
    const double least = -p_fill * DEBT_TICKS;
    if (r_account.balance < least) {
        r_account.balance = least;
    }
}

const SessionResult &SessionSend::run(
    const wire::WireRegistry &p_registry,
    int64_t p_max_bits,
    int64_t p_base_tick,
    int64_t p_now_ms
) {
    SessionResult &out = result;
    out.sends.clear();
    out.ungathered = 0;
    out.caught_up = 0;
    out.deferred = 0;
    out.staged_out = 0;
    out.sent_bits = 0;
    out.tables.clear();
    row_budget_bits = p_max_bits;
    pass_tick = p_base_tick;
    if (world == nullptr) {
        return out;
    }
    record_windows(p_base_tick);
    for (uint32_t peer = 0; peer < world->peer_capacity(); ++peer) {
        if (world->peer_joined(peer)) {
            pass_peer(
                p_registry,
                peer,
                p_max_bits,
                p_base_tick,
                p_now_ms,
                out
            );
        }
    }
    return out;
}

int64_t SessionSend::control_budget_bytes(
    int p_peer,
    int64_t p_reserved_bytes
) const {
    const PeerAccount *held = accounts.getptr(p_peer);
    const int64_t spare_bytes = held != nullptr
        ? int64_t(held->balance)
        : int64_t(
              configured_rate(p_peer, row_budget_bits)
              * double(link.share(p_peer)) / double(tickrate)
          );
    return spare_bytes > p_reserved_bytes ? spare_bytes : p_reserved_bytes;
}

bool SessionSend::describe(const RowSend &p_send, CarrierRow &r_row) {
    if (p_send.reliable) {
        return false;
    }
    r_row.route = p_send.route;
    r_row.comp = p_send.comp;
    r_row.bits = p_send.bits;
    r_row.token = p_send.token;
    r_row.revision = p_send.revision;
    r_row.masked = p_send.masked;
    return true;
}

bool SessionSend::commit(
    int p_peer,
    uint16_t p_seq,
    const LocalVector<CarrierRow> &p_rows,
    int64_t p_frames,
    int64_t p_bits
) {
    return pass.record_datagram(p_peer, p_seq, uint16_t(p_frames), p_bits);
}

void SessionSend::cancel(
    int p_peer,
    const LocalVector<CarrierRow> &p_rows,
    int64_t p_tick
) {
    if (world == nullptr) {
        return;
    }
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        world->stream_withdrawn(p_peer, p_rows[at].token, p_rows[at].revision);
    }
}

RowExplain SessionSend::explain(
    int64_t p_route,
    uint8_t p_comp,
    int p_peer
) const {
    RowExplain out;
    if (world == nullptr) {
        return out;
    }
    const uint32_t peer = world->peer_of(p_peer);
    if (peer == store::Table::NO_SLOT) {
        return out;
    }
    const wire::StreamFamily families[3] = {
        wire::StreamFamily::VOLATILE,
        wire::StreamFamily::RETAINED,
        wire::StreamFamily::WINDOW,
    };
    for (uint32_t family = 0; family < 3; ++family) {
        wire::StreamLane named;
        named.route = p_route;
        named.ordinal = p_comp;
        named.family = families[family];
        const store::RowAddress at = world->lane_row(named);
        if (!at.valid()) {
            continue;
        }
        const store::Table &table = world->table(at.table);
        const store::PeerRows &rows = table.peers[peer];
        out.tick = rows.last_sent[at.row];
        out.confirmed = rows.acked_rev[at.row];
        out.exposed = rows.exposed_rev[at.row];
        out.has_baseline = rows.acked_rev[at.row] != 0;
        out.priority = rows.acc[at.row];
        out.row_class = store::class_name(store::row_class(
            *world,
            table,
            at.row,
            pass_tick,
            rows.due.get(at.row) && !rows.owed.get(at.row)
        ));
        out.age_ticks = rows.last_sent[at.row] >= 0 && pass_tick >= 0
            ? pass_tick - rows.last_sent[at.row]
            : -1;
        if (!rows.sendable.get(at.row)) {
            out.verdict = RowVerdict::UNOFFERED;
        } else if ((table.flags[at.row] & store::Table::UNENCODED) != 0) {
            out.verdict = RowVerdict::UNGATHERED;
        } else if (pass_tick >= 0 && rows.last_sent[at.row] == pass_tick
                   && rows.owed.get(at.row) && rows.acc[at.row] == 0.0f) {
            out.verdict = RowVerdict::SENT;
        } else if (rows.owed.get(at.row) || rows.due.get(at.row)) {
            out.verdict = RowVerdict::DEFERRED;
        } else {
            out.verdict = RowVerdict::CAUGHT_UP;
        }
        break;
    }
    return out;
}

void SessionSend::forget_peer(int p_peer) {
    link.forget(p_peer);
    pass.forget(p_peer);
    readers.forget_peer(p_peer);
    control.forget_peer(p_peer);
    accounts.erase(p_peer);
    if (world != nullptr) {
        world->peer_leave(p_peer);
    }
}

void SessionSend::close_route(int64_t p_route) {
    lanes.close_route(p_route);
    readers.close_route(p_route);
    if (world != nullptr) {
        world->route_closed(p_route);
    }
}

AckReport SessionSend::acknowledge(
    int p_peer,
    uint16_t p_acked_seq,
    uint32_t p_history,
    double p_rtt_ms,
    double p_jitter_ms,
    int64_t p_now_ms
) {
    LocalVector<wire::AckEntry> delivered;
    LocalVector<wire::AckEntry> lost;
    pass.acknowledge(p_peer, p_acked_seq, p_history, delivered, lost);
    link.note_ack(
        p_peer,
        delivered.size(),
        lost.size(),
        p_rtt_ms,
        p_jitter_ms,
        p_now_ms
    );

    AckReport report;
    report.delivered = delivered.size();
    report.lost = lost.size();
    for (uint32_t at = 0; at < delivered.size(); ++at) {
        report.delivered_bits += delivered[at].bits;
    }
    return report;
}

void SessionSend::hide(int p_peer, const wire::StreamLane &p_lane) {
    if (world == nullptr) {
        return;
    }
    const uint64_t token = world->token_of(p_peer, p_lane);
    if (world->stream_hide(p_peer, p_lane) && token != 0) {
        control.drop_token(p_peer, token);
    }
}

} // namespace netw::repl
