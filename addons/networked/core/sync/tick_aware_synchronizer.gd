## [ProxySynchronizer] that prepends a [code]__tick[/code] property to every replication packet.
##
## The sender writes the current [NetworkClock] tick as the first property in the replication
## config. Because Godot packs and unpacks properties in config-declaration order, the receiver's
## [method _set] sees [code]__tick[/code] first, caches it in [member _pending_tick], and then
## every subsequent [method _set] call within the same deserialization pass can read
## [member _pending_tick] to know which tick the data belongs to.
##
## Subclasses ([code]RollbackSynchronizer[/code], [code]NetworkInputSynchronizer[/code]) use
## [member _pending_tick] to write received values into a [HistoryBuffer] at the correct tick.
##
## [b]Setup[/b]: call [method finalize_with_tick] instead of [method ProxySynchronizer.finalize].
class_name TickAwareSynchronizer
extends ProxySynchronizer

## The tick received from [code]__tick[/code]; valid during [method _write_property] calls.
var _pending_tick: int = -1


func _get(property: StringName) -> Variant:
	if property == &"__tick":
		var clock := NetworkClock.for_node(self)
		assert(clock, "TickAwareSynchronizer requires a NetworkClock in the tree.")
		return clock.tick
	return super._get(property)


func _set(property: StringName, value: Variant) -> bool:
	if property == &"__tick":
		_pending_tick = value
		return true
	return super._set(property, value)


## Call instead of [method ProxySynchronizer.finalize].
##
## Inserts [code]__tick[/code] as the FIRST property in the replication config so that
## [member _pending_tick] is populated before any other [method _write_property] call.
func finalize_with_tick() -> void:
	var tick_path := NodePath(":__tick")

	var ordered := SceneReplicationConfig.new()
	# __tick first — always ALWAYS replicated so the receiver can correlate the packet.
	ordered.add_property(tick_path)
	ordered.property_set_replication_mode(tick_path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	ordered.property_set_spawn(tick_path, false)
	ordered.property_set_watch(tick_path, false)

	# Copy all registered properties after __tick.
	for prop: NodePath in _config.get_properties():
		ordered.add_property(prop)
		ordered.property_set_replication_mode(prop, _config.property_get_replication_mode(prop))
		ordered.property_set_spawn(prop, _config.property_get_spawn(prop))
		ordered.property_set_watch(prop, _config.property_get_watch(prop))

	_config = ordered
	replication_config = _config
