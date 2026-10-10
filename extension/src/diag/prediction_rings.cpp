#include "netw/diag/prediction_rings.hpp"

#if defined(DEBUG_ENABLED)

#include <iterator>

#include "godot/display_server.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "netw/api/display.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/prediction.hpp"
#include "netw/clock_engine.hpp"
#include "netw/diag/hints.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/store/table.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace netw::diag {

namespace {

int pose_field_of(const predict::Wiring *p_wiring) {
    if (p_wiring == nullptr) {
        return -1;
    }
    for (int at = 0; at < p_wiring->count(); ++at) {
        if (uint32_t(at) < p_wiring->pose.size() && p_wiring->pose[at] != 0) {
            return at;
        }
    }
    for (int at = 0; at < p_wiring->count(); ++at) {
        const StringName &name = p_wiring->fields.name_at(at);
        if (name == StringName("position")
            || name == StringName("global_position")
            || name == StringName("transform")) {
            return at;
        }
    }
    return -1;
}

const char *const OPERATOR_NAMES[] = {
    "",
    "REBASE_PROJECTED",
    "REBASE_EXACT",
    "FULL_CLOSURE",
    "TRANSPORT_DELTA",
    "RESEED",
    "RESEED_ALIGN",
    "DEMOTE",
    "DISSIPATE",
    "JOINT_REBASE",
};

const char *const OUTCOME_NAMES[] = {
    "pending",
    "contracted",
    "failed to contract",
    "introduced boundary",
    "withheld",
};

const char *state_word(int p_state) {
    switch (p_state) {
        case StateLine::QUARANTINE:
            return "QUARANTINE";
        case StateLine::DEMOTED:
            return "DEMOTED";
        case StateLine::OPEN:
            return "OPEN";
        case StateLine::CLOSED:
            return "CLOSED";
        case StateLine::FALLBACK:
            return "FALLBACK";
        default:
            return "no episode";
    }
}

int64_t hundredths(double p_value) {
    return int64_t(Math::round(p_value * 100.0));
}

StateLine state_line_of(const predict::Slot &p_slot, const RingRow &p_row) {
    StateLine line;
    line.flat = p_row.flat;
    if (p_row.role == NetwPrediction::ROLE_SIMULATE) {
        line.replica = true;
        if (p_row.has(MARK_SERVER)) {
            line.age_hundredths = hundredths(p_row.confirmed_age);
            line.drift_hundredths
                = hundredths((p_row.simulated - p_row.server).length());
        }
        return line;
    }
    const predict::Episode &episode = p_slot.episode;
    if (p_slot.quarantine.latched) {
        line.state = StateLine::QUARANTINE;
    } else if (episode.demoted) {
        line.state = StateLine::DEMOTED;
    } else if (episode.active) {
        line.state = int(episode.state);
    }
    if (!episode.writes.is_empty()) {
        const predict::EpisodeWrite &write
            = episode.writes[episode.writes.size() - 1];
        line.op = int(write.op);
        line.outcome = int(write.outcome);
    } else if (!episode.decisions.is_empty()) {
        line.op = int(episode.decisions[episode.decisions.size() - 1].op);
        line.outcome = int(predict::OperatorOutcome::REFUSED);
    }
    line.gap_hundredths = p_row.has(MARK_SERVER) && p_row.has(MARK_OURS)
        ? hundredths(p_row.gap)
        : -1;
    line.ack = p_slot.stats.ack_age_ticks;
    return line;
}

String distance_words(int64_t p_hundredths, bool p_flat) {
    const double value = double(p_hundredths) / 100.0;
    return p_flat ? vformat("%d px", int64_t(Math::round(value)))
                  : vformat("%.2f m", value);
}

const Color SHOWN_COLOR(0.25f, 0.9f, 1.0f);
const Color SIMULATED_COLOR(0.35f, 1.0f, 0.35f);
const Color SERVER_COLOR(1.0f, 0.6f, 0.1f);
const Color OURS_COLOR(1.0f, 0.3f, 0.9f);
const Color ERROR_COLOR(1.0f, 1.0f, 1.0f);

Color faded(const Color &p_color, float p_alpha) {
    return Color(p_color.r, p_color.g, p_color.b, p_color.a * p_alpha);
}

} // namespace

bool StateLine::wanted(int p_role) const {
    return p_role == NetwPrediction::ROLE_PREDICT
        || p_role == NetwPrediction::ROLE_SIMULATE || state == QUARANTINE
        || state == DEMOTED || state == OPEN || state == FALLBACK;
}

bool StateLine::operator==(const StateLine &p_other) const {
    return state == p_other.state && op == p_other.op
        && outcome == p_other.outcome && flat == p_other.flat
        && replica == p_other.replica
        && gap_hundredths == p_other.gap_hundredths && ack == p_other.ack
        && age_hundredths == p_other.age_hundredths
        && drift_hundredths == p_other.drift_hundredths;
}

