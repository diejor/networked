## Network radio for [SaveComponent] — replicates the bound entity over the wire.
##
## On [method setup], builds a virtual [SceneReplicationConfig] from
## [member SaveComponent.tracked_properties]: each entity key becomes a virtual
## property whose getter/setter round-trips through the [Entity] and the actual
## scene node at the declared [NodePath].
##
## Unlike the old implicit-scan approach, [SaveSynchronizer] no longer touches
## sibling [MultiplayerSynchronizer] nodes. Tracked properties are declared
## explicitly on [SaveComponent.tracked_properties].
class_name SaveSynchronizer
extends MultiplayerSynchronizer

## Emitted when any tracked property changes value (coalesced per-frame by [method save_once]).
signal state_changed

var save_component: SaveComponent

var bound_entity: Entity:
	get: return save_component.bound_entity

var scene_owner: Node:
	get: return save_component.owner

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
	
	root_path = get_path_to(scomponent.owner)

func _enter_tree() -> void:
	if not _initialized:
		setup()
	
	var prop_count := replication_config.get_properties().size() if replication_config else 0
	if prop_count == 0 and not save_component.tracked_properties.is_empty():
		Netw.dbg.warn("[CLIENT_EMPTY_CONFIG] '%s' on '%s' has 0 properties " \
			+ "after entering tree. C++ replication registration will silently fail. " \
			+ "peer_id=%d is_server=%s root_path=%s" % [
				name,
				save_component.owner.name if save_component and save_component.owner else "?",
				multiplayer.get_unique_id() if multiplayer else 0,
				str(multiplayer.is_server() if multiplayer else false),
				str(root_path),
			], func(m): push_warning(m))


func _ready() -> void:
	if not _initialized:
		setup()
	
	set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)


## Builds the virtual replication config from [member SaveComponent.tracked_properties].
##
## Each entry in [member SaveComponent.tracked_properties] becomes one virtual property
## in the config. The virtual property name equals the entity key; reads and writes are
## intercepted by [method _get] and [method _set] and forwarded to the entity and the
## real scene node at the declared [NodePath].
func setup() -> void:
	if _initialized:
		Netw.dbg.warn("SaveSynchronizer.setup: called more than once.", func(m): push_warning(m))
		return
	_initialized = true
	
	assert(bound_entity)
	assert(
		bound_entity.resource_local_to_scene,
		"`%s` is not local to scene." % bound_entity,
	)

	_build_replication_config()
	notify_property_list_changed()


func _build_replication_config() -> void:
	var new_config := SceneReplicationConfig.new()

	for entity_key: StringName in save_component.tracked_properties:
		var scene_path: NodePath = save_component.tracked_properties[entity_key]
		
		var virtual_path := NodePath(":" + String(entity_key))
		
		new_config.add_property(virtual_path)
		new_config.property_set_replication_mode(
			virtual_path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
		new_config.property_set_spawn(virtual_path, true)
		new_config.property_set_sync(virtual_path, true)
		new_config.property_set_watch(virtual_path, false)

		if scene_owner and not bound_entity.has_value(entity_key):
			var node_res: Array = scene_owner.get_node_and_resource(scene_path)
			if node_res[0]:
				var value: Variant = (node_res[0] as Node).get_indexed(node_res[2])
				bound_entity.set_value(entity_key, value)

	root_path = NodePath(".")
	replication_config = new_config


func _get_tracked_property_names() -> Array[StringName]:
	return save_component.tracked_properties.keys()


func _get_property_list() -> Array[Dictionary]:
	var properties: Array[Dictionary] = []
	for property_name: StringName in _get_tracked_property_names():
		var value: Variant = bound_entity.get_value(property_name, null)
		properties.append({
			"name": property_name,
			"type": typeof(value),
		})
	return properties


## Returns [code]true[/code] if [param property] is declared in [member SaveComponent.tracked_properties].
func has_state_property(property: StringName) -> bool:
	return save_component.tracked_properties.has(property)


func _get_scene_value(property_name: StringName) -> Variant:
	var scene_path: NodePath = save_component.tracked_properties[property_name]
	var node_res := scene_owner.get_node_and_resource(scene_path)
	assert(node_res[0], "Invalid property path for '%s': %s" % [property_name, scene_path])
	return (node_res[0] as Node).get_indexed(node_res[2])


func _set_scene_value(property_name: StringName, value: Variant) -> void:
	var scene_path: NodePath = save_component.tracked_properties[property_name]
	var node_res := scene_owner.get_node_and_resource(scene_path)
	assert(node_res[0], "Invalid property path for '%s': %s" % [property_name, scene_path])
	(node_res[0] as Node).set_indexed(node_res[2], value)


func _get(property: StringName) -> Variant:
	if has_state_property(property):
		return _get_scene_value(property)
	return null


func _set(property: StringName, value: Variant) -> bool:
	if has_state_property(property):
		bound_entity.set_value(property, value)
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


## Reads the current live values from the scene into the entity.
func pull_from_scene() -> void:
	assert(_initialized, "Synchronizer not initialized.")
	for property_name: StringName in save_component.tracked_properties:
		bound_entity.set_value(property_name, _get_scene_value(property_name))


## Writes entity values for all tracked properties into the live scene nodes.
##
## Skips properties that the entity does not yet have a value for (the scene node
## keeps its default). Always returns [constant OK].
func push_to_scene() -> Error:
	if not _initialized:
		Netw.dbg.error("SaveSynchronizer: push_to_scene called before setup().", func(m): push_error(m))
		return ERR_UNCONFIGURED

	assert(bound_entity)

	for entity_key: StringName in save_component.tracked_properties:
		if not bound_entity.has_value(entity_key):
			continue
		_set_scene_value(entity_key, bound_entity.get_value(entity_key))

	return OK


## Packages the current scene state and sends it over the network to [param peer_id].
func push_to(peer_id: int) -> void:
	pull_from_scene()
	state_changed.emit()
	request_push.rpc_id(peer_id, bound_entity.serialize())


## RPC called by a client to push its serialized entity state to this peer.
##
## Deserializes [param bytes] into the entity and applies the result to the live scene.
@rpc("any_peer", "call_remote", "reliable")
func request_push(bytes: PackedByteArray) -> void:
	bound_entity.deserialize(bytes)
	push_to_scene()
	state_changed.emit()
