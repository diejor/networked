#include "netw/diag/sketch.hpp"

#if defined(DEBUG_ENABLED)

#include <cstring>
#include <initializer_list>

#include "godot/label_3d.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/rendering_server.hpp"
#include "godot/theme.hpp"
#include "godot/world.hpp"

using namespace godot;

namespace netw::diag {

namespace {

constexpr float FULL_TURN = 6.28318530717958647692f;
constexpr int ANNULUS_SEGMENTS = 48;
constexpr int DISC_SEGMENTS = 16;
constexpr int SEGMENT_DASHES = 6;
constexpr float ANNULUS_WIDTH = 0.06f;
constexpr float FLAT_ANNULUS_WIDTH = 0.07f;
constexpr uint32_t SPATIAL_FLOATS = 16;
constexpr uint32_t FLAT_FLOATS = 12;
constexpr float REACH = 1.0e5f;
constexpr int CANVAS_Z = 4096;
constexpr float CANVAS_LINE_WIDTH = 2.0f;
constexpr float CANVAS_DASH = 4.0f;
constexpr float CANVAS_DASH_PERIOD = 7.0f;
constexpr int CANVAS_DASHES_PER_SEGMENT = 64;
constexpr int CANVAS_FONT_SIZE = 12;
constexpr float CANVAS_TEXT_CLEARANCE = 1.14f * 32.0f;
constexpr int LABEL_FONT_SIZE = 24;
constexpr float LABEL_PIXEL_SIZE = 0.0012f;
constexpr float LABEL_LIFT = 1.6f;

const char *SKETCH_SHADER = "shader_type spatial;\n"
                            "render_mode unshaded, cull_disabled, "
                            "depth_test_disabled, shadows_disabled;\n"
                            "void fragment() {\n"
                            "    ALBEDO = COLOR.rgb;\n"
                            "    ALPHA = COLOR.a;\n"
                            "}\n";

const char *GHOST_SHADER = "shader_type spatial;\n"
                           "render_mode unshaded, depth_test_disabled, "
                           "depth_draw_never, shadows_disabled;\n"
                           "varying vec4 tint;\n"
                           "void vertex() {\n"
                           "    tint = INSTANCE_CUSTOM;\n"
                           "}\n"
                           "void fragment() {\n"
                           "    ALBEDO = tint.rgb;\n"
                           "    ALPHA = tint.a;\n"
                           "}\n";

constexpr uint32_t BATCH_FLOOR = 16;

int64_t held_rids = 0;

RenderingServer *rs() {
    return RenderingServer::get_singleton();
}

RID kept(const RID &p_rid) {
    if (p_rid.is_valid()) {
        ++held_rids;
    }
    return p_rid;
}

void drop(RID &r_rid) {
    if (r_rid.is_valid()) {
        rs()->free_rid(r_rid);
        --held_rids;
    }
    r_rid = RID();
}

AABB everywhere() {
    const Vector3 reach(REACH, REACH, REACH);
    return AABB(-reach, reach * 2.0f);
}

template <class Points>
RID white_mesh(gd::rse::PrimitiveType p_primitive, const Points &p_points) {
    PackedColorArray colors;
    colors.resize(p_points.size());
    colors.fill(Color(1.0f, 1.0f, 1.0f));
    Array arrays;
    arrays.resize(gd::rse::ARRAY_MAX);
    arrays[gd::rse::ARRAY_VERTEX] = p_points;
    arrays[gd::rse::ARRAY_COLOR] = colors;
    const RID mesh = kept(rs()->mesh_create());
    rs()->mesh_add_surface_from_arrays(mesh, p_primitive, arrays);
    return mesh;
}

template <class Points, class Point>
RID annulus_mesh(bool p_dashed, float p_width, Point (*p_on_circle)(float)) {
    Points points;
    const float inner = 1.0f - p_width;
    for (int at = 0; at < ANNULUS_SEGMENTS; ++at) {
        if (p_dashed && (at / 2) % 2 == 1) {
            continue;
        }
        const Point outer_from
            = p_on_circle(FULL_TURN * float(at) / ANNULUS_SEGMENTS);
        const Point outer_to
            = p_on_circle(FULL_TURN * float(at + 1) / ANNULUS_SEGMENTS);
        points.push_back(outer_from);
        points.push_back(outer_to);
        points.push_back(outer_from * inner);
        points.push_back(outer_from * inner);
        points.push_back(outer_to);
        points.push_back(outer_to * inner);
    }
    return white_mesh(gd::rse::PRIMITIVE_TRIANGLES, points);
}

template <class Points, class Point>
RID disc_mesh(Point (*p_on_circle)(float)) {
    Points points;
    for (int at = 0; at < DISC_SEGMENTS; ++at) {
        points.push_back(Point());
        points.push_back(p_on_circle(FULL_TURN * float(at) / DISC_SEGMENTS));
        points.push_back(
            p_on_circle(FULL_TURN * float(at + 1) / DISC_SEGMENTS)
        );
    }
    return white_mesh(gd::rse::PRIMITIVE_TRIANGLES, points);
}

Vector3 on_ground_circle(float p_turn) {
    return Vector3(Math::cos(p_turn), 0.0f, Math::sin(p_turn));
}

Vector2 on_flat_circle(float p_turn) {
    return Vector2(Math::cos(p_turn), Math::sin(p_turn));
}

RID spatial_annulus(bool p_dashed) {
    return annulus_mesh<PackedVector3Array>(
        p_dashed,
        ANNULUS_WIDTH,
        &on_ground_circle
    );
}

RID flat_annulus_mesh(bool p_dashed) {
    return annulus_mesh<PackedVector2Array>(
        p_dashed,
        FLAT_ANNULUS_WIDTH,
        &on_flat_circle
    );
}

RID segment_mesh(bool p_dashed) {
    PackedVector3Array points;
    if (!p_dashed) {
        points.push_back(Vector3(0.0f, 0.0f, 0.0f));
        points.push_back(Vector3(0.0f, 0.0f, 1.0f));
        return white_mesh(gd::rse::PRIMITIVE_LINES, points);
    }
    const float period = 1.0f / float(SEGMENT_DASHES);
    for (int at = 0; at < SEGMENT_DASHES; ++at) {
        points.push_back(Vector3(0.0f, 0.0f, period * float(at)));
        points.push_back(Vector3(0.0f, 0.0f, period * (float(at) + 0.5f)));
    }
    return white_mesh(gd::rse::PRIMITIVE_LINES, points);
}

void put(
    float *r_at,
    const Basis &p_basis,
    const Vector3 &p_origin,
    const Color &p_color
) {
    const float values[SPATIAL_FLOATS] = {
        p_basis.rows[0].x,
        p_basis.rows[0].y,
        p_basis.rows[0].z,
        p_origin.x,
        p_basis.rows[1].x,
        p_basis.rows[1].y,
        p_basis.rows[1].z,
        p_origin.y,
        p_basis.rows[2].x,
        p_basis.rows[2].y,
        p_basis.rows[2].z,
        p_origin.z,
        p_color.r,
        p_color.g,
        p_color.b,
        p_color.a,
    };
    std::memcpy(r_at, values, sizeof(values));
}

void put_flat(
    float *r_at,
    float p_radius,
    const Vector3 &p_center,
    const Color &p_color
) {
    const float values[FLAT_FLOATS] = {
        p_radius,
        0.0f,
        0.0f,
        p_center.x,
        0.0f,
        p_radius,
        0.0f,
        p_center.y,
        p_color.r,
        p_color.g,
        p_color.b,
        p_color.a,
    };
    std::memcpy(r_at, values, sizeof(values));
}

Basis scaled(float p_radius) {
    return Basis(p_radius, 0, 0, 0, p_radius, 0, 0, 0, p_radius);
}

Basis reaching(const Vector3 &p_span) {
    return Basis(1, 0, p_span.x, 0, 1, p_span.y, 0, 0, p_span.z);
}

Vector2 flat_of(const Vector3 &p_point) {
    return Vector2(p_point.x, p_point.y);
}

void add_dashed_line(
    const RID &p_item,
    const Vector2 &p_from,
    const Vector2 &p_to,
    const Color &p_color,
    float p_width
) {
    const Vector2 span = p_to - p_from;
    const float length = span.length();
    if (length <= 0.0f) {
        return;
    }
    const Vector2 along = span / length;
    for (int at = 0; at < CANVAS_DASHES_PER_SEGMENT; ++at) {
        const float start = CANVAS_DASH_PERIOD * float(at);
        if (start >= length) {
            break;
        }
        const float end = MIN(start + CANVAS_DASH, length);
        rs()->canvas_item_add_line(
            p_item,
            p_from + along * start,
            p_from + along * end,
            p_color,
            CANVAS_LINE_WIDTH * p_width
        );
    }
}

Label3D *made_label(Node *p_parent) {
    Label3D *label = memnew(Label3D);
    label->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);
    label->set_draw_flag(Label3D::FLAG_DISABLE_DEPTH_TEST, true);
    label->set_draw_flag(Label3D::FLAG_FIXED_SIZE, true);
    label->set_font_size(LABEL_FONT_SIZE);
    label->set_outline_size(LABEL_FONT_SIZE / 4);
    label->set_pixel_size(LABEL_PIXEL_SIZE);
    label->set_visible(false);
    p_parent->call_deferred("add_child", label);
    return label;
}

uint32_t fitted(uint32_t p_count, uint32_t p_cap) {
    uint32_t size = BATCH_FLOOR;
    while (size < p_count) {
        size *= 2;
    }
    return MIN(size, p_cap);
}

void allocate(Sketch::Batch &r_batch, uint32_t p_instances) {
    r_batch.allocated = p_instances;
    rs()->multimesh_allocate_data(
        r_batch.multimesh,
        int(p_instances),
        r_batch.flat ? gd::rse::MULTIMESH_TRANSFORM_2D
                     : gd::rse::MULTIMESH_TRANSFORM_3D,
        !r_batch.custom,
        r_batch.custom
    );
    rs()->multimesh_set_custom_aabb(r_batch.multimesh, everywhere());
    r_batch.buffer.resize(p_instances * r_batch.stride);
    r_batch.uploaded.resize(p_instances * r_batch.stride);
    r_batch.writing = r_batch.buffer.ptrw();
    r_batch.uploaded_count = UINT32_MAX;
}

void open_multimesh(
    Sketch::Batch &r_batch,
    const RID &p_mesh,
    uint32_t p_cap,
    bool p_flat
) {
    r_batch.cap = p_cap;
    r_batch.flat = p_flat;
    r_batch.stride = p_flat ? FLAT_FLOATS : SPATIAL_FLOATS;
    r_batch.multimesh = kept(rs()->multimesh_create());
    rs()->multimesh_set_mesh(r_batch.multimesh, p_mesh);
    allocate(r_batch, fitted(0, p_cap));
    rs()->multimesh_set_visible_instances(r_batch.multimesh, 0);
    r_batch.uploaded_count = 0;
}

void place_instance(Sketch::Batch &r_batch, const RID &p_scenario) {
    r_batch.instance
        = kept(rs()->instance_create2(r_batch.multimesh, p_scenario));
    rs()->instance_geometry_set_cast_shadows_setting(
        r_batch.instance,
        gd::rse::SHADOW_CASTING_SETTING_OFF
    );
    rs()->instance_set_custom_aabb(r_batch.instance, everywhere());
}

Sketch::Batch &opened(
    Sketch::Batch &r_batch,
    const RID &p_mesh,
    uint32_t p_cap,
    const RID &p_scenario
) {
    if (r_batch.multimesh.is_valid()) {
        return r_batch;
    }
    open_multimesh(r_batch, p_mesh, p_cap, false);
    place_instance(r_batch, p_scenario);
    return r_batch;
}

Sketch::Batch &opened_flat(
    Sketch::Batch &r_batch,
    const RID &p_mesh,
    uint32_t p_cap
) {
    if (!r_batch.multimesh.is_valid()) {
        open_multimesh(r_batch, p_mesh, p_cap, true);
    }
    return r_batch;
}

void begin(Sketch::Batch &r_batch) {
    r_batch.count = 0;
    r_batch.writing = nullptr;
}

float *slot_in(Sketch::Batch &r_batch) {
    if (r_batch.count == r_batch.allocated) {
        if (r_batch.allocated == r_batch.cap) {
            return nullptr;
        }
        allocate(r_batch, fitted(r_batch.count + 1, r_batch.cap));
    }
    if (r_batch.writing == nullptr) {
        r_batch.writing = r_batch.buffer.ptrw();
    }
    return r_batch.writing + r_batch.stride * r_batch.count++;
}

void flush_batch(Sketch::Batch &r_batch) {
    if (!r_batch.multimesh.is_valid()) {
        return;
    }
    if (r_batch.allocated > BATCH_FLOOR
        && r_batch.count * 4 <= r_batch.allocated) {
        allocate(r_batch, fitted(r_batch.count, r_batch.cap));
    }
    r_batch.writing = nullptr;
    const uint32_t floats = r_batch.count * r_batch.stride;
    const bool same = r_batch.count == r_batch.uploaded_count
        && std::memcmp(
               r_batch.buffer.ptr(),
               r_batch.uploaded.ptr(),
               floats * sizeof(float)
           ) == 0;
    if (same) {
        return;
    }
    if (r_batch.count > 0) {
        rs()->multimesh_set_buffer(r_batch.multimesh, r_batch.buffer);
        std::memcpy(
            r_batch.uploaded.ptr(),
            r_batch.buffer.ptr(),
            floats * sizeof(float)
        );
    }
    if (r_batch.count != r_batch.uploaded_count) {
        rs()->multimesh_set_visible_instances(
            r_batch.multimesh,
            int(r_batch.count)
        );
    }
    r_batch.uploaded_count = r_batch.count;
}

void add_to_canvas(const RID &p_item, const Sketch::Batch &p_batch) {
    if (p_batch.multimesh.is_valid()) {
        rs()->canvas_item_add_multimesh(p_item, p_batch.multimesh);
    }
}

void close_batch(Sketch::Batch &r_batch) {
    drop(r_batch.instance);
    drop(r_batch.multimesh);
}

} // namespace

