## Virtualizes sibling [MultiplayerSynchronizer] configs into a single [SaveContainer].
##
## On [method setup], scans all synchronizers whose [code]root_path[/code] points to the same
## owner and builds a virtual [SceneReplicationConfig] whose properties map to flat names in
## the container. Changes received over the network are written back to the live scene nodes.
class_name SaveSynchronizer
extends MultiplayerSynchronizer

## Emitted when any tracked property changes value (coalesced per-frame by [method save_once]).
signal state_changed

var save_component: SaveComponent

var save_container: SaveContainer:
	get: return save_component.save_container

var scene_owner: Node:
	get: return save_component.owner

var _property_paths: Dictionary[StringName, NodePath] = {}
var _initialized: bool = false
var _state_changed: bool = false

func _init(scomponent: SaveComponent) -> void:
	save_component = scomponent
	
	state_changed.connect(scomponent.on_state_changed)
	set_multiplayer_authority(scomponent.get_multiplayer_authority())
	
	delta_interval = 5.0
	replication_interval = 5.0
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
	public_visibility = false

	name = "SaveSynchronizer"
	unique_name_in_owner = true
	
	scomponent.add_child(self)
	
	owner = scomponent 
	
	root_path = NodePath(".")

func _enter_tree() -> void:
	# Call setup() here, NOT in _ready(), so replication_config is set before
	# C++ MultiplayerSynchronizer processes NOTIFICATION_READY.
	#
	# In Godot 4, Object::notification() calls the C++ _notification() chain BEFORE
	# GDScript _ready() runs. If replication_config is null when NOTIFICATION_READY
	# fires (C++ side), the synchronizer is never registered in the replication cache.
	# That is why the client produces "ID X not found in cache" errors: the client runs
	# _ready() → setup() AFTER C++ has already processed NOTIFICATION_READY with a null
	# config. The server avoids this because save_component.spawn() calls setup() while
	# the player is still off-tree, before NOTIFICATION_READY is ever dispatched.
	#
	# This _enter_tree() approach fixes it on the client: NOTIFICATION_ENTER_TREE fires
	# before NOTIFICATION_READY, so the config is in place when C++ does its registration.
	if not _initialized and save_component.save_container:
		setup()

	# Emit a diagnostic regardless of outcome so the editor panel shows client-side state.
	# SaveSynchronizer can't use _emit_debug_event (no NetComponent ancestor), so we send
	# directly via EngineDebugger.
	if EngineDebugger.is_active():
		var prop_count := replication_config.get_properties().size() if replication_config else 0
		var side := "?"
		if is_inside_tree() and multiplayer:
			var lid := multiplayer.get_unique_id()
			side = "S" if lid == 1 else ("C%d" % lid)
		EngineDebugger.send_message("networked:component_event", [{
			"tree_name": (save_component.get_multiplayer_tree().name
			if save_component and save_component.get_multiplayer_tree() else ""),
			"side": side,
			"player_name": (save_component.owner.name.split("|")[0]
				if save_component and save_component.owner else ""),
			"event_type": "save_sync.enter_tree",
			"data": {
				"initialized": _initialized,
				"has_config": replication_config != null,
				"prop_count": prop_count,
				"in_tree": is_inside_tree(),
				"root_path": str(root_path),
			},
			"correlation_id": "",
			"timestamp_usec": Time.get_ticks_usec(),
			"frame": Engine.get_process_frames(),
		}])

		# Emit crash manifest if setup() still hasn't produced any properties.
		# This catches the case where the client's SaveSynchronizer enters the tree
		# with zero tracked properties — the C++ registration will silently fail.
		if prop_count == 0 and _initialized:
			# push_warning so this appears in the console even when the process
			# is not attached to an editor debugger (e.g. the second OS process
			# in a two-process embedded server+client session).
			push_warning(
				"[CRASH MANIFEST] SaveSynchronizer '%s' on '%s' has 0 properties after setup(). " \
				+ "C++ replication registration will silently fail. " \
				+ "peer_id=%d is_server=%s in_tree=%s root_path=%s" % [
					name,
					(save_component.owner.name if save_component and save_component.owner else "?"),
					(multiplayer.get_unique_id() if multiplayer else 0),
					str(multiplayer.is_server() if multiplayer else false),
					str(is_inside_tree()),
					str(root_path),
				])
			var cid_timeline: Array = []
			# Use a dynamic approach to avoid hard parse error if NetworkedDebugReporter is broken
			var tree_root := get_tree().root if is_inside_tree() else (Engine.get_main_loop() as SceneTree).root
			var reporter = tree_root.get_node_or_null("NetworkedDebugger")
			if reporter:
				cid_timeline = reporter._cid_stack.map(func(s: StringName) -> String: return str(s))

			EngineDebugger.send_message("networked:crash_manifest", [{
				"cid": str(save_component._save_cid) if save_component and not save_component._save_cid.is_empty() else "N/A",
				"cid_timeline": cid_timeline,
				"trigger": "CLIENT_EMPTY_CONFIG_ON_ENTER_TREE",
				"frame": Engine.get_process_frames(),
				"timestamp_usec": Time.get_ticks_usec(),
				"active_scene": "",
				"network_state": {
					"is_server": multiplayer.is_server() if multiplayer else false,
					"peer_id": multiplayer.get_unique_id() if multiplayer else 0,
				},
				"preflight_snapshot": [{
					"name": name,
					"parent": get_parent().name if get_parent() else "?",
					"owner": owner.name if owner else "?",
					"root_path": str(root_path),
					"root_path_resolves": get_node_or_null(root_path) != null,
					"public_visibility": public_visibility,
					"prop_count": 0,
				}],
				"player_name": (save_component.owner.name
					if save_component and save_component.owner else "?"),
				"in_tree": is_inside_tree(),
			}])


