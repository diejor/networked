#include "netw/diag/sketch.hpp"

#if defined(DEBUG_ENABLED)

#include <cstring>

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
constexpr float ANNULUS_WIDTH = 0.06f;
constexpr float FLAT_ANNULUS_WIDTH = 0.07f;
constexpr uint32_t SPATIAL_FLOATS = 16;
constexpr uint32_t FLAT_FLOATS = 12;
constexpr float REACH = 1.0e5f;
constexpr int CANVAS_Z = 4096;
constexpr float CANVAS_LINE_WIDTH = 2.0f;
constexpr int CANVAS_FONT_SIZE = 12;
constexpr int LABEL_FONT_SIZE = 24;
constexpr float LABEL_PIXEL_SIZE = 0.0012f;
constexpr float LABEL_LIFT = 1.6f;

const Color SHOWN_COLOR(0.25f, 0.9f, 1.0f);
const Color SIMULATED_COLOR(0.35f, 1.0f, 0.35f);
const Color SERVER_COLOR(1.0f, 0.6f, 0.1f);
const Color OURS_COLOR(1.0f, 0.3f, 0.9f);
const Color ERROR_COLOR(1.0f, 1.0f, 1.0f);

const char *RING_SHADER = "shader_type spatial;\n"
                          "render_mode unshaded, cull_disabled, "
                          "depth_test_disabled, shadows_disabled;\n"
                          "void fragment() {\n"
                          "    ALBEDO = COLOR.rgb;\n"
                          "    ALPHA = COLOR.a;\n"
                          "}\n";

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

RID segment_mesh() {
    PackedVector3Array points;
    points.push_back(Vector3(0.0f, 0.0f, 0.0f));
    points.push_back(Vector3(0.0f, 0.0f, 1.0f));
    return white_mesh(gd::rse::PRIMITIVE_LINES, points);
}

void put(
    float *r_at,
    const Basis &p_basis,
    const Vector3 &p_origin,
    const Color &p_color
) {
    const float values[SPATIAL_FLOATS] = {
        p_basis.rows[0].x, p_basis.rows[0].y, p_basis.rows[0].z, p_origin.x,
        p_basis.rows[1].x, p_basis.rows[1].y, p_basis.rows[1].z, p_origin.y,
        p_basis.rows[2].x, p_basis.rows[2].y, p_basis.rows[2].z, p_origin.z,
        p_color.r,         p_color.g,         p_color.b,         p_color.a,
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
        p_radius,  0.0f,      0.0f,      p_center.x, 0.0f,      p_radius,
        0.0f,      p_center.y, p_color.r, p_color.g,  p_color.b, p_color.a,
    };
    std::memcpy(r_at, values, sizeof(values));
}

Basis scaled(float p_radius) {
    return Basis(p_radius, 0, 0, 0, p_radius, 0, 0, 0, p_radius);
}

Basis reaching(const Vector3 &p_span) {
    return Basis(1, 0, p_span.x, 0, 1, p_span.y, 0, 0, p_span.z);
}

Color faded(const Color &p_color, float p_alpha) {
    return Color(p_color.r, p_color.g, p_color.b, p_color.a * p_alpha);
}