Sketch::Sketch() = default;

Sketch::~Sketch() {
    for (Space &each : spaces) {
        release(each);
    }
    spaces.clear();
    for (Plane &each : planes) {
        release(each);
    }
    planes.clear();
    drop(annulus);
    drop(dashed_annulus);
    drop(disc);
    drop(segment);
    drop(dashed_segment);
    drop(flat_annulus);
    drop(flat_dashed_annulus);
    drop(flat_disc);
    drop(material);
    drop(shader);
    drop(ghost_material);
    drop(ghost_shader);
}

int64_t Sketch::live_rids() {
    return held_rids;
}

void Sketch::ensure_shared() {
    if (material.is_valid()) {
        return;
    }
    shader = kept(rs()->shader_create());
    rs()->shader_set_code(shader, SKETCH_SHADER);
    material = kept(rs()->material_create());
    rs()->material_set_shader(material, shader);
    annulus = spatial_annulus(false);
    dashed_annulus = spatial_annulus(true);
    disc = disc_mesh<PackedVector3Array>(&on_ground_circle);
    segment = segment_mesh(false);
    dashed_segment = segment_mesh(true);
    for (const RID &mesh :
         {annulus, dashed_annulus, disc, segment, dashed_segment}) {
        rs()->mesh_surface_set_material(mesh, 0, material);
    }
}