func _ready() -> void:
	if not _initialized:
		setup()

	set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)


## Initializes the virtualized replication config based on the owner's attached synchronizers.
func setup() -> void:
	if _initialized:
		push_warning("Initializing once again.")
		return
	_initialized = true

	assert(save_container)
	assert(
		save_container.resource_local_to_scene,
		"`%s` is not local to scene." % save_container,
	)

	_virtualize_replication_configs()
	notify_property_list_changed()


func _virtualize_replication_configs() -> void:
	var new_config := SceneReplicationConfig.new()
	_property_paths.clear()
	
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(scene_owner):
		if sync == self or not sync.replication_config:
			continue 

		var source_config: SceneReplicationConfig = sync.replication_config
		
		for real_path: NodePath in source_config.get_properties():
			var mode := source_config.property_get_replication_mode(real_path)
			var spawn := source_config.property_get_spawn(real_path)
			var sync_flag := source_config.property_get_sync(real_path)
			var watch := source_config.property_get_watch(real_path)
			
			if mode == SceneReplicationConfig.ReplicationMode.REPLICATION_MODE_NEVER:
				continue
			
			var node_res: Array = scene_owner.get_node_and_resource(real_path)
			assert(node_res[0],
				"Trying to synchronize '%s' which is not a valid property path. Check source_config." % real_path)

			var node: Node = node_res[0]
			var prop_path: NodePath = node_res[2]

			var is_root := node == scene_owner
			var node_label := "" if is_root else String(node.name)

			var leaf: String
			if prop_path.get_subname_count() > 0:
				leaf = prop_path.get_subname(prop_path.get_subname_count() - 1)
			else:
				leaf = String(prop_path).trim_prefix(":")

			var virtual_name: String
			if is_root:
				virtual_name = leaf
			else:
				virtual_name = node_label + "/" + leaf

			var vname_sn := StringName(virtual_name)
			
			if _property_paths.has(vname_sn):
				continue

			_property_paths[vname_sn] = real_path
			var virtual_path := NodePath(":" + virtual_name)
			
			new_config.add_property(virtual_path)
			new_config.property_set_replication_mode(virtual_path, mode)
			new_config.property_set_spawn(virtual_path, spawn)
			new_config.property_set_sync(virtual_path, sync_flag)
			new_config.property_set_watch(virtual_path, watch)

			if not save_container.has_value(vname_sn):
				var value: Variant = node.get_indexed(prop_path)
				save_container.set_value(vname_sn, value)

	root_path = NodePath(".")
	replication_config = new_config