String StateLine::text() const {
    if (replica) {
        if (age_hundredths < 0) {
            return "replica  no server state";
        }
        return vformat(
                   "replica  server %.2f s ago\n",
                   double(age_hundredths) / 100.0
               )
            + "drift " + distance_words(drift_hundredths, flat);
    }
    String first = state_word(state);
    const int names = int(std::size(OPERATOR_NAMES));
    if (op > 0 && op < names) {
        first += String("  ") + OPERATOR_NAMES[op];
        const int outcomes = int(std::size(OUTCOME_NAMES));
        first += outcome >= 0 && outcome < outcomes
            ? String(" ") + OUTCOME_NAMES[outcome]
            : String(" refused");
    }
    String second = vformat("ack age %d", ack);
    if (gap_hundredths >= 0) {
        second
            = "error " + distance_words(gap_hundredths, flat) + "  " + second;
    }
    return first + "\n" + second;
}

bool PredictionRings::wanted() {
    if ((hints() & HINT_PREDICTION) == 0) {
        return false;
    }
    DisplayServer *display = DisplayServer::get_singleton();
    return display != nullptr && display->get_name() != String("headless");
}

PredictionRings::PredictionRings() {
    sources.reserve(ENTITY_CAP);
    candidates.reserve(ENTITY_CAP);
    sampled.reserve(ENTITY_CAP);
}

void draw_ring_rows(const LocalVector<RingRow> &p_rows, Drawing &r_drawing) {
    for (const RingRow &row : p_rows) {
        const float scale = row.flat ? PredictionRings::PIXELS_PER_METRE : 1.0f;
        Mark mark;
        mark.world = row.world;
        mark.viewport = row.viewport;
        mark.flat = row.flat;
        mark.point = row.shown;
        mark.radius = PredictionRings::RADIUS_SHOWN * scale;
        mark.color = SHOWN_COLOR;
        r_drawing.marks.push_back(mark);
        mark.point = row.simulated;
        mark.radius = PredictionRings::RADIUS_SIMULATED * scale;
        mark.color = SIMULATED_COLOR;
        r_drawing.marks.push_back(mark);
        if (row.has(MARK_SERVER)) {
            mark.point = row.server;
            mark.radius = PredictionRings::RADIUS_SERVER * scale;
            mark.color = faded(SERVER_COLOR, row.confirmed_alpha);
            r_drawing.marks.push_back(mark);
        }
        if (row.has(MARK_OURS)) {
            mark.shape = SHAPE_DASHED_RING;
            mark.point = row.ours;
            mark.radius = PredictionRings::RADIUS_OURS * scale;
            mark.color = faded(OURS_COLOR, row.confirmed_alpha);
            r_drawing.marks.push_back(mark);
        }
        if (row.erred()) {
            Segment error;
            error.world = row.world;
            error.viewport = row.viewport;
            error.flat = row.flat;
            error.from = row.server;
            error.to = row.ours;
            error.color = faded(ERROR_COLOR, row.confirmed_alpha);
            r_drawing.segments.push_back(error);
        }
        if (!row.text.is_empty()) {
            Text text;
            text.world = row.world;
            text.viewport = row.viewport;
            text.flat = row.flat;
            text.anchor = row.simulated;
            text.text = row.text;
            r_drawing.texts.push_back(text);
        }
    }
}

void PredictionRings::sample(NetwMultiplayer &p_session) {
    ++generation;
    sampled.clear();
    cameras.clear();
    candidates.clear();
    NetwPredictionEngine *pool = p_session.get_prediction_engine();
    if (pool == nullptr) {
        forget_departed();
        return;
    }
    candidates.reserve(sources.size());
    for (uint32_t at = 0; at < sources.size(); ++at) {
        const RingSource &source = sources[at];
        const int role = pool->role_of(source.slot);
        if (role != NetwPrediction::ROLE_PREDICT
            && role != NetwPrediction::ROLE_SIMULATE) {
            continue;
        }
        Node *owner = source.owner;
        Candidate made;
        if (!owner->is_inside_tree()
            || !global_point_of(owner, made.simulated)) {
            continue;
        }
        made.world = world_of(owner);
        if (made.world == 0) {
            continue;
        }
        made.source = at;
        made.role = role;
        made.distance
            = (made.simulated - cameras.point(made.world, owner)).length();
        seen[source.key].generation = generation;
        candidates.push_back(made);
    }
    keep_nearest_per_world(candidates, ENTITY_CAP);
    const ClockEngine &clock = p_session.clock_engine();
    const Clock now{
        clock.get_tick(),
        1.0 / double(MAX(1, clock.get_tickrate())),
    };
    for (const Candidate &each : candidates) {
        read(p_session, *pool, each, now);
    }
    forget_departed();
}