void Sketch::ensure_flat_shared() {
    if (flat_annulus.is_valid()) {
        return;
    }
    flat_annulus = flat_annulus_mesh(false);
    flat_dashed_annulus = flat_annulus_mesh(true);
    flat_disc = disc_mesh<PackedVector2Array>(&on_flat_circle);
}

void Sketch::ensure_ghost_shared() {
    if (ghost_material.is_valid()) {
        return;
    }
    ghost_shader = kept(rs()->shader_create());
    rs()->shader_set_code(ghost_shader, GHOST_SHADER);
    ghost_material = kept(rs()->material_create());
    rs()->material_set_shader(ghost_material, ghost_shader);
}

Sketch::Space *Sketch::space_of(uint64_t p_world, uint64_t p_viewport) {
    for (Space &each : spaces) {
        if (each.world == p_world) {
            return &each;
        }
    }
    World3D *world = Object::cast_to<World3D>(gd::object_of(ObjectID(p_world)));
    if (world == nullptr) {
        return nullptr;
    }
    ensure_shared();
    spaces.push_back(Space());
    Space &made = spaces[spaces.size() - 1];
    made.world = p_world;
    made.viewport = p_viewport;
    made.scenario = world->get_scenario();
    return &made;
}

Sketch::Plane *Sketch::plane_of(uint64_t p_world) {
    for (Plane &each : planes) {
        if (each.world == p_world) {
            return &each;
        }
    }
    World2D *world = Object::cast_to<World2D>(gd::object_of(ObjectID(p_world)));
    if (world == nullptr) {
        return nullptr;
    }
    ensure_flat_shared();
    planes.push_back(Plane());
    Plane &made = planes[planes.size() - 1];
    made.world = p_world;
    made.item = kept(rs()->canvas_item_create());
    rs()->canvas_item_set_parent(made.item, world->get_canvas());
    rs()->canvas_item_set_z_index(made.item, CANVAS_Z);
    return &made;
}

