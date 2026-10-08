#include "netw/diag/interpolation_view.hpp"

#if defined(DEBUG_ENABLED)

#include <cmath>
#include <cstring>

#include "godot/display_server.hpp"
#include "godot/mesh_instance.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "godot/sprite.hpp"
#include "godot/viewport.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/clock_engine.hpp"
#include "netw/diag/hints.hpp"
#include "netw/display/channel.hpp"
#include "netw/display/decl.hpp"
#include "netw/display/history.hpp"
#include "netw/display/playhead.hpp"
#include "netw/display/runtime.hpp"
#include "netw/predict/engine.hpp"
#include "netw/store/table.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace netw::diag {

namespace {

const char *pump_word(int64_t p_pump) {
    switch (p_pump) {
        case display::PUMP_REMOTE:
            return "REMOTE";
        case display::PUMP_CHASE:
            return "CHASE";
        case display::PUMP_BRACKETED:
            return "BRACKETED";
        default:
            return "DISABLED";
    }
}

const char *phase_word(int p_phase) {
    switch (p_phase) {
        case PHASE_REST:
            return "REST";
        case PHASE_BUFFERED:
            return "BUFFERED";
        case PHASE_HOLD:
            return "HOLD";
        case PHASE_STARVING:
            return "STARVING";
        case PHASE_FORECAST:
            return "FORECAST";
        case PHASE_GLIDE:
            return "GLIDE";
        default:
            return "";
    }
}

int64_t tenths(double p_value) {
    return int64_t(std::round(p_value * 10.0));
}

int64_t hundredths(double p_value) {
    return int64_t(std::round(p_value * 100.0));
}

class Words {
public:
    void put(const char *p_text) {
        while (*p_text != '\0' && length < CAPACITY) {
            letters[length++] = *p_text++;
        }
    }

    void whole(int64_t p_value) {
        if (p_value < 0) {
            put("-");
        }
        uint64_t magnitude = p_value < 0 ? uint64_t(0) - uint64_t(p_value)
                                         : uint64_t(p_value);
        char digits[24];
        int count = 0;
        do {
            digits[count++] = char('0' + magnitude % 10);
            magnitude /= 10;
        } while (magnitude > 0);
        while (count > 0 && length < CAPACITY) {
            letters[length++] = digits[--count];
        }
    }

    void fixed(int64_t p_scaled, int p_places) {
        int64_t unit = 1;
        for (int at = 0; at < p_places; ++at) {
            unit *= 10;
        }
        if (p_scaled < 0) {
            put("-");
        }
        const uint64_t magnitude = p_scaled < 0
            ? uint64_t(0) - uint64_t(p_scaled)
            : uint64_t(p_scaled);
        whole(int64_t(magnitude / uint64_t(unit)));
        put(".");
        uint64_t fraction = magnitude % uint64_t(unit);
        for (int64_t place = unit / 10; place > 0; place /= 10) {
            whole(int64_t(fraction / uint64_t(place)));
            fraction %= uint64_t(place);
        }
    }

    void ticks(int64_t p_tenths) {
        fixed(p_tenths, 1);
        put("t");
    }

    void distance(int64_t p_hundredths, bool p_flat) {
        if (p_flat) {
            whole(int64_t(std::round(double(p_hundredths) / 100.0)));
            put(" px");
            return;
        }
        fixed(p_hundredths, 2);
        put(" m");
    }

