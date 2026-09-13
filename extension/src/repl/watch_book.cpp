#include "netw/repl/watch_book.hpp"

#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw::repl {

using namespace godot;

namespace {

// A container the game keeps mutating would alias the stored copy, so the poll
// would compare it against itself and never see it change again. The packed
// families are values and need no copy.
Variant stable_copy(const Variant &p_value) {
    switch (p_value.get_type()) {
        case Variant::ARRAY:
            return Array(p_value).duplicate(true);
        case Variant::DICTIONARY:
            return Dictionary(p_value).duplicate(true);
        default:
            return p_value;
    }
}

bool readable_at(const Array &p_readable, int64_t p_index) {
    return p_index >= p_readable.size() || bool(p_readable[p_index]);
}

} // namespace

WatchBook::Stream *WatchBook::stream_for(int64_t p_key) {
    HashMap<int64_t, Stream>::Iterator found = streams.find(p_key);
    return found == streams.end() ? nullptr : &found->value;
}

const WatchBook::Stream *WatchBook::stream_for(int64_t p_key) const {
    HashMap<int64_t, Stream>::ConstIterator found = streams.find(p_key);
    return found == streams.end() ? nullptr : &found->value;
}

void WatchBook::poll(
    int64_t p_key,
    const Array &p_values,
    const Array &p_readable
) {
    NETW_ZONE_NC("Watch poll", colors::WIRE);
    Stream &stream = streams[p_key];
    if (!stream.inited) {
        stream.change_counter = 1;
        for (int64_t i = 0; i < p_values.size(); ++i) {
            const bool ok = readable_at(p_readable, i);
            stream.values.push_back(ok ? stable_copy(p_values[i]) : Variant());
            stream.stamps.push_back(ok ? 1 : 0);
        }
        stream.inited = true;
        return;
    }
    for (int64_t i = 0; i < p_values.size() && i < stream.values.size(); ++i) {
        if (!readable_at(p_readable, i)) {
            continue;
        }
        const Variant current = p_values[i];
        const Variant previous = stream.values[i];
        if (current.get_type() == previous.get_type() && current == previous) {
            continue;
        }
        stream.change_counter += 1;
        stream.values[i] = stable_copy(current);
        stream.stamps[i] = stream.change_counter;
    }
}

void WatchBook::mask_for(
    int64_t p_key,
    int64_t p_peer,
    uint64_t &r_mask,
    Array &r_values
) const {
    r_mask = 0;
    r_values.clear();
    const Stream *stream = stream_for(p_key);
    if (stream == nullptr || !stream->inited) {
        return;
    }
    HashMap<int64_t, int64_t>::ConstIterator found
        = stream->baselines.find(p_peer);
    const int64_t baseline
        = found == stream->baselines.end() ? 0 : found->value;
    for (uint32_t i = 0; i < stream->stamps.size(); ++i) {
        if (stream->stamps[i] > baseline) {
            r_mask |= uint64_t(1) << i;
            r_values.push_back(stream->values[int64_t(i)]);
        }
    }
}

void WatchBook::commit(int64_t p_key, int64_t p_peer) {
    Stream *stream = stream_for(p_key);
    if (stream != nullptr) {
        stream->baselines[p_peer] = stream->change_counter;
    }
}

void WatchBook::reset(int64_t p_key) {
    streams.erase(p_key);
}

void WatchBook::clear_baselines(int64_t p_key) {
    Stream *stream = stream_for(p_key);
    if (stream != nullptr) {
        stream->baselines.clear();
    }
}

void WatchBook::clear_peer(int64_t p_peer) {
    for (KeyValue<int64_t, Stream> &entry : streams) {
        entry.value.baselines.erase(p_peer);
    }
}

void WatchBook::retain_baselines(
    int64_t p_key,
    const PackedInt32Array &p_recipients
) {
    Stream *stream = stream_for(p_key);
    if (stream == nullptr) {
        return;
    }
    LocalVector<int64_t> doomed;
    for (const KeyValue<int64_t, int64_t> &entry : stream->baselines) {
        bool kept = false;
        for (int64_t i = 0; i < p_recipients.size(); ++i) {
            if (int64_t(p_recipients[i]) == entry.key) {
                kept = true;
                break;
            }
        }
        if (!kept) {
            doomed.push_back(entry.key);
        }
    }
    for (int64_t peer : doomed) {
        stream->baselines.erase(peer);
    }
}

bool WatchBook::is_inited(int64_t p_key) const {
    const Stream *stream = stream_for(p_key);
    return stream != nullptr && stream->inited;
}

void WatchBook::clear() {
    streams.clear();
}

} // namespace netw::repl
