#pragma once

namespace netw_test::gdsrc {

constexpr const char *STOCK_SYNC_PROBE = R"(extends Node2D

var synced_value := 0
var watched_value := 0
var spawn_value := ""
var enter_tree_spawn_value := ""
var mutate_in_ready := false


func _enter_tree() -> void:
	enter_tree_spawn_value = spawn_value


func _ready() -> void:
	if mutate_in_ready:
		spawn_value = "ready-mutated"
)";

constexpr const char *STOCK_AUTHORITY_PROBE = R"(extends Node2D

var synced_value := 0


func _enter_tree() -> void:
	set_multiplayer_authority(str(name).to_int())
)";

constexpr const char *STOCK_NESTED_VISIBILITY_PROBE = R"(extends Node2D

static var child_scene_path := ""
static var lifecycle_events: Array[Dictionary] = []

@export var event_kind: StringName = &""

var synced_value := 0


static func clear_lifecycle_events() -> void:
	lifecycle_events.clear()


static func lifecycle_kinds(
		peer_id: int,
		phase: StringName,
) -> Array[StringName]:
	var kinds: Array[StringName] = []
	for event: Dictionary in lifecycle_events:
		if event.peer_id == peer_id and event.phase == phase:
			kinds.append(event.kind as StringName)
	return kinds


func _enter_tree() -> void:
	record_lifecycle(&"enter")


func _ready() -> void:
	var spawner := get_node_or_null("Gun/ChildSpawner") \
			as MultiplayerSpawner
	if spawner and not child_scene_path.is_empty():
		spawner.add_spawnable_scene(child_scene_path)


func _exit_tree() -> void:
	record_lifecycle(&"exit")


func record_lifecycle(phase: StringName) -> void:
	if event_kind.is_empty():
		return
	lifecycle_events.append(
		{
			"peer_id": multiplayer.get_unique_id(),
			"phase": phase,
			"kind": event_kind,
		},
	)
)";

} // namespace netw_test::gdsrc