    String text() const {
        return String::utf8(letters, length);
    }

private:
    static constexpr int CAPACITY = 192;
    char letters[CAPACITY] = {};
    int length = 0;
};

Node *frame_of(display::Channel &p_channel, Node *p_owner) {
    const Variant source = p_channel.get_source_obj();
    Object *object = source;
    Node *node = Object::cast_to<Node>(object);
    return node != nullptr ? node : p_owner;
}

Node *drawn_of(display::Channel &p_channel, Node *p_frame) {
    const Variant target = p_channel.get_target_obj();
    Object *object = target;
    Node *node = Object::cast_to<Node>(object);
    return node != nullptr ? node : p_frame;
}

double glide_of(const display::Channel &p_channel) {
    Vector3 residual;
    return point_of(p_channel.render_offset().residual, residual)
        ? double(residual.length())
        : 0.0;
}

bool heading_of(const Variant &p_value, Quaternion &r_heading) {
    switch (p_value.get_type()) {
        case Variant::QUATERNION:
            r_heading = p_value;
            return true;
        case Variant::BASIS:
            r_heading = Basis(p_value).get_rotation_quaternion();
            return true;
        case Variant::TRANSFORM3D:
            r_heading = Transform3D(p_value).basis.get_rotation_quaternion();
            return true;
        case Variant::TRANSFORM2D:
            r_heading = Quaternion(
                Vector3(0.0f, 0.0f, 1.0f),
                Transform2D(p_value).get_rotation()
            );
            return true;
        default:
            return false;
    }
}

bool turns(int p_type) {
    return p_type == Variant::QUATERNION || p_type == Variant::BASIS
        || p_type == Variant::TRANSFORM3D || p_type == Variant::TRANSFORM2D;
}

bool every_channel_sleeping(const display::Runtime &p_runtime) {
    const LocalVector<display::Channel *> &channels = p_runtime.channels();
    if (channels.is_empty()) {
        return false;
    }
    for (const display::Channel *channel : channels) {
        if (!channel->display_history().is_sleeping()) {
            return false;
        }
    }
    return true;
}

bool snapped_this_frame(const display::Runtime &p_runtime) {
    if (!p_runtime.get_pump_listed()) {
        return false;
    }
    for (const display::Channel *channel : p_runtime.channels()) {
        const display::History &history = channel->display_history();
        if (history.has_snapped() && !history.is_sleeping()) {
            return true;
        }
    }
    return false;
}

bool same_value(const Variant &p_left, const Variant &p_right) {
    const Variant::Type type = p_left.get_type();
    if (type != p_right.get_type()) {
        return p_left == p_right;
    }
    switch (type) {
        case Variant::VECTOR3:
            return Vector3(p_left) == Vector3(p_right);
        case Variant::VECTOR2:
            return Vector2(p_left) == Vector2(p_right);
        case Variant::TRANSFORM3D:
            return Transform3D(p_left) == Transform3D(p_right);
        case Variant::TRANSFORM2D:
            return Transform2D(p_left) == Transform2D(p_right);
        default:
            return p_left == p_right;
    }
}

Variant pose_value(const display::Channel &p_channel) {
    const Variant written = p_channel.get_last_written();
    return written.get_type() != Variant::NIL
        ? written
        : p_channel.display_history().value_of_age(0);
}

} // namespace

bool TimelineLine::operator==(const TimelineLine &p_other) const {
    return pump == p_other.pump && phase == p_other.phase
        && snapped == p_other.snapped && flat == p_other.flat
        && buffer_tenths == p_other.buffer_tenths
        && lag_tenths == p_other.lag_tenths && has_age == p_other.has_age
        && age_tenths == p_other.age_tenths
        && has_received == p_other.has_received
        && received_ahead == p_other.received_ahead
        && forecast_tenths == p_other.forecast_tenths
        && forecast_cap == p_other.forecast_cap
        && glide_hundredths == p_other.glide_hundredths
        && misses == p_other.misses && horizon == p_other.horizon;
}

String TimelineLine::text() const {
    Words out;
    out.put(pump_word(pump));
    out.put("  ");
    out.put(snapped ? "SNAP" : phase_word(phase));
    if (phase == PHASE_GLIDE) {
        out.put("\nglide ");
        out.distance(glide_hundredths, flat);
        return out.text();
    }
    if (pump == display::PUMP_CHASE) {
        out.put("\nhorizon ");
        out.whole(horizon);
        out.put("t");
        return out.text();
    }
    if (phase == PHASE_FORECAST) {
        out.put("\nforecast ");
        out.ticks(forecast_tenths);
        out.put(" of ");
        out.whole(forecast_cap);
        out.put("  lag ");
        out.ticks(lag_tenths);
    } else if (phase != PHASE_REST) {
        out.put("\nbuf ");
        out.ticks(buffer_tenths);
        out.put("  lag ");
        out.ticks(lag_tenths);
        if (has_age) {
            out.put("  age ");
            out.ticks(age_tenths);
        }
        if (has_received) {
            out.put("  ahead ");
            out.whole(received_ahead);
            out.put("t");
        }
    } else if (has_received) {
        out.put("\nahead ");
        out.whole(received_ahead);
        out.put("t");
    }
    if (misses > 0) {
        out.put("\nmiss ");
        out.whole(misses);
    }
    return out.text();
}

Color TimelineStyle::tint(TimelineTint p_tint, float p_alpha) {
    switch (p_tint) {
        case TINT_PAST:
            return Color(0.55f, 0.6f, 0.72f, p_alpha);
        case TINT_BUFFERED:
            return Color(0.2f, 0.78f, 0.88f, p_alpha);
        case TINT_HOLD:
        case TINT_MISS:
            return Color(1.0f, 0.78f, 0.2f, p_alpha);
        case TINT_STARVING:
            return Color(1.0f, 0.25f, 0.25f, p_alpha);
        case TINT_BRACKET:
            return Color(0.95f, 0.95f, 0.95f, p_alpha);
        case TINT_GLIDE:
            return Color(0.62f, 0.5f, 1.0f, p_alpha);
    }
    return Color(1.0f, 1.0f, 1.0f, p_alpha);
}

