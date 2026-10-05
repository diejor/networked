#include "netw/diag/prediction_rings.hpp"

#if defined(DEBUG_ENABLED)

#include <algorithm>
#include <iterator>

#include "godot/camera.hpp"
#include "godot/display_server.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "godot/viewport.hpp"
#include "godot/world.hpp"
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

bool point_of(const Variant &p_value, Vector3 &r_point) {
    switch (p_value.get_type()) {
        case Variant::VECTOR3:
            r_point = p_value;
            return true;
        case Variant::VECTOR2: {
            const Vector2 flat = p_value;
            r_point = Vector3(flat.x, flat.y, 0.0f);
            return true;
        }
        case Variant::TRANSFORM3D:
            r_point = Transform3D(p_value).origin;
            return true;
        case Variant::TRANSFORM2D: {
            const Vector2 origin = Transform2D(p_value).get_origin();
            r_point = Vector3(origin.x, origin.y, 0.0f);
            return true;
        }
        default:
            return false;
    }
}

bool parent_relative(const StringName &p_key) {
    return p_key == StringName("position") || p_key == StringName("transform");
}

Vector3 lifted(
    Node *p_owner,
    bool p_parent_key,
    bool p_global_key,
    const Vector3 &p_point
) {
    if (p_global_key) {
        return p_point;
    }
    if (Node3D *spatial = Object::cast_to<Node3D>(p_owner)) {
        if (!p_parent_key) {
            return spatial->to_global(p_point);
        }
        Node3D *parent = spatial->get_parent_node_3d();
        return parent != nullptr ? parent->to_global(p_point) : p_point;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_owner)) {
        const Vector2 local(p_point.x, p_point.y);
        Vector2 world = local;
        Node2D *parent = Object::cast_to<Node2D>(flat->get_parent());
        if (!p_parent_key) {
            world = flat->to_global(local);
        } else if (parent != nullptr) {
            world = parent->to_global(local);
        }
        return Vector3(world.x, world.y, 0.0f);
    }
    return p_point;
}

bool global_point_of(Node *p_node, Vector3 &r_point) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_node)) {
        r_point = spatial->get_global_position();
        return true;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_node)) {
        const Vector2 at = flat->get_global_position();
        r_point = Vector3(at.x, at.y, 0.0f);
        return true;
    }
    return false;
}

uint64_t world_of(Node *p_owner) {
    if (Node3D *spatial = Object::cast_to<Node3D>(p_owner)) {
        const Ref<World3D> world = spatial->get_world_3d();
        return world.is_valid() ? uint64_t(world->get_instance_id()) : 0;
    }
    if (Node2D *flat = Object::cast_to<Node2D>(p_owner)) {
        const Ref<World2D> world = flat->get_world_2d();
        return world.is_valid() ? uint64_t(world->get_instance_id()) : 0;
    }
    return 0;
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
        second = "error " + distance_words(gap_hundredths, flat) + "  "
            + second;
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

void PredictionRings::attach_sketch() {
    if (sketch == nullptr) {
        sketch = std::make_unique<Sketch>();
    }
}

void PredictionRings::poll(NetwMultiplayer &p_session) {
    sample(p_session);
    if (sketch != nullptr) {
        sketch->draw(sampled);
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
            = (made.simulated - camera_point(made.world, owner)).length();
        seen[source.key].generation = generation;
        candidates.push_back(made);
    }
    select();
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
        entry.global_key = String(entry.key).begins_with("global_");
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
        Node *visual = display.is_valid()
                && !display->get_visual_root().is_empty()
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
                row.server = lifted(
                    owner,
                    entry.parent_key,
                    entry.global_key,
                    server
                );
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
                row.server = lifted(
                    owner,
                    entry.parent_key,
                    entry.global_key,
                    server
                );
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

void PredictionRings::select() {
    if (candidates.size() <= ENTITY_CAP) {
        return;
    }
    std::sort(
        candidates.ptr(),
        candidates.ptr() + candidates.size(),
        [](const Candidate &p_left, const Candidate &p_right) {
            if (p_left.world != p_right.world) {
                return p_left.world < p_right.world;
            }
            return p_left.distance < p_right.distance;
        }
    );
    uint32_t kept = 0;
    uint32_t run = 0;
    for (uint32_t at = 0; at < candidates.size(); ++at) {
        if (at == 0 || candidates[at].world != candidates[at - 1].world) {
            run = 0;
        }
        if (run < ENTITY_CAP) {
            candidates[kept++] = candidates[at];
        }
        ++run;
    }
    candidates.resize(kept);
}

Vector3 PredictionRings::camera_point(uint64_t p_world, Node *p_owner) {
    for (const WorldCamera &each : cameras) {
        if (each.world == p_world) {
            return each.point;
        }
    }
    WorldCamera found;
    found.world = p_world;
    Viewport *viewport = p_owner->get_viewport();
    if (viewport != nullptr) {
        if (Object::cast_to<Node3D>(p_owner) != nullptr) {
            Camera3D *camera = viewport->get_camera_3d();
            if (camera != nullptr) {
                found.point = camera->get_global_position();
            }
        } else {
            const Vector2 center = viewport->get_canvas_transform()
                                       .affine_inverse()
                                       .xform(
                                           viewport->get_visible_rect().size
                                           * 0.5f
                                       );
            found.point = Vector3(center.x, center.y, 0.0f);
        }
    }
    cameras.push_back(found);
    return found.point;
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