Vector2 flat_of(const Vector3 &p_point) {
    return Vector2(p_point.x, p_point.y);
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

void open_multimesh(
    Sketch::Batch &r_batch,
    const RID &p_mesh,
    uint32_t p_cap,
    bool p_flat
) {
    r_batch.cap = p_cap;
    r_batch.stride = p_flat ? FLAT_FLOATS : SPATIAL_FLOATS;
    r_batch.multimesh = kept(rs()->multimesh_create());
    rs()->multimesh_allocate_data(
        r_batch.multimesh,
        int(p_cap),
        p_flat ? gd::rse::MULTIMESH_TRANSFORM_2D
               : gd::rse::MULTIMESH_TRANSFORM_3D,
        true
    );
    rs()->multimesh_set_mesh(r_batch.multimesh, p_mesh);
    rs()->multimesh_set_custom_aabb(r_batch.multimesh, everywhere());
    rs()->multimesh_set_visible_instances(r_batch.multimesh, 0);
    r_batch.buffer.resize(p_cap * r_batch.stride);
    r_batch.uploaded.resize(p_cap * r_batch.stride);
}

void open_batch(
    Sketch::Batch &r_batch,
    const RID &p_mesh,
    uint32_t p_cap,
    const RID &p_scenario
) {
    open_multimesh(r_batch, p_mesh, p_cap, false);
    r_batch.instance
        = kept(rs()->instance_create2(r_batch.multimesh, p_scenario));
    rs()->instance_geometry_set_cast_shadows_setting(
        r_batch.instance,
        gd::rse::SHADOW_CASTING_SETTING_OFF
    );
    rs()->instance_set_custom_aabb(r_batch.instance, everywhere());
}

float *slot_in(Sketch::Batch &r_batch) {
    if (r_batch.count == r_batch.cap) {
        return nullptr;
    }
    return r_batch.buffer.ptrw() + r_batch.stride * r_batch.count++;
}

void flush_batch(Sketch::Batch &r_batch) {
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
    drop(segment);
    drop(flat_annulus);
    drop(flat_dashed_annulus);
    drop(material);
    drop(shader);
}

int64_t Sketch::live_rids() {
    return held_rids;
}

void Sketch::ensure_shared() {
    if (material.is_valid()) {
        return;
    }
    shader = kept(rs()->shader_create());
    rs()->shader_set_code(shader, RING_SHADER);
    material = kept(rs()->material_create());
    rs()->material_set_shader(material, shader);
    annulus = spatial_annulus(false);
    dashed_annulus = spatial_annulus(true);
    segment = segment_mesh();
    rs()->mesh_surface_set_material(annulus, 0, material);
    rs()->mesh_surface_set_material(dashed_annulus, 0, material);
    rs()->mesh_surface_set_material(segment, 0, material);
}

void Sketch::ensure_flat_shared() {
    if (flat_annulus.is_valid()) {
        return;
    }
    flat_annulus = flat_annulus_mesh(false);
    flat_dashed_annulus = flat_annulus_mesh(true);
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
    const RID scenario = world->get_scenario();
    spaces.push_back(Space());
    Space &made = spaces[spaces.size() - 1];
    made.world = p_world;
    made.viewport = p_viewport;
    open_batch(made.solid, annulus, SPACE_CAP * 3, scenario);
    open_batch(made.dashed, dashed_annulus, SPACE_CAP, scenario);
    open_batch(made.lines, segment, SPACE_CAP, scenario);
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
    open_multimesh(made.solid, flat_annulus, SPACE_CAP * 3, true);
    open_multimesh(made.dashed, flat_dashed_annulus, SPACE_CAP, true);
    return &made;
}

void Sketch::draw(const LocalVector<RingRow> &p_rows) {
    sweep();
    for (const RingRow &row : p_rows) {
        if (row.flat) {
            plane_of(row.world);
        } else {
            space_of(row.world, row.viewport);
        }
    }
    for (Space &each : spaces) {
        fill(each, p_rows);
        flush_batch(each.solid);
        flush_batch(each.dashed);
        flush_batch(each.lines);
        label(each, p_rows);
    }
    for (Plane &each : planes) {
        paint(each, p_rows);
    }
}

void Sketch::fill(Space &r_space, const LocalVector<RingRow> &p_rows) {
    r_space.solid.count = 0;
    r_space.dashed.count = 0;
    r_space.lines.count = 0;
    for (const RingRow &row : p_rows) {
        if (row.flat || row.world != r_space.world) {
            continue;
        }
        if (float *at = slot_in(r_space.solid)) {
            put(at, scaled(RADIUS_SHOWN), row.shown, SHOWN_COLOR);
        }
        if (float *at = slot_in(r_space.solid)) {
            put(at, scaled(RADIUS_SIMULATED), row.simulated, SIMULATED_COLOR);
        }
        if (row.has(MARK_SERVER)) {
            if (float *at = slot_in(r_space.solid)) {
                put(
                    at,
                    scaled(RADIUS_SERVER),
                    row.server,
                    faded(SERVER_COLOR, row.confirmed_alpha)
                );
            }
        }
        if (row.has(MARK_OURS)) {
            if (float *at = slot_in(r_space.dashed)) {
                put(
                    at,
                    scaled(RADIUS_OURS),
                    row.ours,
                    faded(OURS_COLOR, row.confirmed_alpha)
                );
            }
        }
        if (row.erred()) {
            if (float *at = slot_in(r_space.lines)) {
                put(
                    at,
                    reaching(row.ours - row.server),
                    row.server,
                    faded(ERROR_COLOR, row.confirmed_alpha)
                );
            }
        }
    }
}

void Sketch::label(Space &r_space, const LocalVector<RingRow> &p_rows) {
    uint32_t used = 0;
    for (const RingRow &row : p_rows) {
        if (row.flat || row.world != r_space.world || row.text.is_empty()) {
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
        if (shown->get_text() != row.text) {
            shown->set_text(row.text);
        }
        shown->set_position(row.simulated + Vector3(0.0f, LABEL_LIFT, 0.0f));
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

void Sketch::paint(Plane &r_plane, const LocalVector<RingRow> &p_rows) {
    rs()->canvas_item_clear(r_plane.item);
    r_plane.solid.count = 0;
    r_plane.dashed.count = 0;
    for (const RingRow &row : p_rows) {
        if (!row.flat || row.world != r_plane.world) {
            continue;
        }
        if (float *at = slot_in(r_plane.solid)) {
            put_flat(
                at,
                RADIUS_SHOWN * PIXELS_PER_METRE,
                row.shown,
                SHOWN_COLOR
            );
        }
        if (float *at = slot_in(r_plane.solid)) {
            put_flat(
                at,
                RADIUS_SIMULATED * PIXELS_PER_METRE,
                row.simulated,
                SIMULATED_COLOR
            );
        }
        if (row.has(MARK_SERVER)) {
            if (float *at = slot_in(r_plane.solid)) {
                put_flat(
                    at,
                    RADIUS_SERVER * PIXELS_PER_METRE,
                    row.server,
                    faded(SERVER_COLOR, row.confirmed_alpha)
                );
            }
        }
        if (row.has(MARK_OURS)) {
            if (float *at = slot_in(r_plane.dashed)) {
                put_flat(
                    at,
                    RADIUS_OURS * PIXELS_PER_METRE,
                    row.ours,
                    faded(OURS_COLOR, row.confirmed_alpha)
                );
            }
        }
        if (row.erred()) {
            rs()->canvas_item_add_line(
                r_plane.item,
                flat_of(row.server),
                flat_of(row.ours),
                faded(ERROR_COLOR, row.confirmed_alpha),
                CANVAS_LINE_WIDTH
            );
        }
    }
    flush_batch(r_plane.solid);
    flush_batch(r_plane.dashed);
    rs()->canvas_item_add_multimesh(r_plane.item, r_plane.solid.multimesh);
    rs()->canvas_item_add_multimesh(r_plane.item, r_plane.dashed.multimesh);
    const Ref<Font> font = ThemeDB::get_singleton()->get_fallback_font();
    if (font.is_null()) {
        return;
    }
    uint32_t texts = 0;
    const float radius = RADIUS_OURS * PIXELS_PER_METRE;
    for (const RingRow &row : p_rows) {
        if (!row.flat || row.world != r_plane.world || row.text.is_empty()) {
            continue;
        }
        if (texts++ == TEXT_CAP) {
            break;
        }
        font->draw_multiline_string(
            r_plane.item,
            flat_of(row.simulated) + Vector2(radius + 4.0f, -radius),
            row.text,
            HORIZONTAL_ALIGNMENT_LEFT,
            -1.0f,
            CANVAS_FONT_SIZE
        );
    }
}

void Sketch::release(Space &r_space) {
    close_batch(r_space.solid);
    close_batch(r_space.dashed);
    close_batch(r_space.lines);
    for (const uint64_t each : r_space.labels) {
        Node *held = Object::cast_to<Node>(gd::object_of(ObjectID(each)));
        if (held != nullptr) {
            held->queue_free();
        }
    }
    r_space.labels.clear();
}

void Sketch::release(Plane &r_plane) {
    close_batch(r_plane.solid);
    close_batch(r_plane.dashed);
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
