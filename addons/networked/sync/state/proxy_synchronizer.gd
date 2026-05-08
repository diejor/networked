@tool
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

## Maps virtual property name -> real NodePath (relative to original root).
var _properties: Dictionary[StringName, NodePath] = {}
var _config: SceneReplicationConfig = SceneReplicationConfig.new()

## The original root_path used for resolving real property paths.
## Set during [method finalize].
var _target_root: NodePath = NodePath(".")


## Registers [param virtual_name] as a replicated property backed by [param real_path].
##
## [br][br]
## [b]Timing and Resolution:[/b]
## [br]- Must be called before [method finalize].
## [br]- [param real_path] is resolved relative to the node returned by
## [member MultiplayerSynchronizer.root_path] at the time [method finalize] runs.
## [br]- At runtime, [method finalize] pivots [member root_path] to [code]"."[/code]
## to enable interception, but [param real_path] continues to resolve against the
## original target node.
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
	_config.property_set_watch(vpath, _filter_watch(virtual_name, watch))


## Alias for [method register_property].
func track(
		virtual_name: StringName,
		real_path: NodePath,
		mode: SceneReplicationConfig.ReplicationMode = SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	register_property(virtual_name, real_path, mode, spawn, watch)


## Applies the built config to [member MultiplayerSynchronizer.replication_config].
##
## [b]Capture and Pivot:[/b]
## [br]- If [member replication_config] contains standard property paths (set via
## the Inspector), they are automatically converted to virtual names (using the
## property's leaf name) and registered.
## [br]- Stores the current [member root_path] as the "Target Root".
## [br]- At runtime, pivots [member root_path] to [code]"."[/code] (self) so Godot
## routes replication traffic through this node's [method _get] and [method _set].
##
## [br][br][b]Note:[/b] For tick-aware subclasses, call
## [method TickAwareSynchronizer.finalize_with_tick] instead.
func finalize() -> void:
	if Engine.is_editor_hint():
		# In editor, just track what the root WOULD be for internal lookups.
		# We don't pivot or swap the config so the Replication UI remains editable.
		_target_root = root_path
		return

	if replication_config and replication_config != _config:
		_import_from_config(replication_config)
	
	if root_path != NodePath("."):
		_target_root = root_path
		root_path = NodePath(".")
	
	replication_config = _config


## Returns a list of all registered virtual property names.
##
## [b]Output Format:[/b]
## [br]- Returns an [code]Array[StringName][/code] (e.g., [code][&"position",
## &"health"][/code]).
## [br]- These names correspond to the virtual keys used in network packets
## and [method _read_property]/[method _write_property] calls.
func get_virtual_properties() -> Array[StringName]:
	return _properties.keys()


## Returns the [NodePath] associated with the given [param virtual_name].
##
## [b]Output Format:[/b]
## [br]- Returns a [NodePath] relative to the original "Target Root" node.
## [br]- Returns an empty path if the property is not registered.
func get_real_path(virtual_name: StringName) -> NodePath:
	return _properties.get(virtual_name, NodePath(""))


## Returns [code]true[/code] if [param virtual_name] is a registered property.
func has_virtual_property(virtual_name: StringName) -> bool:
	return _properties.has(virtual_name)


## Internal hook used by [method register_property] to filter the [param watch] flag.
##
## Subclasses can override this to enforce [code]watch = false[/code] (e.g. for
## high-latency save data) or [code]watch = true[/code] regardless of the input.
func _filter_watch(_name: StringName, watch: bool) -> bool:
	return watch


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
##
## [param path] is resolved via [method Node.get_node_and_resource] so it handles both
## plain property paths ([code]:velocity[/code]) and node-traversal paths
## ([code]Body:velocity[/code], [code].:position[/code]).
func _read_property(_name: StringName, path: NodePath) -> Variant:
	var root := get_node_or_null(_target_root)
	return SynchronizersCache.resolve_value(root, path) if root else null


## Override to redirect writes. Default writes to the node at [member MultiplayerSynchronizer.root_path].
##
## [param path] is resolved via [method Node.get_node_and_resource] so it handles both
## plain property paths ([code]:velocity[/code]) and node-traversal paths
## ([code]Body:velocity[/code], [code].:position[/code]).
func _write_property(_name: StringName, path: NodePath, value: Variant) -> void:
	var root := get_node_or_null(_target_root)
	if root:
		SynchronizersCache.assign_value(root, path, value)


func _import_from_config(config: SceneReplicationConfig) -> void:
	for path in config.get_properties():
		if path.is_empty():
			continue

		var is_virtual := path.get_subname_count() > 0 and path.get_subname(0).begins_with(":")
		if is_virtual:
			var vname := StringName(path.get_subname(0).substr(1))
			if not _properties.has(vname):
				# Pure virtual property picked in UI.
				_properties[vname] = NodePath("")

			if not _config.has_property(path):
				_config.add_property(path)
			_config.property_set_replication_mode(path, config.property_get_replication_mode(path))
			_config.property_set_spawn(path, config.property_get_spawn(path))
			_config.property_set_watch(path, config.property_get_watch(path))
			continue

		# Non-virtual: convert to virtual
		var vname := _find_virtual_name_for_path(path)
		if vname == &"":
			vname = _generate_virtual_name(path)

		register_property(
			vname, path,
			config.property_get_replication_mode(path),
			config.property_get_spawn(path),
			config.property_get_watch(path)
		)


func _find_virtual_name_for_path(path: NodePath) -> StringName:
	for vname in _properties:
		if _properties[vname] == path:
			return vname
	return &""


func _generate_virtual_name(path: NodePath) -> StringName:
	var vname := &""
	if path.get_subname_count() > 0:
		vname = StringName(path.get_subname(path.get_subname_count() - 1))
	else:
		vname = StringName(path.get_concatenated_names().replace("/", "_").replace(".", ""))

	var original_vname := vname
	var counter := 1
	while _properties.has(vname):
		vname = StringName(str(original_vname) + str(counter))
		counter += 1
	return vname
