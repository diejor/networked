@tool
## [MultiplayerSynchronizer] that virtualizes arbitrary node properties
## behind a stable name map.
##
## Subclasses call [method register_property] or
## [method register_node_property] to declare virtual names. Then they call
## [method finalize] to commit the config. Godot routes every virtual
## property through this node. The read and write hooks redirect those calls
## to the real locations.
##
## [codeblock]
## # root_path points at the entity root.
## func _ready() -> void:
##     register_property(&"velocity", NodePath("Character:velocity"))
##     register_property(&"health",   NodePath("Character:health"))
##     finalize()
## [/codeblock]
##
## Virtual paths in [member MultiplayerSynchronizer.replication_config] use
## [code]path/to/proxy:vname[/code] relative to [member root_path].
## They survive entity reparents and are visible to
## [method SynchronizersCache.get_synchronizers].
class_name ProxySynchronizer
extends MultiplayerSynchronizer

# Deferred node-property registration. Resolved during finalize().
class _NodePropEntry extends RefCounted:
	var vname: StringName
	var source: Node
	var property: StringName
	var mode: int
	var spawn: bool
	var watch: bool

## Maps virtual property name -> real [NodePath] relative to the entity root.
## Populated by [method register_property] and [method register_node_property].
var _properties: Dictionary[StringName, NodePath] = { }

## Maps virtual property name -> flag dict {"mode", "spawn", "watch"}.
var _prop_options: Dictionary = { }

## Pending [method register_node_property] calls whose real path is resolved at
## [method finalize] (root must be stable first).
var _deferred_node_props: Array = []