void PredictionRings::read(
    NetwMultiplayer &p_session,
    NetwPredictionEngine &p_pool,
    const Candidate &p_candidate,
    const Clock &p_now
) {
    NetwPredictionEngine *const pool = &p_pool;
    const RingSource &source = sources[p_candidate.source];
    const int role = p_candidate.role;
    Node *owner = source.owner;
    RingRow row;
    row.simulated = p_candidate.simulated;
    row.world = p_candidate.world;
    row.camera_distance = p_candidate.distance;
    row.key = source.key;
    row.viewport = uint64_t(owner->get_viewport()->get_instance_id());
    Seen &entry = seen[source.key];
    const predict::Wiring *wiring = pool->wiring_of(source.slot);
    if (!entry.resolved || entry.wiring != wiring) {
        entry.resolved = true;
        entry.wiring = wiring;
        entry.field = pose_field_of(wiring);
        entry.key = entry.field >= 0 ? wiring->fields.name_at(entry.field)
                                     : StringName();
        entry.global_key = global_key(entry.key);
        entry.parent_key = parent_relative(entry.key);
        entry.store_resolved = false;
    }
    if (entry.field < 0) {
        return;
    }
    if (!entry.visual_resolved) {
        entry.visual_resolved = true;
        const Ref<NetwDisplay> display = source.entity != nullptr
            ? source.entity->get_display()
            : Ref<NetwDisplay>();
        Node *visual
            = display.is_valid() && !display->get_visual_root().is_empty()
            ? owner->get_node_or_null(display->get_visual_root())
            : nullptr;
        entry.visual
            = visual != nullptr ? uint64_t(visual->get_instance_id()) : 0;
    }
    Node *visual = entry.visual != 0
        ? Object::cast_to<Node>(gd::object_of(ObjectID(entry.visual)))
        : nullptr;
    if (visual == nullptr || !global_point_of(visual, row.shown)) {
        row.shown = row.simulated;
    }
    row.flat = Object::cast_to<Node2D>(owner) != nullptr;
    row.role = role;
    row.marks = MARK_SHOWN | MARK_SIMULATED;
    row.epsilon = pool->epsilon_of(source.slot, entry.field);
    if (row.epsilon < 0.0) {
        row.epsilon = pool->epsilon_of_slot(source.slot);
    }
    int64_t confirmed_tick = -1;
    if (role == NetwPrediction::ROLE_PREDICT) {
        const predict::EvaluationRecord *record
            = pool->last_evaluation_of(source.slot);
        if (record != nullptr && record->serial > 0) {
            const predict::StateRow &received = pool->received_row(source.slot);
            const predict::StateRow &compared = pool->compared_row(source.slot);
            Vector3 server;
            Vector3 ours;
            if (received.has(entry.field)
                && point_of(received.values[entry.field], server)) {
                row.server
                    = lifted(owner, entry.parent_key, entry.global_key, server);
                row.marks |= MARK_SERVER;
            }
            if (compared.has(entry.field)
                && point_of(compared.values[entry.field], ours)) {
                row.ours
                    = lifted(owner, entry.parent_key, entry.global_key, ours);
                row.marks |= MARK_OURS;
            }
            if (row.has(MARK_SERVER) && row.has(MARK_OURS)) {
                row.gap = (server - ours).length();
            }
            confirmed_tick = record->recv_tick;
        }
    } else {
        if (!entry.store_resolved) {
            entry.store_resolved = true;
            entry.has_store = p_session.display_table_of(
                owner,
                entry.key,
                entry.store_owner,
                entry.store_lane,
                entry.store_column
            );
        }
        const store::RowAddress at = entry.has_store
            ? p_session.store().row_of(entry.store_owner, entry.store_lane)
            : store::RowAddress();
        if (at.valid()) {
            const store::Table &table = p_session.store().table(at.table);
            Vector3 server;
            if (table.sources[at.row] == uint8_t(store::Source::REMOTE)
                && point_of(
                    table.read_value(at.row, entry.store_column),
                    server
                )) {
                row.server
                    = lifted(owner, entry.parent_key, entry.global_key, server);
                row.marks |= MARK_SERVER;
                confirmed_tick = table.received_ticks[at.row];
            }
        }
    }
    if (confirmed_tick >= 0) {
        row.confirmed_age
            = double(p_now.tick - confirmed_tick) * p_now.tick_seconds;
        row.confirmed_alpha
            = row.confirmed_age < FRESH_SECONDS ? 1.0f : STALE_ALPHA;
    }
    const predict::Slot *slot_row = pool->diag_row_of(source.slot);
    if (slot_row != nullptr) {
        const StateLine line = state_line_of(*slot_row, row);
        if (line.wanted(role)) {
            if (!(line == entry.line)) {
                entry.line = line;
                entry.text = line.text();
            }
            row.text = entry.text;
        }
    }
    sampled.push_back(row);
}

void PredictionRings::forget_departed() {
    departed.clear();
    for (const KeyValue<int64_t, Seen> &each : seen) {
        if (each.value.generation != generation) {
            departed.push_back(each.key);
        }
    }
    for (const int64_t key : departed) {
        seen.erase(key);
    }
}

} // namespace netw::diag

#endif