TimelineTint TimelineStyle::head_tint(int p_phase) {
    switch (p_phase) {
        case PHASE_HOLD:
        case PHASE_FORECAST:
            return TINT_HOLD;
        case PHASE_STARVING:
            return TINT_STARVING;
        default:
            return TINT_BUFFERED;
    }
}

float TimelineStyle::scale_for(double p_size, bool p_flat) {
    const double base = p_flat ? MARK_PIXELS : MARK_METRES;
    return float(
        CLAMP(double(SIZE_RATIO) * p_size / base, 1.0, double(MAX_SCALE))
    );
}

float TimelineStyle::mark_alpha(double p_distance, double p_reach) {
    return float(1.0 - p_distance / (p_reach + 1.0));
}

namespace {

struct Pen {
    const TimelineRow &row;
    Drawing &drawing;

    void disc(const Vector3 &p_point, float p_radius, const Color &p_color) {
        Mark mark;
        mark.world = row.world;
        mark.viewport = row.viewport;
        mark.flat = row.flat;
        mark.shape = SHAPE_DISC;
        mark.point = p_point;
        mark.radius = p_radius;
        mark.color = p_color;
        drawing.marks.push_back(mark);
    }

    void line(
        const Vector3 &p_from,
        const Vector3 &p_to,
        const Color &p_color,
        bool p_dashed
    ) {
        Segment segment;
        segment.world = row.world;
        segment.viewport = row.viewport;
        segment.flat = row.flat;
        segment.dashed = p_dashed;
        segment.width = row.scale;
        segment.from = p_from;
        segment.to = p_to;
        segment.color = p_color;
        drawing.segments.push_back(segment);
    }
};

void draw_remote(Pen &r_pen, const HeldSample *p_newest_first) {
    const TimelineRow &row = r_pen.row;
    const uint32_t count = row.sample_count;
    if (count == 0) {
        return;
    }
    const float mark = (row.flat ? TimelineStyle::MARK_PIXELS
                                 : TimelineStyle::MARK_METRES)
        * row.scale;
    const float heading = (row.flat ? TimelineStyle::HEADING_PIXELS
                                    : TimelineStyle::HEADING_METRES)
        * row.scale;
    const Vector3 forward
        = row.flat ? Vector3(1.0f, 0.0f, 0.0f) : Vector3(0.0f, 0.0f, -1.0f);
    const double reference
        = row.has_head ? row.playhead : double(p_newest_first[0].tick);
    double reach = 0.0;
    for (uint32_t at = 0; at < count; ++at) {
        reach = MAX(
            reach,
            std::abs(double(p_newest_first[at].tick) - reference)
        );
    }
    if (row.has_head) {
        r_pen.disc(
            p_newest_first[0].point,
            mark * TimelineStyle::HEAD_SCALE,
            TimelineStyle::tint(
                TimelineStyle::head_tint(row.phase),
                TimelineStyle::HEAD_ALPHA
            )
        );
    }
    for (uint32_t at = 0; at < count; ++at) {
        const HeldSample &sample = p_newest_first[at];
        const Color color = TimelineStyle::tint(
            sample.buffered ? TINT_BUFFERED : TINT_PAST,
            TimelineStyle::mark_alpha(
                std::abs(double(sample.tick) - reference),
                reach
            )
        );
        r_pen.disc(sample.point, mark, color);
        if (sample.turned) {
            r_pen.line(
                sample.point,
                sample.point + sample.heading.xform(forward) * heading,
                color,
                false
            );
        }
    }
    for (uint32_t at = 0; at + 1 < count; ++at) {
        const HeldSample &newer = p_newest_first[at];
        const HeldSample &older = p_newest_first[at + 1];
        const bool bracket = row.has_head && double(older.tick) <= row.playhead
            && row.playhead < double(newer.tick);
        if (bracket) {
            r_pen.line(
                older.point,
                newer.point,
                TimelineStyle::tint(TINT_BRACKET),
                false
            );
        } else if (newer.follows_miss) {
            r_pen.line(
                older.point,
                newer.point,
                TimelineStyle::tint(TINT_MISS),
                true
            );
        }
    }
    if (row.phase == PHASE_FORECAST) {
        r_pen.line(
            p_newest_first[0].point,
            row.anchor,
            TimelineStyle::tint(TINT_HOLD),
            true
        );
    }
}

} // namespace

