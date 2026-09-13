#include "netw/wire/attribution.hpp"

namespace netw::wire {

using namespace godot;

uint64_t AttributionBook::lane_key(
    int64_t p_peer,
    bool p_reliable,
    bool p_carrier
) {
    return (uint64_t(uint32_t(p_peer)) << 2) | (p_reliable ? 2u : 0u)
        | (p_carrier ? 1u : 0u);
}

uint64_t AttributionBook::peer_channel_key(int64_t p_peer, int64_t p_channel) {
    return (uint64_t(uint32_t(p_peer)) << 8) | uint64_t(uint8_t(p_channel));
}

uint64_t AttributionBook::refusal_key(
    int64_t p_peer,
    int64_t p_channel,
    Refusal p_refusal
) {
    return (peer_channel_key(p_peer, p_channel) << 8)
        | uint64_t(uint8_t(p_refusal));
}

const char *refusal_name(Refusal p_refusal) {
    switch (p_refusal) {
        case Refusal::NONE:
            return "none";
        case Refusal::GATE:
            return "gate";
        case Refusal::STALE:
            return "stale";
        case Refusal::UNROUTED:
            return "unrouted";
        case Refusal::UNBOUND:
            return "unbound";
        case Refusal::MALFORMED:
            return "malformed";
        case Refusal::BASELINE_UNKNOWN:
            return "baseline_unknown";
        case Refusal::COUNT:
            return "?";
    }
    return "?";
}

void AttributionBook::stage(
    bool p_reliable,
    bool p_carrier,
    const Attribution &p_frame
) {
    const uint64_t key = lane_key(p_frame.peer, p_reliable, p_carrier);
    Vector<Attribution> *run = staged.getptr(key);
    if (run == nullptr) {
        staged.insert(key, Vector<Attribution>());
        run = staged.getptr(key);
    }
    run->push_back(p_frame);
}

int64_t AttributionBook::commit(
    int64_t p_peer,
    bool p_reliable,
    bool p_carrier,
    int64_t p_seq
) {
    const uint64_t key = lane_key(p_peer, p_reliable, p_carrier);
    Vector<Attribution> *run = staged.getptr(key);
    if (run == nullptr) {
        return 0;
    }
    int64_t bits = 0;
    for (int at = 0; at < run->size(); ++at) {
        Attribution frame = (*run)[at];
        frame.seq = p_seq;
        bits += frame.bits;
        const int64_t bytes = frame.bits / 8;
        attributed_out += bytes;
        frames_out += 1;
        bytes_out[peer_channel_key(frame.peer, frame.channel)] += bytes;
        route_out[frame.route] += bytes;
    }
    staged.erase(key);
    return bits / 8;
}

void AttributionBook::discard(int64_t p_peer, bool p_reliable, bool p_carrier) {
    const uint64_t key = lane_key(p_peer, p_reliable, p_carrier);
    const Vector<Attribution> *run = staged.getptr(key);
    if (run == nullptr) {
        return;
    }
    for (int at = 0; at < run->size(); ++at) {
        staged_dropped_out += (*run)[at].bits / 8;
    }
    staged.erase(key);
}

void AttributionBook::discard_peer(int64_t p_peer) {
    discard(p_peer, false, false);
    discard(p_peer, false, true);
    discard(p_peer, true, false);
    discard(p_peer, true, true);
}

void AttributionBook::note_framing_out(int64_t p_bytes) {
    framing_out += p_bytes;
}

void AttributionBook::note_datagram(bool p_inbound, int64_t p_bytes) {
    if (p_inbound) {
        wire_in += p_bytes;
        datagrams_in += 1;
        return;
    }
    wire_out += p_bytes;
    datagrams_out += 1;
}

void AttributionBook::observe_in(const Attribution &p_frame) {
    const int64_t bytes = p_frame.bits / 8;
    attributed_in += bytes;
    frames_in += 1;
    bytes_in[peer_channel_key(p_frame.peer, p_frame.channel)] += bytes;
    frames_by_peer_channel_in[peer_channel_key(p_frame.peer, p_frame.channel)]
        += 1;
    route_in[p_frame.route] += bytes;
}

void AttributionBook::note_refusal(
    int64_t p_peer,
    int64_t p_channel,
    Refusal p_refusal
) {
    if (p_refusal == Refusal::NONE) {
        return;
    }
    refused_in += 1;
    refusals[refusal_key(p_peer, p_channel, p_refusal)] += 1;
}

void AttributionBook::note_column(
    int64_t p_schema,
    int64_t p_column,
    int64_t p_bits
) {
    if (!armed) {
        return;
    }
    column_bits
        [(uint64_t(uint32_t(p_schema)) << 16) | uint64_t(uint16_t(p_column))]
        += p_bits;
}

int64_t AttributionBook::peer_channel_out(
    int64_t p_peer,
    int64_t p_channel
) const {
    const HashMap<uint64_t, int64_t>::ConstIterator found
        = bytes_out.find(peer_channel_key(p_peer, p_channel));
    return found != bytes_out.end() ? found->value : 0;
}

int64_t AttributionBook::peer_channel_in(
    int64_t p_peer,
    int64_t p_channel
) const {
    const HashMap<uint64_t, int64_t>::ConstIterator found
        = bytes_in.find(peer_channel_key(p_peer, p_channel));
    return found != bytes_in.end() ? found->value : 0;
}

int64_t AttributionBook::peer_channel_refused(
    int64_t p_peer,
    int64_t p_channel,
    Refusal p_refusal
) const {
    const HashMap<uint64_t, int64_t>::ConstIterator found
        = refusals.find(refusal_key(p_peer, p_channel, p_refusal));
    return found != refusals.end() ? found->value : 0;
}

static Dictionary peer_channel_rows(const HashMap<uint64_t, int64_t> &p_table) {
    Dictionary out;
    for (const KeyValue<uint64_t, int64_t> &row : p_table) {
        const int64_t peer = int64_t(int32_t(row.key >> 8));
        const int64_t channel = int64_t(row.key & 0xff);
        Dictionary channels = out.get(peer, Dictionary());
        channels[channel] = row.value;
        out[peer] = channels;
    }
    return out;
}

static Dictionary route_rows(const HashMap<int64_t, int64_t> &p_table) {
    Dictionary out;
    for (const KeyValue<int64_t, int64_t> &row : p_table) {
        out[row.key] = row.value;
    }
    return out;
}

Dictionary AttributionBook::snapshot() const {
    Dictionary out;
    out[StringName("bytes_out")] = peer_channel_rows(bytes_out);
    out[StringName("bytes_in")] = peer_channel_rows(bytes_in);
    out[StringName("route_bytes_out")] = route_rows(route_out);
    out[StringName("route_bytes_in")] = route_rows(route_in);
    out[StringName("attributed_out")] = attributed_out;
    out[StringName("attributed_in")] = attributed_in;
    out[StringName("framing_out")] = framing_out;
    out[StringName("frames_out")] = frames_out;
    out[StringName("frames_in")] = frames_in;
    out[StringName("staged_dropped_out")] = staged_dropped_out;
    out[StringName("frames_in_by_channel")]
        = peer_channel_rows(frames_by_peer_channel_in);
    out[StringName("refused_in")] = refused_in;
    Dictionary refused;
    for (const KeyValue<uint64_t, int64_t> &row : refusals) {
        const int64_t peer = int64_t(int32_t(row.key >> 16));
        const int64_t channel = int64_t((row.key >> 8) & 0xff);
        const Refusal verdict = Refusal(uint8_t(row.key & 0xff));
        Dictionary channels = refused.get(peer, Dictionary());
        Dictionary verdicts = channels.get(channel, Dictionary());
        verdicts[String(refusal_name(verdict))] = row.value;
        channels[channel] = verdicts;
        refused[peer] = channels;
    }
    out[StringName("refusals")] = refused;
    out[StringName("armed")] = armed;
    Dictionary columns;
    for (const KeyValue<uint64_t, int64_t> &row : column_bits) {
        Dictionary cell;
        cell[StringName("schema")] = int64_t(int32_t(row.key >> 16));
        cell[StringName("column")] = int64_t(row.key & 0xffff);
        cell[StringName("bits")] = row.value;
        columns[int64_t(row.key)] = cell;
    }
    out[StringName("columns")] = columns;
    return out;
}

void AttributionBook::clear() {
    bytes_out.clear();
    bytes_in.clear();
    route_out.clear();
    route_in.clear();
    column_bits.clear();
    frames_by_peer_channel_in.clear();
    refusals.clear();
    staged.clear();
    refused_in = 0;
    wire_out = 0;
    wire_in = 0;
    datagrams_out = 0;
    datagrams_in = 0;
    attributed_out = 0;
    attributed_in = 0;
    framing_out = 0;
    frames_out = 0;
    frames_in = 0;
    staged_dropped_out = 0;
}

} // namespace netw::wire