Sketch::Batch &Sketch::mark_batch(Space &r_space, MarkShape p_shape) {
    switch (p_shape) {
        case SHAPE_DASHED_RING:
            return opened(
                r_space.dashed_rings,
                dashed_annulus,
                DASHED_RING_CAP,
                r_space.scenario
            );
        case SHAPE_DISC:
            return opened(r_space.discs, disc, DISC_CAP, r_space.scenario);
        default:
            return opened(r_space.rings, annulus, RING_CAP, r_space.scenario);
    }
}

Sketch::Batch &Sketch::mark_batch(Plane &r_plane, MarkShape p_shape) {
    switch (p_shape) {
        case SHAPE_DASHED_RING:
            return opened_flat(
                r_plane.dashed_rings,
                flat_dashed_annulus,
                DASHED_RING_CAP
            );
        case SHAPE_DISC:
            return opened_flat(r_plane.discs, flat_disc, DISC_CAP);
        default:
            return opened_flat(r_plane.rings, flat_annulus, RING_CAP);
    }
}

Sketch::Batch &Sketch::line_batch(Space &r_space, bool p_dashed) {
    if (p_dashed) {
        return opened(
            r_space.dashed_lines,
            dashed_segment,
            DASHED_LINE_CAP,
            r_space.scenario
        );
    }
    return opened(r_space.lines, segment, LINE_CAP, r_space.scenario);
}