void draw_timelines(
    const LocalVector<TimelineRow> &p_rows,
    const LocalVector<HeldSample> &p_marks,
    Drawing &r_drawing
) {
    for (const TimelineRow &row : p_rows) {
        Pen pen{row, r_drawing};
        if (row.pump == display::PUMP_CHASE && row.sample_count == 0) {
            pen.line(
                row.chase_target,
                row.anchor,
                TimelineStyle::tint(TINT_GLIDE),
                false
            );
        } else {
            draw_remote(pen, p_marks.ptr() + row.first_sample);
        }
        if (!row.text.is_empty()) {
            Text text;
            text.world = row.world;
            text.viewport = row.viewport;
            text.flat = row.flat;
            text.anchor = row.anchor;
            text.text = row.text;
            r_drawing.texts.push_back(text);
        }
    }
}

bool InterpolationView::wanted() {
    if ((hints() & HINT_INTERPOLATION) == 0) {
        return false;
    }
    DisplayServer *display = DisplayServer::get_singleton();
    return display != nullptr && display->get_name() != String("headless");
}

InterpolationView::InterpolationView() {
    sources.reserve(ENTITY_CAP);
    candidates.reserve(ENTITY_CAP);
    sampled.reserve(ENTITY_CAP);
    held.reserve(ENTITY_CAP * SAMPLE_CAP);
    held_ticks.resize(SAMPLE_CAP);
    held_values.resize(SAMPLE_CAP);
}

void InterpolationView::sample(NetwMultiplayer &p_session) {
    ++generation;
    sampled.clear();
    held.clear();
    ghosted.clear();
    candidates.clear();
    texted.clear();
    cameras.clear();
    const ClockEngine &clock = p_session.clock_engine();
    if (!clock.get_configured()) {
        forget_departed();
        return;
    }
    Now now;
    now.tick = clock.get_tick();
    now.display_tick = clock.display_tick();
    now.tick_factor = clock.tick_factor();
    now.snap_ticks = MAX(
        int64_t(1),
        int64_t(std::ceil(SNAP_SECONDS * double(clock.get_tickrate())))
    );
    candidates.reserve(sources.size());
    for (uint32_t at = 0; at < sources.size(); ++at) {
        const InterpSource &source = sources[at];
        display::Runtime &runtime = *source.runtime;
        const int64_t pump = runtime.get_pump_mode();
        if (pump != display::PUMP_REMOTE && pump != display::PUMP_CHASE) {
            continue;
        }
        Node *owner = source.owner;
        if (!owner->is_inside_tree()) {
            continue;
        }
        Candidate made;
        made.world = world_of(owner);
        if (made.world == 0) {
            continue;
        }
        Seen &entry = seen[source.route];
        entry.generation = generation;
        if (!resolve_pose(entry, runtime)) {
            continue;
        }
        display::Channel &pose = *runtime.channels()[entry.pose];
        if (pump == display::PUMP_REMOTE) {
            if (snapped_this_frame(runtime)) {
                entry.snap_until = now.tick + now.snap_ticks;
            }
        } else {
            const Ref<NetwEntity> entity = runtime.entity();
            made.forecast
                = entity.is_valid() && !entity->get_is_controlled_locally();
            made.glide = glide_of(pose);
            const double floor = Object::cast_to<Node2D>(owner) != nullptr
                ? GLIDE_FLOOR_PIXELS
                : GLIDE_FLOOR_METRES;
            if (!made.forecast && made.glide <= floor) {
                continue;
            }
        }
        Vector3 shown;
        if (!point_of(pose.get_last_written(), shown)) {
            continue;
        }
        made.frame = frame_of(pose, owner);
        made.anchor
            = lifted(made.frame, entry.parent_key, entry.global_key, shown);
        made.entry = &entry;
        made.source = at;
        made.distance
            = (made.anchor - cameras.point(made.world, owner)).length();
        candidates.push_back(made);
    }
    keep_nearest_per_world(candidates, ENTITY_CAP);
    for (const Candidate &each : candidates) {
        read(p_session, each, now);
    }
    forget_departed();
}

bool InterpolationView::resolve_pose(
    Seen &r_entry,
    display::Runtime &p_runtime
) {
    const LocalVector<display::Channel *> &channels = p_runtime.channels();
    if (r_entry.pose >= 0 && r_entry.runtime == &p_runtime
        && r_entry.channel_count == channels.size()) {
        return true;
    }
    r_entry.runtime = &p_runtime;
    r_entry.channel_count = channels.size();
    r_entry.pose = -1;
    r_entry.heading_from = HEADING_UNRESOLVED;
    r_entry.parts_found = false;
    for (uint32_t at = 0; at < channels.size(); ++at) {
        Vector3 ignored;
        if (point_of(pose_value(*channels[at]), ignored)) {
            r_entry.pose = int(at);
            break;
        }
    }
    if (r_entry.pose < 0) {
        return false;
    }
    const StringName key = channels[r_entry.pose]->get_source_prop();
    r_entry.global_key = global_key(key);
    r_entry.parent_key = parent_relative(key);
    return true;
}

