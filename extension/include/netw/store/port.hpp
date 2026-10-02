#pragma once

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/store/column.hpp"
#include "netw/store/resident.hpp"

namespace netw::store {

struct BodyState {
    godot::Object *body = nullptr;
    godot::PhysicsDirectBodyState3D *state_3d = nullptr;
    godot::Node3D *frame_3d = nullptr;
    godot::CanvasItem *frame_2d = nullptr;
    bool solid = false;
    bool root = false;
    bool frozen = false;
    bool sleeping = false;
    bool read = false;
    godot::Transform3D local_3d;
    godot::Vector3 linear_3d;
    godot::Vector3 angular_3d;
    godot::Transform2D local_2d;
    godot::Vector2 linear_2d;
    double angular_2d = 0.0;

    bool rests() const {
        return root && !frozen && sleeping;
    }
};

void body_probe(godot::Object *p_node, BodyState &r_state);

void body_probe(const NodeCell &p_cell, BodyState &r_state);

void body_read(BodyState &r_state);

godot::Variant body_value(
    const BodyState &p_state,
    BodyKey p_key,
    godot::Object *p_node,
    const godot::StringName &p_name
);

bool body_typed(
    const BodyState &p_state,
    BodyKey p_key,
    int p_type,
    uint8_t *r_value
);

godot::Variant object_value(
    godot::Object *p_node,
    const godot::StringName &p_name
);

void resolve_columns(
    godot::Object *p_node,
    const godot::LocalVector<godot::StringName> &p_keys,
    godot::LocalVector<Column> &r_columns
);

constexpr int PUSH_RANKS = 3;

int push_rank(const Column &p_column);

} // namespace netw::store
