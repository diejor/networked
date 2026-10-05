#pragma once

#if defined(DEBUG_ENABLED)

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::diag {

enum RingMark : uint8_t {
    MARK_SHOWN = 1u << 0,
    MARK_SIMULATED = 1u << 1,
    MARK_SERVER = 1u << 2,
    MARK_OURS = 1u << 3,
};

struct RingRow {
    int64_t key = 0;
    uint64_t world = 0;
    uint64_t viewport = 0;
    bool flat = false;
    int role = -1;
    uint8_t marks = 0;
    godot::Vector3 shown;
    godot::Vector3 simulated;
    godot::Vector3 server;
    godot::Vector3 ours;
    double gap = 0.0;
    double epsilon = 0.0;
    double confirmed_age = 0.0;
    float confirmed_alpha = 1.0f;
    godot::String text;
    double camera_distance = 0.0;

    bool has(RingMark p_mark) const {
        return (marks & p_mark) != 0;
    }
    bool erred() const {
        return has(MARK_SERVER) && has(MARK_OURS) && gap > epsilon;
    }
};

class Sketch {
public:
    static constexpr uint32_t SPACE_CAP = 64;
    static constexpr uint32_t TEXT_CAP = 8;
    static constexpr float RADIUS_SHOWN = 0.72f;
    static constexpr float RADIUS_SIMULATED = 0.86f;
    static constexpr float RADIUS_SERVER = 1.0f;
    static constexpr float RADIUS_OURS = 1.14f;
    static constexpr float PIXELS_PER_METRE = 32.0f;

    Sketch();
    ~Sketch();
    Sketch(const Sketch &) = delete;
    Sketch &operator=(const Sketch &) = delete;

    void draw(const godot::LocalVector<RingRow> &p_rows);
    uint32_t space_count() const {
        return spaces.size() + planes.size();
    }

    static int64_t live_rids();

    struct Batch {
        godot::RID multimesh;
        godot::RID instance;
        uint32_t stride = 0;
        uint32_t cap = 0;
        uint32_t count = 0;
        uint32_t uploaded_count = 0;
        godot::PackedFloat32Array buffer;
        godot::LocalVector<float> uploaded;
    };

private:
    struct Space {
        uint64_t world = 0;
        uint64_t viewport = 0;
        Batch solid;
        Batch dashed;
        Batch lines;
        godot::LocalVector<uint64_t> labels;
    };

    struct Plane {
        uint64_t world = 0;
        godot::RID item;
        Batch solid;
        Batch dashed;
    };

    godot::RID annulus;
    godot::RID dashed_annulus;
    godot::RID segment;
    godot::RID shader;
    godot::RID material;
    godot::RID flat_annulus;
    godot::RID flat_dashed_annulus;
    godot::LocalVector<Space> spaces;
    godot::LocalVector<Plane> planes;

    void ensure_shared();
    void ensure_flat_shared();
    Space *space_of(uint64_t p_world, uint64_t p_viewport);
    Plane *plane_of(uint64_t p_world);
    void fill(Space &r_space, const godot::LocalVector<RingRow> &p_rows);
    void label(Space &r_space, const godot::LocalVector<RingRow> &p_rows);
    void paint(Plane &r_plane, const godot::LocalVector<RingRow> &p_rows);
    void release(Space &r_space);
    void release(Plane &r_plane);
    void sweep();
};

} // namespace netw::diag

#endif