void InterpolationView::resolve_heading(
    Seen &r_entry,
    const display::Channel &p_pose,
    NetwMultiplayer &p_session
) {
    if (r_entry.heading_from != HEADING_UNRESOLVED) {
        return;
    }
    if (turns(pose_value(p_pose).get_type())) {
        r_entry.heading_from = HEADING_POSE;
        return;
    }
    uint64_t owner = 0;
    uint32_t lane = 0;
    uint32_t column = 0;
    if (!p_pose.display_history().follow_address(owner, lane, column)) {
        r_entry.heading_from = HEADING_NONE;
        return;
    }
    const store::RowAddress at = p_session.store().row_of(owner, lane);
    if (!at.valid()) {
        return;
    }
    const store::Table &table = p_session.store().table(at.table);
    if (!table.holds_values(at.row)) {
        return;
    }
    r_entry.heading_from = HEADING_NONE;
    for (uint32_t each = 0; each < table.columns.size(); ++each) {
        if (each != column
            && turns(table.read_value(at.row, each).get_type())) {
            r_entry.heading_from = HEADING_COLUMN;
            r_entry.heading_owner = owner;
            r_entry.heading_lane = lane;
            r_entry.heading_column = each;
            return;
        }
    }
}

void InterpolationView::read(
    NetwMultiplayer &p_session,
    const Candidate &p_candidate,
    const Now &p_now
) {
    const InterpSource &source = sources[p_candidate.source];
    display::Runtime &runtime = *source.runtime;
    Node *owner = source.owner;
    Seen &entry = *p_candidate.entry;
    display::Channel &pose = *runtime.channels()[entry.pose];
    Node *frame = p_candidate.frame;

    TimelineRow row;
    row.route = source.route;
    row.world = p_candidate.world;
    row.viewport = uint64_t(owner->get_viewport()->get_instance_id());
    row.flat = Object::cast_to<Node2D>(owner) != nullptr;
    row.pump = runtime.get_pump_mode();
    row.role = runtime.get_role();
    row.anchor = p_candidate.anchor;
    row.camera_distance = p_candidate.distance;

    if (row.pump == display::PUMP_CHASE && !p_candidate.forecast) {
        row.phase = PHASE_GLIDE;
        row.glide = p_candidate.glide;
        Vector3 target;
        row.chase_target = point_of(pose.current_source_value(), target)
            ? lifted(frame, entry.parent_key, entry.global_key, target)
            : row.anchor;
        if (!rings_drawn && takes_text(row.world)) {
            TimelineLine line;
            line.pump = row.pump;
            line.flat = row.flat;
            line.phase = row.phase;
            line.glide_hundredths = hundredths(row.glide);
            row.text = text_of(entry, line);
        }
        sampled.push_back(row);
        return;
    }
    resolve_heading(entry, pose, p_session);
    if (row.pump == display::PUMP_CHASE) {
        read_forecast(p_session, entry, runtime, frame, row, p_now);
        return;
    }
    read_remote(p_session, entry, runtime, frame, row, p_now);
}