func _get_tracked_property_names() -> Array[StringName]:
	return _property_paths.keys()


func _get_property_list() -> Array[Dictionary]:
	var properties: Array[Dictionary] = []
	for property_name: StringName in _get_tracked_property_names():
		var value: Variant = save_container.get_value(property_name, null)
		properties.append({
			"name": property_name,
			"type": typeof(value),
		})
	return properties


## Returns [code]true[/code] if [param property] is tracked by the virtual replication config.
func has_state_property(property: StringName) -> bool:
	return _property_paths.has(property)


func _get_scene_value(property_name: StringName) -> Variant:
	var real_path: NodePath = _property_paths[property_name]
	var node_res := scene_owner.get_node_and_resource(real_path)
	assert(node_res[0], "Invalid real property path for get_scene_value: %s" % String(real_path))
	
	var node: Node = node_res[0]
	var prop_path: NodePath = node_res[2]
	return node.get_indexed(prop_path)


func _set_scene_value(property_name: StringName, value: Variant) -> void:
	var real_path: NodePath = _property_paths[property_name]
	var node_res := scene_owner.get_node_and_resource(real_path)
	assert(node_res[0], "Invalid real property path for set_scene_value: %s" % String(real_path))
	
	var node: Node = node_res[0]
	var prop_path: NodePath = node_res[2]
	node.set_indexed(prop_path, value)


func _get(property: StringName) -> Variant:
	if has_state_property(property):
		return _get_scene_value(property)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if has_state_property(property):
		save_container.set_value(property, value)

		_state_changed = true
		save_once.call_deferred()

		return true

	return false


## Emits [signal state_changed] if any property was mutated this frame, then resets the dirty flag.
##
## Called via [method Object.call_deferred] to coalesce multiple within-frame writes into one signal.
func save_once() -> void:
	if _state_changed:
		state_changed.emit()
		_state_changed = false


## Pulls the latest live values from the scene nodes into the virtualized save container.
func pull_from_scene() -> void:
	assert(_initialized, "Synchronizer not initialized.")
	for property_name: StringName in _get_tracked_property_names():
		var value: Variant = _get_scene_value(property_name)
		save_container.set_value(property_name, value)


## Pushes the loaded virtual container values into the actual live scene nodes.
func push_to_scene() -> Error:
	if not _initialized:
		push_error("SaveSynchronizer: push_to_scene called before setup().")
		return ERR_UNCONFIGURED
	assert(save_container)

	for property_name in save_container:
		var pname := StringName(property_name)
		if not has_state_property(pname):
			push_error("Trying to push a save with property '%s' that is not tracked by the `SaveSynchronizer`." % property_name)
			return Error.ERR_UNCONFIGURED

		var real_path: NodePath = _property_paths[pname]
		var value: Variant = save_container.get_value(pname)
		if value == null:
			push_error("Trying to push but save doesn't have property '%s' that is tracked by the `SaveSynchronizer`." % property_name)
			return Error.ERR_UNCONFIGURED

		_set_scene_value(pname, value)

	return Error.OK


## Packages the current scene state and sends it over the network to the specified peer.
func push_to(peer_id: int) -> void:
	pull_from_scene()
	state_changed.emit()
	request_push.rpc_id(peer_id, save_container.serialize())


## RPC called by a client to push its serialized save state to this peer.
##
## Deserializes [param bytes] into the container and applies the result to the live scene.
@rpc("any_peer", "call_remote", "reliable")
func request_push(bytes: PackedByteArray) -> void:
	save_container.deserialize(bytes)
	push_to_scene()
	state_changed.emit()
