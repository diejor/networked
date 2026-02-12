@tool
class_name StateSynchronizer
extends MultiplayerSynchronizer

@onready var spawn_sync: SpawnSynchronizer:
	get: return $SpawnSynchronizer

func _ready() -> void:
	# Fixes weird behavior where `replication_config` is shared between scene
	# instances
	unique_name_in_owner = true
	if Engine.is_editor_hint():
		replication_config = replication_config.duplicate(true)
		return
	
	assert(root_path == get_path_to(owner))
	add_visibility_filter(scene_visibility_filter)
	spawn_sync.add_visibility_filter(scene_visibility_filter)
	update_visibility()


func only_server() -> void:
	set_visibility_for(0, false)
	spawn_sync.set_visibility_for(0, false)
	set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
	spawn_sync.set_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER, true)
	update_visibility()


func update(peer_id: int = 0) -> void:
	update_visibility(peer_id)
	spawn_sync.update_visibility(peer_id)


func scene_visibility_filter(peer_id: int) -> bool:
	if "Spawner" in owner.name:
		return false
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return true
		
	# Not sure why we need to set to false when `peer_id` equals `0`, my guess is that
	# setting it to true would mean that all peer ids have `true` visibility,
	# therefore, the filter would not be called for specific peer ids.
	if peer_id == 0:
		return false
	
	var world: Node = owner.get_parent().get_parent()
	var scene_sync: SceneSynchronizer = world.get_node("%SceneSynchronizer")
	var res: bool = peer_id in scene_sync.connected_clients
	return res


func _on_teleport() -> void:
	only_server()