void InterpolationView::read_remote(
    NetwMultiplayer &p_session,
    Seen &r_entry,
    display::Runtime &p_runtime,
    Node *p_frame,
    TimelineRow &r_row,
    const Now &p_now
) {
    const display::Channel &pose = *p_runtime.channels()[r_entry.pose];
    const display::History &history = pose.display_history();
    const uint32_t count = history.newest_held(
        SAMPLE_CAP,
        held_ticks.ptr(),
        held_values.ptr()
    );
    if (count == 0) {
        return;
    }
    r_row.lag = p_runtime.display_playhead().get_display_lag();
    r_row.playhead
        = double(p_now.display_tick) + p_now.tick_factor - r_row.lag;
    const int64_t playhead_tick = int64_t(std::floor(r_row.playhead));
    const int64_t received = history.received_tick();
    const int64_t newest_held = held_ticks[0];
    r_row.has_received = received >= 0;
    r_row.received_ahead = r_row.has_received ? received - newest_held : 0;
    r_row.finished = history.stream_finished(playhead_tick);
    const bool resting = r_row.has_received
        ? r_row.finished && playhead_tick >= newest_held
        : every_channel_sleeping(p_runtime);
    hold_marks(
        p_session,
        r_entry,
        p_frame,
        count,
        resting ? INFINITY : r_row.playhead,
        r_row
    );

    const display::Decl &config = p_runtime.get_config();
    const bool forecast_mode
        = config.timeline_mode == display::TIMELINE_FORECAST;
    if (resting) {
        r_row.phase = PHASE_REST;
    } else if (history.has_projected()) {
        r_row.phase = PHASE_FORECAST;
        r_row.forecast_age = history.get_project_age();
        r_row.forecast_cap = config.max_forecast_ticks;
    } else if (playhead_tick < newest_held) {
        r_row.phase = PHASE_BUFFERED;
    } else if (playhead_tick == newest_held || forecast_mode) {
        r_row.phase = PHASE_HOLD;
    } else {
        r_row.phase = PHASE_STARVING;
    }
    r_row.has_head = !resting;
    if (r_row.has_head) {
        r_row.buffer = double(newest_held) - r_row.playhead;
    }
    r_row.snapped = p_now.tick < r_entry.snap_until;
    const display::Playhead &playhead = p_runtime.display_playhead();
    r_row.has_age = playhead.hears_arrivals();
    r_row.arrival_age = playhead.get_arrival_age();

    TimelineLine line;
    line.pump = r_row.pump;
    line.flat = r_row.flat;
    line.phase = r_row.phase;
    line.snapped = r_row.snapped;
    line.buffer_tenths = tenths(r_row.buffer);
    line.lag_tenths = tenths(r_row.lag);
    line.has_age = r_row.has_age;
    line.age_tenths = tenths(r_row.arrival_age);
    line.has_received = r_row.has_received;
    line.received_ahead = r_row.received_ahead;
    line.forecast_tenths = tenths(r_row.forecast_age);
    line.forecast_cap = r_row.forecast_cap;
    line.misses = r_row.misses;
    if (takes_text(r_row.world)) {
        r_row.text = text_of(r_entry, line);
    }
    Node *drawn = drawn_of(*p_runtime.channels()[r_entry.pose], p_frame);
    find_parts(r_entry, drawn, r_row.flat);
    r_row.scale = TimelineStyle::scale_for(r_entry.part_size, r_row.flat);
    cast_ghost(r_entry, p_frame, drawn, r_row);
    sampled.push_back(r_row);
}

void InterpolationView::read_forecast(
    NetwMultiplayer &p_session,
    Seen &r_entry,
    display::Runtime &p_runtime,
    Node *p_frame,
    TimelineRow &r_row,
    const Now &p_now
) {
    const display::Channel &pose = *p_runtime.channels()[r_entry.pose];
    const uint32_t count = pose.display_history().newest_held(
        SAMPLE_CAP,
        held_ticks.ptr(),
        held_values.ptr()
    );
    if (count == 0) {
        return;
    }
    hold_marks(p_session, r_entry, p_frame, count, INFINITY, r_row);
    NetwPredictionEngine *engine = p_session.get_prediction_engine();
    const int64_t offset = engine != nullptr
        ? engine->joint_world_offset_of(engine->slot_of(p_runtime.entity()))
        : 0;
    r_row.phase = PHASE_FORECAST;
    r_row.horizon = p_now.tick + offset - held_ticks[0];

    TimelineLine line;
    line.pump = r_row.pump;
    line.flat = r_row.flat;
    line.phase = r_row.phase;
    line.horizon = r_row.horizon;
    if (takes_text(r_row.world)) {
        r_row.text = text_of(r_entry, line);
    }
    find_parts(
        r_entry,
        drawn_of(*p_runtime.channels()[r_entry.pose], p_frame),
        r_row.flat
    );
    r_row.scale = TimelineStyle::scale_for(r_entry.part_size, r_row.flat);
    sampled.push_back(r_row);
}

void InterpolationView::hold_marks(
    NetwMultiplayer &p_session,
    const Seen &p_entry,
    Node *p_frame,
    uint32_t p_count,
    double p_buffered_after,
    TimelineRow &r_row
) {
    const store::Table *heading_table = nullptr;
    uint32_t heading_row = 0;
    if (p_entry.heading_from == HEADING_COLUMN) {
        const store::RowAddress at = p_session.store().row_of(
            p_entry.heading_owner,
            p_entry.heading_lane
        );
        if (at.valid()) {
            heading_table = &p_session.store().table(at.table);
            heading_row = at.row;
        }
    }

    r_row.first_sample = held.size();
    const Lift lift = lift_of(p_frame, p_entry.parent_key, p_entry.global_key);
    for (uint32_t age = 0; age < p_count; ++age) {
        HeldSample mark;
        mark.tick = held_ticks[age];
        const Variant &value = held_values[age];
        Vector3 point;
        if (point_of(value, point)) {
            mark.point = lift.apply(point);
        }
        mark.buffered = double(mark.tick) > p_buffered_after;
        if (p_entry.heading_from == HEADING_POSE) {
            mark.turned = heading_of(value, mark.heading);
        } else if (heading_table != nullptr) {
            mark.turned = heading_of(
                heading_table->history_value(
                    heading_row,
                    age,
                    p_entry.heading_column
                ),
                mark.heading
            );
        }
        if (age > 0) {
            HeldSample &above = held[held.size() - 1];
            const int64_t gap = above.tick - mark.tick;
            if (gap > 1 && !same_value(value, held_values[age - 1])) {
                above.follows_miss = true;
                r_row.misses += gap - 1;
            }
        }
        held.push_back(mark);
    }
    r_row.sample_count = p_count;
}

