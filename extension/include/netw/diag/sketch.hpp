#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>

#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::diag {

enum MarkShape : uint8_t {
    SHAPE_RING,
    SHAPE_DASHED_RING,
    SHAPE_DISC,
};

struct Mark {
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    MarkShape shape = SHAPE_RING;
    godot::Vector3 point;
    float radius = 0.0f;
    godot::Color color;
};

struct Segment {
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    bool dashed = false;
    float width = 1.0f;
    godot::Vector3 from;
    godot::Vector3 to;
    godot::Color color;
};

struct Text {
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    godot::Vector3 anchor;
    godot::String text;
};

struct Ghost {
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    godot::RID base;
    godot::Transform3D transform;
    godot::Transform2D transform_2d;
    godot::Rect2 rect;
    godot::Rect2 source;
    godot::Color color;
};

struct Drawing {
    godot::LocalVector<Mark> marks;
    godot::LocalVector<Segment> segments;
    godot::LocalVector<Text> texts;
    godot::LocalVector<Ghost> ghosts;

    void clear() {
        marks.clear();
        segments.clear();
        texts.clear();
        ghosts.clear();
    }
};

class Sketch {
public:
    static constexpr uint32_t ENTITY_CAP = 64;
    static constexpr uint32_t SAMPLE_CAP = 16;
    static constexpr uint32_t GHOST_PARTS = 16;
    static constexpr uint32_t GHOST_CAP = ENTITY_CAP * GHOST_PARTS;
    static constexpr uint32_t RING_CAP = ENTITY_CAP * 3;
    static constexpr uint32_t DASHED_RING_CAP = ENTITY_CAP;
    static constexpr uint32_t DISC_CAP = ENTITY_CAP * (SAMPLE_CAP + 1);
    static constexpr uint32_t LINE_CAP = ENTITY_CAP * (SAMPLE_CAP + 3);
    static constexpr uint32_t DASHED_LINE_CAP = ENTITY_CAP * SAMPLE_CAP;
    static constexpr uint32_t TEXT_CAP = 8;

    Sketch();
    ~Sketch();
    Sketch(const Sketch &) = delete;
    Sketch &operator=(const Sketch &) = delete;

    void draw(const Drawing &p_drawing);
    uint32_t space_count() const {
        return spaces.size() + planes.size();
    }

    static int64_t live_rids();

    struct Batch {
        godot::RID multimesh;
        godot::RID instance;
        bool flat = false;
        bool custom = false;
        uint32_t stride = 0;
        uint32_t cap = 0;
        uint32_t allocated = 0;
        uint32_t count = 0;
        uint32_t uploaded_count = 0;
        float *writing = nullptr;
        godot::PackedFloat32Array buffer;
        godot::LocalVector<float> uploaded;
    };

private:
    struct GhostBatch {
        godot::RID base;
        Batch batch;
    };

    struct Space {
        uint64_t world = 0;
        uint64_t viewport = 0;
        godot::RID scenario;
        Batch rings;
        Batch dashed_rings;
        Batch discs;
        Batch lines;
        Batch dashed_lines;
        godot::LocalVector<GhostBatch> ghosts;
        godot::HashMap<uint64_t, uint32_t> ghost_of_base;
        godot::LocalVector<uint64_t> labels;
    };

    struct Plane {
        uint64_t world = 0;
        godot::RID item;
        Batch rings;
        Batch dashed_rings;
        Batch discs;
    };

    godot::RID annulus;
    godot::RID dashed_annulus;
    godot::RID disc;
    godot::RID segment;
    godot::RID dashed_segment;
    godot::RID shader;
    godot::RID material;
    godot::RID flat_annulus;
    godot::RID flat_dashed_annulus;
    godot::RID flat_disc;
    godot::RID ghost_shader;
    godot::RID ghost_material;
    godot::LocalVector<Space> spaces;
    godot::LocalVector<Plane> planes;

    void ensure_shared();
    void ensure_flat_shared();
    void ensure_ghost_shared();
    Space *space_of(uint64_t p_world, uint64_t p_viewport);
    Plane *plane_of(uint64_t p_world);
    Batch &mark_batch(Space &r_space, MarkShape p_shape);
    Batch &mark_batch(Plane &r_plane, MarkShape p_shape);
    Batch &line_batch(Space &r_space, bool p_dashed);
    Batch &ghost_batch(Space &r_space, const godot::RID &p_base);
    void fill(Space &r_space, const Drawing &p_drawing);
    void label(Space &r_space, const Drawing &p_drawing);
    void place_ghosts(Space &r_space, const Drawing &p_drawing);
    void paint(Plane &r_plane, const Drawing &p_drawing);
    void paint_ghosts(Plane &r_plane, const Drawing &p_drawing);
    void release(Space &r_space);
    void release(Plane &r_plane);
    void sweep();
};

} // namespace netw::diag

#endif
