## Proto input synchronizer for the lag-comp spike (architecture P2).
##
## A [ProxySynchronizer] whose authority is the controlling client, so input
## flows client to server (the authority-derived direction). It replicates one
## atomic [code]__input[/code] dictionary (the A.1 finding: bundle, never split).
## On the server, a received input routes through [member on_input_received]
## into the entity's input timeline instead of the live body. Deleted when
## Phase 0 lands.
@tool
class_name SpikeInputSync
extends ProxySynchronizer

## Peer id of the controlling client. Both peers pin authority here so direction
## follows authority without property arrays.
var controller_id: int = 1

## The input the controlling client authored this tick, surfaced as
## [code]__input[/code].
var authored_input: Dictionary = {}

## Server-side sink invoked per received input with
## [code](tick: int, input: Dictionary)[/code].
var on_input_received: Callable = Callable()


func _ready() -> void:
	if Engine.is_editor_hint():
		return
	set_multiplayer_authority(controller_id)
	if not _properties.has(&"__input"):
		_properties[&"__input"] = NodePath("")
		_prop_options[&"__input"] = {
			"mode": SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
			"spawn": false,
			"watch": false,
		}
	finalize()


func _ordered_virtual_names() -> Array[StringName]:
	var order: Array[StringName] = []
	order.append(&"__input")
	return order


func _read_property(prop: StringName, path: NodePath) -> Variant:
	if prop == &"__input":
		return authored_input
	return super._read_property(prop, path)


func _write_property(prop: StringName, path: NodePath, value: Variant) -> void:
	if prop == &"__input":
		var snap: Dictionary = value
		var tick := int(snap.get(&"tick", -1))
		if tick >= 0 and on_input_received.is_valid():
			on_input_received.call(tick, snap)
		return
	super._write_property(prop, path, value)
