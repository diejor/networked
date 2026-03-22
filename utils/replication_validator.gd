@tool
class_name ReplicationValidator
extends RefCounted
## Verifies that all properties with a `replicated` hint string are correctly configured.
## In the editor, it silently auto-configures missing properties.
## In the game, it strictly asserts to prevent silent multiplayer failures.

static func verify_and_configure(component: Node) -> void:
	if not component.owner:
		return

	if Engine.is_editor_hint():
		_auto_configure_replication.call_deferred(component)
	else:
		_assert_replicated_properties(component)


## Analyzes the component and returns configuration warnings for the Godot editor.
## Only warns if the foundational MultiplayerSynchronizer node is completely missing.
static func get_configuration_warnings(component: Node) -> PackedStringArray:
	var warnings := PackedStringArray()

	if not component.owner:
		return warnings

	var synchronizers := SynchronizersCache.get_client_synchronizers(component.owner)
	if synchronizers.is_empty():
		warnings.append("Requires at least one MultiplayerSynchronizer in the scene with root_path pointing to the owner.")

	return warnings


static func _assert_replicated_properties(component: Node) -> void:
	var synchronizers := SynchronizersCache.get_client_synchronizers(component.owner)
	var current_path: NodePath = component.owner.get_path_to(component)

	for property in _get_replicated_properties(component):
		var prop_path := NodePath(String(current_path) + ":" + property.name)
		var is_valid := _is_property_valid(prop_path, property.hint_string, synchronizers)

		assert(is_valid, "`%s` property `%s` lacks proper replication config in MultiplayerSynchronizer." % [component.name, prop_path])


## Evaluates a single property path against all cached synchronizers to see if its replication mode matches its hint.
static func _is_property_valid(prop_path: NodePath, hint: String, synchronizers: Array[MultiplayerSynchronizer]) -> bool:
	for sync in synchronizers:
		var config := sync.replication_config
		if config and config.has_property(prop_path):
			var mode := config.property_get_replication_mode(prop_path)

			if (hint == "replicated:always" and mode == SceneReplicationConfig.REPLICATION_MODE_ALWAYS) or \
			(hint == "replicated:on_change" and mode == SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE) or \
			(hint == "replicated:never" and mode == SceneReplicationConfig.REPLICATION_MODE_NEVER) or \
			(hint == "replicated" and mode != SceneReplicationConfig.REPLICATION_MODE_NEVER):
				return true
	return false


static func _auto_configure_replication(component: Node) -> void:
	var synchronizers := SynchronizersCache.get_client_synchronizers(component.owner)
	if synchronizers.is_empty():
		return

	var target_sync := synchronizers[0]
	if not target_sync.replication_config:
		target_sync.replication_config = SceneReplicationConfig.new()

	var current_path: NodePath = component.owner.get_path_to(component)
	var replicated_properties := _get_replicated_properties(component)

	for replicated in replicated_properties:
		_configure_property(replicated, current_path, synchronizers, target_sync)


static func _get_replicated_properties(component: Node) -> Array[Dictionary]:
	var properties: Array[Dictionary] = []
	properties.assign(
		component.get_property_list().filter(
	func(property: Dictionary) -> bool:
		return property.hint_string.begins_with("replicated")
		),
	)
	return properties


static func _configure_property(property_data: Dictionary, base_path: NodePath, synchronizers: Array[MultiplayerSynchronizer], fallback_sync: MultiplayerSynchronizer) -> void:
	var prop_path := NodePath(String(base_path) + ":" + property_data.name)
	var hint: String = property_data.hint_string
	var active_config: SceneReplicationConfig = null

	for sync in synchronizers:
		if sync.replication_config and sync.replication_config.has_property(prop_path):
			active_config = sync.replication_config
			break

	if not active_config:
		active_config = fallback_sync.replication_config
		active_config.add_property(prop_path)
		active_config.property_set_spawn(prop_path, true)

	_apply_replication_mode(active_config, prop_path, hint)


static func _apply_replication_mode(config: SceneReplicationConfig, path: NodePath, hint: String) -> void:
	if hint == "replicated:always":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
	elif hint == "replicated:on_change":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE)
	elif hint == "replicated:never":
		config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_NEVER)
	elif hint == "replicated":
		if config.property_get_replication_mode(path) == SceneReplicationConfig.REPLICATION_MODE_NEVER:
			config.property_set_replication_mode(path, SceneReplicationConfig.REPLICATION_MODE_ALWAYS)
