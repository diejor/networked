## [MultiplayerSynchronizer] that virtualizes arbitrary node properties behind a stable name map.
##
## Instead of pointing [member MultiplayerSynchronizer.replication_config] directly at node
## properties, subclasses call [method register_property] to create virtual property names that
## [MultiplayerSynchronizer] replicates, then override [method _read_property] /
## [method _write_property] to redirect reads and writes wherever they need to go.
##
## [b]Usage[/b]
## [codeblock]
## func _ready() -> void:
##     register_property(&"velocity", NodePath("Character:velocity"))
##     register_property(&"health",   NodePath("Character:health"))
##     finalize()
## [/codeblock]
class_name ProxySynchronizer
extends MultiplayerSynchronizer

## Maps virtual property name → real NodePath (relative to root_path node).
var _properties: Dictionary[StringName, NodePath] = {}
var _config: SceneReplicationConfig = SceneReplicationConfig.new()


## Registers [param virtual_name] as a replicated property backed by [param real_path].
##
## Must be called before [method finalize]. [param real_path] is resolved relative to the
## node returned by [member MultiplayerSynchronizer.root_path].
func register_property(
		virtual_name: StringName,
		real_path: NodePath,
		mode: SceneReplicationConfig.ReplicationMode = SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	if _properties.has(virtual_name):
		return
	_properties[virtual_name] = real_path
	var vpath := NodePath(":" + virtual_name)
	_config.add_property(vpath)
	_config.property_set_replication_mode(vpath, mode)
	_config.property_set_spawn(vpath, spawn)
	_config.property_set_watch(vpath, watch)


## Applies the built config to [member MultiplayerSynchronizer.replication_config].
##
## Call once after all [method register_property] calls. For tick-aware subclasses,
## call [method TickAwareSynchronizer.finalize_with_tick] instead.
func finalize() -> void:
	replication_config = _config


func _get(property: StringName) -> Variant:
	if _properties.has(property):
		return _read_property(property, _properties[property])
	return null


func _set(property: StringName, value: Variant) -> bool:
	if _properties.has(property):
		_write_property(property, _properties[property], value)
		return true
	return false


func _get_property_list() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	for vname: StringName in _properties:
		var v: Variant = _read_property(vname, _properties[vname])
		result.append({"name": vname, "type": typeof(v)})
	return result


## Override to redirect reads. Default reads from the node at [member MultiplayerSynchronizer.root_path].
func _read_property(name: StringName, path: NodePath) -> Variant:
	var root := get_node_or_null(root_path)
	if not root:
		return null
	return root.get_indexed(path)


## Override to redirect writes. Default writes to the node at [member MultiplayerSynchronizer.root_path].
func _write_property(_name: StringName, path: NodePath, value: Variant) -> void:
	var root := get_node_or_null(root_path)
	if root:
		root.set_indexed(path, value)