## Registers [param virtual_name] as a replicated property backed by
## [param real_path] relative to the entity root.
##
## Must be called before [method finalize]. Idempotent on duplicate
## [param virtual_name].
func register_property(
		virtual_name: StringName,
		real_path: NodePath,
		mode: SceneReplicationConfig.ReplicationMode = \
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	if _properties.has(virtual_name):
		return
	_properties[virtual_name] = real_path
	_prop_options[virtual_name] = {
		"mode": mode,
		"spawn": spawn,
		"watch": _filter_watch(virtual_name, watch),
	}


## Defers registration of [param property] on [param source] as
## [param virtual_name].
##
## The real path ([code]root.get_path_to(source):property[/code]) is resolved
## in [method finalize] once the entity root is stable. Idempotent on
## duplicate [param virtual_name].
func register_node_property(
		virtual_name: StringName,
		source: Node,
		property: StringName,
		mode: SceneReplicationConfig.ReplicationMode = \
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		spawn: bool = false,
		watch: bool = true,
) -> void:
	if _properties.has(virtual_name):
		return
	for e: _NodePropEntry in _deferred_node_props:
		if e.vname == virtual_name:
			return
	var entry := _NodePropEntry.new()
	entry.vname = virtual_name
	entry.source = source
	entry.property = property
	entry.mode = mode
	entry.spawn = spawn
	entry.watch = watch
	_deferred_node_props.append(entry)


## Returns [param property] on [param source] as a [NodePath] relative to
## the entity root (i.e. [member MultiplayerSynchronizer.root_path]).
func path_to_property(source: Node, property: StringName) -> NodePath:
	var root := _resolve_root()
	if not is_instance_valid(root):
		return NodePath("")
	var rel := root.get_path_to(source)
	if rel.is_empty():
		return NodePath("")
	return NodePath("%s:%s" % [rel, property])


## Commits registered properties to
## [member MultiplayerSynchronizer.replication_config].
##
## Must be called once, after all [method register_property] /
## [method register_node_property] calls and once the entity root is in the
## tree so root resolution is stable.
##
## The inspector-set [member replication_config] is imported first so editor
## flags (spawn, replication mode) survive. No-op in the editor.
func finalize() -> void:
	if Engine.is_editor_hint():
		return

	var root := _resolve_root()
	if not root:
		return

	# Import flags from an inspector-configured config before building.
	if replication_config:
		_import_from_config(replication_config, root)

	# Resolve deferred node-property entries now that root is stable.
	for entry: _NodePropEntry in _deferred_node_props:
		if _properties.has(entry.vname):
			continue
		if not is_instance_valid(entry.source):
			continue
		var rel := root.get_path_to(entry.source)
		if rel.is_empty():
			continue
		_properties[entry.vname] = NodePath("%s:%s" % [rel, entry.property])
		_prop_options[entry.vname] = {
			"mode": entry.mode,
			"spawn": entry.spawn,
			"watch": _filter_watch(entry.vname, entry.watch),
		}
	_deferred_node_props.clear()

	# Build the config in insertion order, respecting ordering overrides.
	var config := SceneReplicationConfig.new()
	var ordered_first: Array[StringName] = _ordered_virtual_names()
	for vname: StringName in ordered_first:
		if _properties.has(vname):
			_add_property_to_config(config, vname, root)
	for vname: StringName in _properties:
		if vname not in ordered_first:
			_add_property_to_config(config, vname, root)

	replication_config = config


## Returns all registered virtual property names.
func get_virtual_properties() -> Array[StringName]:
	return _properties.keys()


## Returns the [NodePath] backing [param virtual_name],
## or an empty path if not registered.
func get_real_path(virtual_name: StringName) -> NodePath:
	return _properties.get(virtual_name, NodePath(""))


## Returns [code]true[/code] if [param virtual_name] is a registered property.
func has_virtual_property(virtual_name: StringName) -> bool:
	return _properties.has(virtual_name)


## Hook called by [method register_property] to filter the [param watch] flag.
##
## Override to enforce [code]watch = false[/code] (e.g. for high-latency save
## data) or [code]watch = true[/code] unconditionally.
func _filter_watch(_name: StringName, watch: bool) -> bool:
	return watch


## Override to list virtual names that must appear first in the config.
##
## Used by [TickAwareSynchronizer] to ensure [code]__tick[/code] is always
## the leading property in the replication packet.
func _ordered_virtual_names() -> Array[StringName]:
	return []


## Returns the entity root node: [member root_path] if it resolves,
## otherwise [member Node.owner]. Detached tests fall back to self.
func _resolve_root() -> Node:
	if root_path and has_node(root_path):
		return get_node(root_path)
	if is_instance_valid(owner):
		return owner
	return self


func _add_property_to_config(
		config: SceneReplicationConfig,
		vname: StringName,
		root: Node,
) -> void:
	var vpath := _virtual_path(vname, root)
	if not config.has_property(vpath):
		config.add_property(vpath)
	var opts: Dictionary = _prop_options.get(vname, { })
	config.property_set_replication_mode(
		vpath,
		opts.get("mode", SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE),
	)
	config.property_set_spawn(vpath, opts.get("spawn", false))
	config.property_set_watch(vpath, opts.get("watch", true))


## Returns the virtual [NodePath] for [param vname] under [param root].
##
## Produces [code]path/to/self:vname[/code] relative to [param root], so
## Godot traverses to this proxy, then routes the virtual property through it.
func _virtual_path(vname: StringName, root: Node = null) -> NodePath:
	if not root:
		root = _resolve_root()
	if not is_instance_valid(root):
		return NodePath(":" + vname)
	var rel := root.get_path_to(self)
	return NodePath("%s:%s" % [rel, vname])


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
		result.append({ "name": vname, "type": typeof(v) })
	return result


## Override to redirect reads.
##
## Default resolves [param path] against the entity root
## ([member MultiplayerSynchronizer.root_path]).
func _read_property(_name: StringName, path: NodePath) -> Variant:
	var root := get_node_or_null(root_path)
	return SynchronizersCache.resolve_value(root, path) if root else null


## Override to redirect writes.
##
## Default resolves [param path] against the entity root
## ([member MultiplayerSynchronizer.root_path]).
func _write_property(_name: StringName, path: NodePath, value: Variant) -> void:
	var root := get_node_or_null(root_path)
	if root:
		SynchronizersCache.assign_value(root, path, value)


func _import_from_config(config: SceneReplicationConfig, root: Node) -> void:
	for path: NodePath in config.get_properties():
		if path.is_empty():
			continue
		var subcount := path.get_subname_count()
		if subcount == 0:
			continue

		var vname := StringName(path.get_subname(subcount - 1))

		# Virtual test: the node part of the path resolves to self (from root).
		var node_part := path.get_concatenated_names()
		var is_virtual: bool
		if node_part.is_empty():
			# Old-style ":vname" is always virtual.
			is_virtual = true
		else:
			var target := root.get_node_or_null(NodePath(node_part))
			is_virtual = (target == self)
			if not is_virtual:
				is_virtual = _looks_like_own_virtual_path(path, vname)

		if is_virtual:
			if not _properties.has(vname):
				_properties[vname] = NodePath("")
			_apply_imported_options(config, path, vname)
		else:
			# Non-virtual path: convert to virtual.
			var existing := _find_virtual_name_for_path(path)
			if existing == &"":
				existing = vname if _properties.has(vname) else \
				_generate_virtual_name(path)
			if not _properties.has(existing):
				_properties[existing] = path
			_apply_imported_options(config, path, existing)


func _apply_imported_options(
		config: SceneReplicationConfig,
		path: NodePath,
		vname: StringName,
) -> void:
	_prop_options[vname] = {
		"mode": config.property_get_replication_mode(path),
		"spawn": config.property_get_spawn(path),
		"watch": config.property_get_watch(path),
	}


func _looks_like_own_virtual_path(path: NodePath, vname: StringName) -> bool:
	if not _properties.has(vname):
		return false
	var node_part := path.get_concatenated_names()
	if node_part.is_empty():
		return true
	var names := NodePath(node_part)
	if names.get_name_count() == 0:
		return false
	return names.get_name(names.get_name_count() - 1) == name


func _find_virtual_name_for_path(path: NodePath) -> StringName:
	for vname: StringName in _properties:
		if _properties[vname] == path:
			return vname
	return &""


func _generate_virtual_name(path: NodePath) -> StringName:
	var vname: StringName
	if path.get_subname_count() > 0:
		vname = StringName(path.get_subname(path.get_subname_count() - 1))
	else:
		vname = StringName(
			path.get_concatenated_names().replace("/", "_").replace(".", ""),
		)

	var original := vname
	var counter := 1
	while _properties.has(vname):
		vname = StringName(str(original) + str(counter))
		counter += 1
	return vname