void InterpolationView::find_parts(Seen &r_entry, Node *p_frame, bool p_flat) {
    if (r_entry.parts_found) {
        return;
    }
    r_entry.parts_found = true;
    r_entry.part_count = 0;
#if defined(NETW_TESTS)
    ++part_walks;
#endif
    walk.clear();
    walk.push_back(p_frame);
    uint32_t visited = 0;
    while (!walk.is_empty() && visited < GHOST_WALK_CAP
           && r_entry.part_count < GHOST_PARTS) {
        Node *node = walk[walk.size() - 1];
        walk.resize(walk.size() - 1);
        ++visited;
        const bool part = p_flat
            ? Object::cast_to<Sprite2D>(node) != nullptr
            : Object::cast_to<MeshInstance3D>(node) != nullptr;
        if (part) {
            const uint32_t at = r_entry.part_count++;
            r_entry.parts[at] = uint64_t(node->get_instance_id());
            r_entry.part_nodes[at] = node;
        }
        for (int at = node->get_child_count(false) - 1; at >= 0; --at) {
            walk.push_back(node->get_child(at, false));
        }
    }
    walk.clear();
    r_entry.part_size = 0.0;
    if (r_entry.part_count == 0) {
        return;
    }
    if (p_flat) {
        for (uint32_t at = 0; at < r_entry.part_count; ++at) {
            const Sprite2D *part = Object::cast_to<Sprite2D>(
                gd::object_of(ObjectID(r_entry.parts[at]))
            );
            if (part == nullptr) {
                continue;
            }
            const Vector2 extent = part->get_rect().size
                * part->get_global_transform().get_scale().abs();
            r_entry.part_size
                = MAX(r_entry.part_size, double(MAX(extent.x, extent.y)));
        }
        return;
    }
    for (uint32_t at = 0; at < r_entry.part_count; ++at) {
        const MeshInstance3D *part = Object::cast_to<MeshInstance3D>(
            gd::object_of(ObjectID(r_entry.parts[at]))
        );
        if (part == nullptr || part->get_mesh().is_null()) {
            continue;
        }
        const Vector3 box = part->get_mesh()->get_aabb().size
            * part->get_global_transform().basis.get_scale().abs();
        r_entry.part_size = MAX(
            r_entry.part_size,
            double(MAX(box.x, MAX(box.y, box.z)))
        );
    }
}

void InterpolationView::cast_ghost(
    Seen &r_entry,
    Node *p_frame,
    Node *p_drawn,
    const TimelineRow &p_row
) {
    if (!p_row.has_head || p_row.sample_count == 0) {
        return;
    }
    find_parts(r_entry, p_drawn, p_row.flat);
    if (r_entry.part_count == 0) {
        return;
    }
    const HeldSample &newest = held[p_row.first_sample];
    if (p_row.flat) {
        cast_flat_ghost(r_entry, p_frame, p_row, newest);
    } else {
        cast_spatial_ghost(r_entry, p_frame, p_row, newest);
    }
}

namespace {

constexpr double FULL_TURN = 6.28318530717958647692;

double turn_angle(const Quaternion &p_turn) {
    return 2.0 * std::acos(MIN(1.0, std::abs(double(p_turn.w))));
}

bool sits_on_body(
    const Vector3 &p_offset,
    const TimelineRow &p_row,
    double p_turn
) {
    const float clearance
        = (p_row.flat ? TimelineStyle::MARK_PIXELS : TimelineStyle::MARK_METRES)
        * p_row.scale * TimelineStyle::HEAD_SCALE;
    return p_offset.length() <= clearance
        && p_turn <= TimelineStyle::GHOST_TURN_RADIANS;
}

Color ghost_tint(const TimelineRow &p_row) {
    return TimelineStyle::tint(
        TimelineStyle::head_tint(p_row.phase),
        TimelineStyle::GHOST_ALPHA
    );
}

template <class Part>
Part *live_part(uint64_t p_id, Node *p_node) {
    return gd::object_registered(ObjectID(p_id)) ? static_cast<Part *>(p_node)
                                                 : nullptr;
}

} // namespace

