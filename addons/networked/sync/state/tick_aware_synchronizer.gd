## [ProxySynchronizer] that prepends a [code]__tick[/code] property to
## every replication packet so receivers know which tick the data belongs to.
##
## Call [method finalize_with_tick] instead of
## [method ProxySynchronizer.finalize].
##
## [codeblock]
## func _ready() -> void:
##     register_property(&"position", NodePath(":position"))
##     finalize_with_tick()
## [/codeblock]
## @experimental
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
## Inserts [code]__tick[/code] as the first property so
## [member _pending_tick] is set before any other property arrives.
func finalize_with_tick() -> void:
	# Ensure UI properties are imported first
	if replication_config and replication_config != _config:
		_import_from_config(replication_config)
	
	var tick_path := NodePath(":__tick")

	var ordered := SceneReplicationConfig.new()
	# __tick first - always ALWAYS replicated so the receiver can correlate the packet.
	ordered.add_property(tick_path)
	ordered.property_set_replication_mode(tick_path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	ordered.property_set_spawn(tick_path, false)
	ordered.property_set_watch(tick_path, false)

	# Copy all registered properties after __tick.
	for prop: NodePath in _config.get_properties():
		if prop == tick_path: continue
		ordered.add_property(prop)
		ordered.property_set_replication_mode(prop, _config.property_get_replication_mode(prop))
		ordered.property_set_spawn(prop, _config.property_get_spawn(prop))
		ordered.property_set_watch(prop, _config.property_get_watch(prop))

	_config = ordered
	super.finalize()
