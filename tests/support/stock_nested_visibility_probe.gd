## Stock-purity probe for nested visibility conformance.
##
## The probe deliberately uses only [MultiplayerSpawner],
## [MultiplayerSynchronizer], and the stock [MultiplayerAPI]. Tests build the
## node shapes from godotengine/godot#68508 around this script, then mount them
## under [MultiplayerTree] without adding Networked nodes to the packed scenes.
class_name StockNestedVisibilityProbe
extends Node2D

## Packed child scene registered by an optional nested spawner in
## [method Node._ready].
static var child_scene_path := ""

## Process-wide lifecycle observations keyed by the peer whose tree owns the
## instance. Tests use these to pin parent-before-child materialization and
## child-before-parent removal.
static var lifecycle_events: Array[Dictionary] = []

## Label written to [member lifecycle_events]. Empty labels are not recorded.
@export var event_kind: StringName = &""

## Per-tick value carried by the synchronizer rooted at this node.
var synced_value := 0


## Clears every recorded lifecycle event.
static func clear_lifecycle_events() -> void:
	lifecycle_events.clear()


## Returns lifecycle kinds for [param peer_id] and [param phase] in observed
## order.
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
	_record_lifecycle(&"enter")


func _ready() -> void:
	var spawner := get_node_or_null("Gun/ChildSpawner") \
			as MultiplayerSpawner
	if spawner and not child_scene_path.is_empty():
		spawner.add_spawnable_scene(child_scene_path)


func _exit_tree() -> void:
	_record_lifecycle(&"exit")


# Records one stock-node lifecycle edge for ordering assertions.
func _record_lifecycle(phase: StringName) -> void:
	if event_kind.is_empty():
		return
	lifecycle_events.append(
		{
			"peer_id": multiplayer.get_unique_id(),
			"phase": phase,
			"kind": event_kind,
		},
	)