void Sketch::draw(const Drawing &p_drawing) {
    sweep();
    for (const Mark &each : p_drawing.marks) {
        if (each.flat) {
            plane_of(each.world);
        } else {
            space_of(each.world, each.viewport);
        }
    }
    for (const Segment &each : p_drawing.segments) {
        if (each.flat) {
            plane_of(each.world);
        } else {
            space_of(each.world, each.viewport);
        }
    }
    for (const Text &each : p_drawing.texts) {
        if (each.flat) {
            plane_of(each.world);
        } else {
            space_of(each.world, each.viewport);
        }
    }
    for (const Ghost &each : p_drawing.ghosts) {
        if (each.flat) {
            plane_of(each.world);
        } else {
            space_of(each.world, each.viewport);
        }
    }
    for (Space &each : spaces) {
        fill(each, p_drawing);
        label(each, p_drawing);
        place_ghosts(each, p_drawing);
    }
    for (Plane &each : planes) {
        paint(each, p_drawing);
    }
}

void Sketch::fill(Space &r_space, const Drawing &p_drawing) {
    for (Batch *each :
         {&r_space.rings,
          &r_space.dashed_rings,
          &r_space.discs,
          &r_space.lines,
          &r_space.dashed_lines}) {
        begin(*each);
    }
    for (const Mark &mark : p_drawing.marks) {
        if (mark.flat || mark.world != r_space.world) {
            continue;
        }
        if (float *at = slot_in(mark_batch(r_space, mark.shape))) {
            put(at, scaled(mark.radius), mark.point, mark.color);
        }
    }
    for (const Segment &line : p_drawing.segments) {
        if (line.flat || line.world != r_space.world) {
            continue;
        }
        if (float *at = slot_in(line_batch(r_space, line.dashed))) {
            put(at, reaching(line.to - line.from), line.from, line.color);
        }
    }
    for (Batch *each :
         {&r_space.rings,
          &r_space.dashed_rings,
          &r_space.discs,
          &r_space.lines,
          &r_space.dashed_lines}) {
        flush_batch(*each);
    }
}