void InterpolationView::cast_spatial_ghost(
    const Seen &r_entry,
    Node *p_frame,
    const TimelineRow &p_row,
    const HeldSample &p_newest
) {
    Node3D *body = Object::cast_to<Node3D>(p_frame);
    if (body == nullptr) {
        return;
    }
    Quaternion turn;
    if (p_newest.turned && (r_entry.parent_key || r_entry.global_key)) {
        Quaternion heading = p_newest.heading;
        Node3D *parent = body->get_parent_node_3d();
        if (!r_entry.global_key && parent != nullptr) {
            heading
                = parent->get_global_transform().basis.get_rotation_quaternion()
                * heading;
        }
        turn = heading
            * body->get_global_transform()
                  .basis.get_rotation_quaternion()
                  .inverse();
    }
    if (sits_on_body(p_newest.point - p_row.anchor, p_row, turn_angle(turn))) {
        return;
    }
    const Transform3D moved = Transform3D(Basis(turn), p_newest.point)
        * Transform3D(Basis(), -p_row.anchor);
    const Color tint = ghost_tint(p_row);
    for (uint32_t at = 0; at < r_entry.part_count; ++at) {
        MeshInstance3D *part = live_part<MeshInstance3D>(
            r_entry.parts[at],
            r_entry.part_nodes[at]
        );
        if (part == nullptr || !part->is_visible_in_tree()) {
            continue;
        }
        const RID base = part->get_base();
        if (!base.is_valid()) {
            continue;
        }
        Ghost ghost;
        ghost.world = p_row.world;
        ghost.viewport = p_row.viewport;
        ghost.base = base;
        ghost.transform = moved * part->get_global_transform();
        ghost.color = tint;
        ghosted.push_back(ghost);
    }
}

void InterpolationView::cast_flat_ghost(
    const Seen &r_entry,
    Node *p_frame,
    const TimelineRow &p_row,
    const HeldSample &p_newest
) {
    Node2D *body = Object::cast_to<Node2D>(p_frame);
    if (body == nullptr) {
        return;
    }
    double turn = 0.0;
    if (p_newest.turned && (r_entry.parent_key || r_entry.global_key)) {
        double heading = p_newest.heading.get_euler().z;
        Node2D *parent = Object::cast_to<Node2D>(body->get_parent());
        if (!r_entry.global_key && parent != nullptr) {
            heading += parent->get_global_rotation();
        }
        turn = std::remainder(
            heading - double(body->get_global_rotation()),
            FULL_TURN
        );
    }
    if (sits_on_body(p_newest.point - p_row.anchor, p_row, std::abs(turn))) {
        return;
    }
    const Vector2 newest(p_newest.point.x, p_newest.point.y);
    const Vector2 anchor(p_row.anchor.x, p_row.anchor.y);
    const Transform2D moved
        = Transform2D(real_t(turn), newest) * Transform2D(0.0f, -anchor);
    const Color tint = ghost_tint(p_row);
    for (uint32_t at = 0; at < r_entry.part_count; ++at) {
        Sprite2D *part
            = live_part<Sprite2D>(r_entry.parts[at], r_entry.part_nodes[at]);
        if (part == nullptr || !part->is_visible_in_tree()) {
            continue;
        }
        const Ref<Texture2D> texture = part->get_texture();
        if (texture.is_null()) {
            continue;
        }
        const Rect2 sheet = part->is_region_enabled()
            ? part->get_region_rect()
            : Rect2(Vector2(), texture->get_size());
        const Vector2 cell = sheet.size
            / Vector2(real_t(part->get_hframes()), real_t(part->get_vframes()));
        const Vector2i frame = part->get_frame_coords();
        Ghost ghost;
        ghost.world = p_row.world;
        ghost.viewport = p_row.viewport;
        ghost.flat = true;
        ghost.base = texture->get_rid();
        ghost.transform_2d = moved * part->get_global_transform();
        ghost.rect = part->get_rect();
        if (part->is_flipped_h()) {
            ghost.rect.size.x = -ghost.rect.size.x;
        }
        if (part->is_flipped_v()) {
            ghost.rect.size.y = -ghost.rect.size.y;
        }
        ghost.source = Rect2(
            sheet.position + cell * Vector2(real_t(frame.x), real_t(frame.y)),
            cell
        );
        ghost.color = tint;
        ghosted.push_back(ghost);
    }
}

bool InterpolationView::takes_text(uint64_t p_world) {
    for (WorldTexts &each : texted) {
        if (each.world == p_world) {
            if (each.count == Sketch::TEXT_CAP) {
                return false;
            }
            ++each.count;
            return true;
        }
    }
    WorldTexts made;
    made.world = p_world;
    made.count = 1;
    texted.push_back(made);
    return true;
}

const String &InterpolationView::text_of(
    Seen &r_entry,
    const TimelineLine &p_line
) {
    if (!(p_line == r_entry.line)) {
        r_entry.line = p_line;
        r_entry.text = p_line.text();
    }
    return r_entry.text;
}

void InterpolationView::forget_departed() {
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
