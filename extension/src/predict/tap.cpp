#include "netw/predict/tap.hpp"

#include "godot/json.hpp"
#include "godot/os.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/predict_journal_snapshot.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/journal.hpp"

using namespace godot;

namespace netw::predict {

namespace {

const char *TAP_ENV = "NETW_PREDICT_TAP";

bool is_aggregate(const StringName &p_key) {
    return p_key == StringName("arrivals")
        || p_key == StringName("replay_depth")
        || p_key == StringName("consume_shape")
        || p_key == StringName("island_members")
        || p_key == StringName("simulated_members");
}

} // namespace

Dictionary tap_stats_delta(
    Dictionary &r_carried,
    const Dictionary &p_stats,
    int64_t p_lines
) {
    const bool aggregates_due = p_lines % TAP_AGGREGATE_PERIOD == 0;
    Dictionary out;
    const Array keys = p_stats.keys();
    for (int at = 0; at < keys.size(); ++at) {
        const StringName key = keys[at];
        if (is_aggregate(key)) {
            if (aggregates_due) {
                out[key] = p_stats[key];
            }
            continue;
        }
        if (!r_carried.has(key) || r_carried[key] != p_stats[key]) {
            out[key] = p_stats[key];
            r_carried[key] = p_stats[key];
        }
    }
    return out;
}

String tap_episode_stamp(const Dictionary &p_digest) {
    if (p_digest.is_empty()) {
        return String();
    }
    return String::num_int64(int64_t(p_digest.get(StringName("id"), -1)))
        + String(":")
        + String::num_int64(int64_t(p_digest.get(StringName("revision"), -1)));
}

bool tap_row_settled(int p_flags) {
    return (p_flags & (ROW_ACKED | ROW_SUBSTITUTED | ROW_SUPERSEDED)) != 0;
}

Dictionary tap_settlement(const Dictionary &p_row) {
    Dictionary out;
    out[StringName("transition")] = p_row.get(StringName("transition"), -1);
    out[StringName("flags")] = p_row.get(StringName("flags"), 0);
    out[StringName("attribution")] = p_row.get(StringName("attribution"), 0);
    out[StringName("domain")] = p_row.get(StringName("domain"), 0);
    out[StringName("aligned_error")]
        = p_row.get(StringName("aligned_error"), 0.0);
    return out;
}

String Tap::armed_dir() {
    return OS::get_singleton()->get_environment(TAP_ENV);
}

void Tap::open(const String &p_dir) {
    dir = p_dir;
    last_flush_usec = Time::get_singleton()->get_ticks_usec();
}

TapState &Tap::state_for(int64_t p_slot, int64_t p_epoch) {
    if (!states.has(p_slot)) {
        TapState fresh;
        fresh.epoch = p_epoch;
        states.insert(p_slot, fresh);
    }
    return states[p_slot];
}

Ref<FileAccess> Tap::file_for(const StringName &p_entity_id, int64_t p_slot) {
    TapState &state = states[p_slot];
    if (state.file.is_valid()) {
        return state.file;
    }
    const String basename = String(p_entity_id);
    String claimed = basename;
    int ordinal = 2;
    while (names.has(claimed) && names[claimed] != p_slot) {
        claimed = basename + String("~") + String::num_int64(ordinal);
        ordinal += 1;
    }
    names.insert(claimed, p_slot);
    state.file = FileAccess::open(
        dir.path_join(claimed + String(".jsonl")),
        FileAccess::WRITE
    );
    return state.file;
}

void Tap::drain(
    const StringName &p_entity_id,
    NetwPredictionEngine &p_pool,
    int64_t p_slot,
    const Dictionary &p_stats
) {
    if (dir.is_empty() || p_slot < 0) {
        return;
    }
    const Ref<NetwPredictJournal> journal = p_pool.journal_snapshot(p_slot);
    if (journal.is_null()) {
        return;
    }
    const int64_t began = Time::get_singleton()->get_ticks_usec();
    drains += 1;

    TapState &state = state_for(p_slot, journal->epoch());
    Array rows;
    Array settles;
    const String episode_stamp
        = tap_episode_stamp(p_pool.episode_digest(p_slot));

    if (state.epoch != journal->epoch()) {
        const Array lost = state.pending.keys();
        for (int at = 0; at < lost.size(); ++at) {
            Dictionary row;
            row[StringName("transition")] = lost[at];
            row[StringName("lost")] = true;
            settles.push_back(row);
        }
        state.epoch = journal->epoch();
        state.last_exported = -1;
        state.pending = Dictionary();
    }

    const Array waiting = state.pending.keys();
    for (int at = 0; at < waiting.size(); ++at) {
        const int64_t transition = waiting[at];
        const Dictionary row = journal->row_at(transition);
        if (row.is_empty()) {
            Dictionary gone;
            gone[StringName("transition")] = transition;
            gone[StringName("lost")] = true;
            settles.push_back(gone);
            state.pending.erase(transition);
        } else if (tap_row_settled(int(row.get(StringName("flags"), 0)))) {
            settles.push_back(tap_settlement(row));
            state.pending.erase(transition);
        }
    }

    const int64_t closed = journal->last_closed();
    const PackedInt64Array transitions = journal->transitions();
    for (int at = 0; at < transitions.size(); ++at) {
        const int64_t transition = transitions[at];
        if (transition <= state.last_exported) {
            continue;
        }
        if (transition > closed) {
            break;
        }
        const Dictionary row = journal->row_at(transition);
        if (row.is_empty()) {
            continue;
        }
        rows.push_back(row);
        state.last_exported = transition;
        if (tap_row_settled(int(row.get(StringName("flags"), 0)))) {
            settles.push_back(tap_settlement(row));
        } else {
            state.pending[transition] = true;
        }
    }

    const bool episode_changed = episode_stamp != state.episode_stamp;
    if (rows.is_empty() && settles.is_empty() && !episode_changed) {
        drain_usec += Time::get_singleton()->get_ticks_usec() - began;
        return;
    }
    const Ref<FileAccess> file = file_for(p_entity_id, p_slot);
    if (file.is_null()) {
        drain_usec += Time::get_singleton()->get_ticks_usec() - began;
        return;
    }

    Dictionary line;
    line[StringName("stats")]
        = tap_stats_delta(state.carried, p_stats, state.lines);
    line[StringName("rows")] = rows;
    line[StringName("settles")] = settles;
    line[StringName("episode")]
        = episode_changed ? p_pool.episode_record(p_slot) : Dictionary();

    const String text = JSON::stringify(line);
    file->store_line(text);
    bytes_written += text.length() + 1;
    lines_written += 1;
    state.lines += 1;
    state.episode_stamp = episode_stamp;

    const int64_t now = Time::get_singleton()->get_ticks_usec();
    if (now - last_flush_usec >= TAP_FLUSH_INTERVAL_USEC) {
        flush();
    }
    drain_usec += now - began;
}

void Tap::flush() {
    for (KeyValue<int64_t, TapState> &any : states) {
        if (any.value.file.is_valid()) {
            any.value.file->flush();
        }
    }
    last_flush_usec = Time::get_singleton()->get_ticks_usec();
}

Dictionary Tap::cost() const {
    Dictionary out;
    out[StringName("bytes")] = bytes_written;
    out[StringName("lines")] = lines_written;
    out[StringName("drains")] = drains;
    out[StringName("mean_drain_usec")]
        = drains > 0 ? double(drain_usec) / double(drains) : 0.0;
    return out;
}

void Tap::close() {
    for (KeyValue<int64_t, TapState> &any : states) {
        if (any.value.file.is_valid()) {
            any.value.file->close();
        }
    }
    states.clear();
    names.clear();
    if (drains > 0) {
        const Dictionary report = cost();
        gd::print(
            String("[tap] wrote ") + String::num_int64(bytes_written)
            + String(" bytes over ") + String::num_int64(lines_written)
            + String(" lines, ") + String::num_int64(drains)
            + String(" drains, ")
            + String::num_real(double(report[StringName("mean_drain_usec")]))
            + String(" us mean")
        );
    }
}

} // namespace netw::predict
