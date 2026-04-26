class_name TickSimulationEnvironment
extends RefCounted

var server_node: Node
var client_node: Node
var interpolator: TickInterpolator

func set_server_property(path: StringName, value: Variant) -> void:
	server_node.set_indexed(NodePath(path), value)

func get_client_property(path: StringName) -> Variant:
	return client_node.get_indexed(NodePath(path))

## Inspects HistoryBuffer — use to assert recorded network values.
func get_buffer_at(prop: StringName, tick: int) -> Variant:
	var buf := interpolator.get_buffer(prop)
	return buf.get_at(tick) if buf else null

func get_buffer_newest_tick(prop: StringName) -> int:
	var buf := interpolator.get_buffer(prop)
	return buf.newest_tick() if buf else -1

func get_display_lag() -> float:    return interpolator.display_lag
func get_starvation_ticks() -> int: return interpolator.starvation_ticks