void Sketch::label(Space &r_space, const Drawing &p_drawing) {
    uint32_t used = 0;
    for (const Text &each : p_drawing.texts) {
        if (each.flat || each.world != r_space.world || each.text.is_empty()) {
            continue;
        }
        if (used == TEXT_CAP) {
            break;
        }
        if (used == r_space.labels.size()) {
            Node *parent = Object::cast_to<Node>(
                gd::object_of(ObjectID(r_space.viewport))
            );
            if (parent == nullptr) {
                break;
            }
            r_space.labels.push_back(
                uint64_t(made_label(parent)->get_instance_id())
            );
        }
        Label3D *shown = Object::cast_to<Label3D>(
            gd::object_of(ObjectID(r_space.labels[used]))
        );
        if (shown == nullptr) {
            break;
        }
        if (shown->get_text() != each.text) {
            shown->set_text(each.text);
        }
        shown->set_position(each.anchor + Vector3(0.0f, LABEL_LIFT, 0.0f));
        shown->set_visible(true);
        ++used;
    }
    for (uint32_t at = used; at < r_space.labels.size(); ++at) {
        Label3D *idle = Object::cast_to<Label3D>(
            gd::object_of(ObjectID(r_space.labels[at]))
        );
        if (idle != nullptr && idle->is_visible()) {
            idle->set_visible(false);
        }
    }
}

Sketch::Batch &Sketch::ghost_batch(Space &r_space, const RID &p_base) {
    const uint64_t key = p_base.get_id();
    if (const uint32_t *found = r_space.ghost_of_base.getptr(key)) {
        return r_space.ghosts[*found].batch;
    }
    for (uint32_t at = 0; at < r_space.ghosts.size(); ++at) {
        GhostBatch &idle = r_space.ghosts[at];
        if (idle.batch.count == 0 && idle.batch.uploaded_count == 0) {
            r_space.ghost_of_base.erase(idle.base.get_id());
            r_space.ghost_of_base.insert(key, at);
            idle.base = p_base;
            rs()->multimesh_set_mesh(idle.batch.multimesh, p_base);
            return idle.batch;
        }
    }
    ensure_ghost_shared();
    r_space.ghost_of_base.insert(key, r_space.ghosts.size());
    r_space.ghosts.push_back(GhostBatch());
    GhostBatch &made = r_space.ghosts[r_space.ghosts.size() - 1];
    made.base = p_base;
    made.batch.custom = true;
    open_multimesh(made.batch, p_base, GHOST_CAP, false);
    place_instance(made.batch, r_space.scenario);
    rs()->instance_geometry_set_material_override(
        made.batch.instance,
        ghost_material
    );
    return made.batch;
}

void Sketch::place_ghosts(Space &r_space, const Drawing &p_drawing) {
    for (GhostBatch &each : r_space.ghosts) {
        begin(each.batch);
    }
    uint32_t used = 0;
    for (const Ghost &each : p_drawing.ghosts) {
        if (each.flat || each.world != r_space.world || !each.base.is_valid()) {
            continue;
        }
        if (used == GHOST_CAP) {
            break;
        }
        if (float *at = slot_in(ghost_batch(r_space, each.base))) {
            put(at, each.transform.basis, each.transform.origin, each.color);
            ++used;
        }
    }
    for (GhostBatch &each : r_space.ghosts) {
        flush_batch(each.batch);
    }
}

