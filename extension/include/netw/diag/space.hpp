#pragma once

#if defined(DEBUG_ENABLED)

#include <algorithm>
#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/variant.hpp"

namespace netw::diag {

bool point_of(const godot::Variant &p_value, godot::Vector3 &r_point);
bool parent_relative(const godot::StringName &p_key);
bool global_key(const godot::StringName &p_key);

struct Lift {
    enum Kind : uint8_t {
        KIND_NONE,
        KIND_SPATIAL,
        KIND_FLAT,
    };

    Kind kind = KIND_NONE;
    godot::Transform3D spatial;
    godot::Transform2D flat;

    godot::Vector3 apply(const godot::Vector3 &p_point) const;
};

Lift lift_of(godot::Node *p_frame, bool p_parent_key, bool p_global_key);
godot::Vector3 lifted(
    godot::Node *p_frame,
    bool p_parent_key,
    bool p_global_key,
    const godot::Vector3 &p_point
);
bool global_point_of(godot::Node *p_node, godot::Vector3 &r_point);
uint64_t world_of(godot::Node *p_owner);

class CameraBook {
public:
    void clear() {
        cameras.clear();
    }
    godot::Vector3 point(uint64_t p_world, godot::Node *p_owner);

private:
    struct WorldCamera {
        uint64_t world = 0;
        godot::Vector3 point;
    };

    godot::LocalVector<WorldCamera> cameras;
};

template <typename Candidate>
void keep_nearest_per_world(
    godot::LocalVector<Candidate> &r_candidates,
    uint32_t p_cap
) {
    if (r_candidates.size() <= p_cap) {
        return;
    }
    std::sort(
        r_candidates.ptr(),
        r_candidates.ptr() + r_candidates.size(),
        [](const Candidate &p_left, const Candidate &p_right) {
            if (p_left.world != p_right.world) {
                return p_left.world < p_right.world;
            }
            return p_left.distance < p_right.distance;
        }
    );
    uint32_t kept = 0;
    uint32_t run = 0;
    for (uint32_t at = 0; at < r_candidates.size(); ++at) {
        if (at == 0 || r_candidates[at].world != r_candidates[at - 1].world) {
            run = 0;
        }
        if (run < p_cap) {
            r_candidates[kept++] = r_candidates[at];
        }
        ++run;
    }
    r_candidates.resize(kept);
}

} // namespace netw::diag

#endif
