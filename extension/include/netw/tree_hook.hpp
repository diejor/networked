#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/string_name.hpp"

namespace netw::tree {

enum Mark : uint16_t {
    MARK_ENTITY = 1,
    MARK_COMPONENT = 2,
    MARK_LINT = 4,
    MARK_DERIVED_ENTER = 8,
    MARK_DERIVED_EXIT = 16,
    MARK_COMPONENT_SEEN = 32,
    MARK_LINT_SEEN = 64,
    MARK_BOOKED = 128,
    MARK_HEARD = 256,
};

constexpr uint16_t MARKS_ONCE = MARK_COMPONENT | MARK_LINT;
constexpr uint16_t MARKS_ENTERING
    = MARK_ENTITY | MARKS_ONCE | MARK_DERIVED_ENTER;

void mark(godot::Node *p_node, uint16_t p_marks);
void mark_id(uint64_t p_node, uint16_t p_marks);
void unmark(uint64_t p_node, uint16_t p_marks);
bool marked(godot::Node *p_node, uint16_t p_marks);
void watch(godot::Node *p_node, uint64_t p_entity);
void unwatch(godot::Node *p_node);

godot::Object *entity_object_at(godot::Node *p_node);
bool is_entity_root(godot::Node *p_node);

void declarations_entered(godot::Node *p_node, uint16_t p_marks);
void declarations_exiting(godot::Node *p_node);

void despawning(godot::Object *p_wrapper, const godot::StringName &p_reason);

#if defined(NETW_TESTS)
uint64_t watched_by_under_test(godot::Node *p_node);
#endif

} // namespace netw::tree