void Sketch::paint_ghosts(Plane &r_plane, const Drawing &p_drawing) {
    bool moved = false;
    uint32_t used = 0;
    for (const Ghost &each : p_drawing.ghosts) {
        if (!each.flat || each.world != r_plane.world
            || !each.base.is_valid()) {
            continue;
        }
        if (used++ == GHOST_CAP) {
            break;
        }
        rs()->canvas_item_add_set_transform(r_plane.item, each.transform_2d);
        rs()->canvas_item_add_texture_rect_region(
            r_plane.item,
            each.rect,
            each.base,
            each.source,
            each.color
        );
        moved = true;
    }
    if (moved) {
        rs()->canvas_item_add_set_transform(r_plane.item, Transform2D());
    }
}

void Sketch::paint(Plane &r_plane, const Drawing &p_drawing) {
    rs()->canvas_item_clear(r_plane.item);
    paint_ghosts(r_plane, p_drawing);
    for (Batch *each :
         {&r_plane.rings, &r_plane.dashed_rings, &r_plane.discs}) {
        begin(*each);
    }
    for (const Segment &line : p_drawing.segments) {
        if (!line.flat || line.world != r_plane.world) {
            continue;
        }
        if (line.dashed) {
            add_dashed_line(
                r_plane.item,
                flat_of(line.from),
                flat_of(line.to),
                line.color,
                line.width
            );
        } else {
            rs()->canvas_item_add_line(
                r_plane.item,
                flat_of(line.from),
                flat_of(line.to),
                line.color,
                CANVAS_LINE_WIDTH * line.width
            );
        }
    }
    for (const Mark &mark : p_drawing.marks) {
        if (!mark.flat || mark.world != r_plane.world) {
            continue;
        }
        if (float *at = slot_in(mark_batch(r_plane, mark.shape))) {
            put_flat(at, mark.radius, mark.point, mark.color);
        }
    }
    for (Batch *each :
         {&r_plane.rings, &r_plane.dashed_rings, &r_plane.discs}) {
        flush_batch(*each);
        add_to_canvas(r_plane.item, *each);
    }
    const Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
    if (font.is_null()) {
        return;
    }
    uint32_t texts = 0;
    for (const Text &each : p_drawing.texts) {
        if (!each.flat || each.world != r_plane.world || each.text.is_empty()) {
            continue;
        }
        if (texts++ == TEXT_CAP) {
            break;
        }
        font->draw_multiline_string(
            r_plane.item,
            flat_of(each.anchor)
                + Vector2(CANVAS_TEXT_CLEARANCE + 4.0f, -CANVAS_TEXT_CLEARANCE),
            each.text,
            HORIZONTAL_ALIGNMENT_LEFT,
            -1.0f,
            CANVAS_FONT_SIZE
        );
    }
}

void Sketch::release(Space &r_space) {
    for (Batch *each :
         {&r_space.rings,
          &r_space.dashed_rings,
          &r_space.discs,
          &r_space.lines,
          &r_space.dashed_lines}) {
        close_batch(*each);
    }
    for (GhostBatch &each : r_space.ghosts) {
        close_batch(each.batch);
    }
    r_space.ghosts.clear();
    r_space.ghost_of_base.clear();
    for (const uint64_t each : r_space.labels) {
        Node *held = Object::cast_to<Node>(gd::object_of(ObjectID(each)));
        if (held != nullptr) {
            held->queue_free();
        }
    }
    r_space.labels.clear();
}

void Sketch::release(Plane &r_plane) {
    for (Batch *each :
         {&r_plane.rings, &r_plane.dashed_rings, &r_plane.discs}) {
        close_batch(*each);
    }
    drop(r_plane.item);
}

void Sketch::sweep() {
    for (uint32_t at = 0; at < spaces.size();) {
        if (gd::object_of(ObjectID(spaces[at].world)) == nullptr) {
            release(spaces[at]);
            spaces.remove_at_unordered(at);
        } else {
            ++at;
        }
    }
    for (uint32_t at = 0; at < planes.size();) {
        if (gd::object_of(ObjectID(planes[at].world)) == nullptr) {
            release(planes[at]);
            planes.remove_at_unordered(at);
        } else {
            ++at;
        }
    }
}

} // namespace netw::diag

#endif
