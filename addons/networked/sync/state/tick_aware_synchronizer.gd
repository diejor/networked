## [ProxySynchronizer] that prepends [code]__tick[/code] to replication
## packets.
##
## The synchronizer always has authority [code]1[/code] (server), so the server
## writes [code]__tick[/code]. Clients receive it before the remaining
## properties are applied.
##
## [codeblock]
## func _ready() -> void:
##     register_property(&"position", NodePath(".:position"))
##     finalize_with_tick()
## [/codeblock]
## @experimental
class_name TickAwareSynchronizer
extends ProxySynchronizer

## The tick received from [code]__tick[/code] for the current packet.
var _pending_tick: int = -1


## Call instead of [method ProxySynchronizer.finalize].
##
## Registers [code]__tick[/code] as always replicated, no-spawn, and
## no-watch. It is the leading property in the config.
func finalize_with_tick() -> void:
	if not _properties.has(&"__tick"):
		_properties[&"__tick"] = NodePath("")
		_prop_options[&"__tick"] = {
			"mode": SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
			"spawn": false,
			"watch": false,
		}
	finalize()


func _ordered_virtual_names() -> Array[StringName]:
	return [&"__tick"]


func _read_property(name: StringName, path: NodePath) -> Variant:
	if name == &"__tick":
		var clock := MultiplayerClock.for_node(self)
		assert(
			clock,
			"TickAwareSynchronizer requires a MultiplayerClock in the tree.",
		)
		return clock.tick
	return super._read_property(name, path)


func _write_property(name: StringName, path: NodePath, value: Variant) -> void:
	if name == &"__tick":
		_pending_tick = value
		return
	super._write_property(name, path, value)
