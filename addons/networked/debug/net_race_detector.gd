## Static utility for detecting network races in Godot's Multiplayer API.
##
## Specifically identifies "Simplify Path Races" where a property update 
## ([code]simplify_path[/code]) arrives at a client before the corresponding 
## spawn packet, causing "Node not found" errors.
class_name NetRaceDetector
extends RefCounted


## Returns a list of potential races when a lobby/level is spawned.
static func find_lobby_races(lobby: Lobby, mt: MultiplayerTree) -> Array[Dictionary]:
	if not mt.is_server or not mt.multiplayer_api:
		return []
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return []

	var races: Array[Dictionary] = []
	for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
			races.append(_format_race(sync, mt))

	return races


## Returns a list of potential races when a new peer connects.
static func find_connect_races(peer_id: int, mt: MultiplayerTree) -> Array[Dictionary]:
	if not mt or not mt.multiplayer_api:
		return []

	var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
	if not lm:
		return []

	var races: Array[Dictionary] = []
	for lobby_name: StringName in lm.active_lobbies:
		var lobby: Lobby = lm.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
			var sync := child as MultiplayerSynchronizer
			if sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
				var r := _format_race(sync, mt)
				r["lobby"] = str(lobby_name)
				races.append(r)
	return races


## Returns a list of potential races when a player node is added to a lobby.
static func find_player_races(player: Node, mt: MultiplayerTree) -> Array[Dictionary]:
	if not is_instance_valid(player) or not mt.multiplayer_api:
		return []
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return []

	var races: Array[Dictionary] = []
	for child in player.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.is_inside_tree() and sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
			races.append(_format_race(sync, mt))

	return races


static func _format_race(sync: MultiplayerSynchronizer, mt: MultiplayerTree) -> Dictionary:
	return {
		"type": "MultiplayerSynchronizer",
		"path": str(sync.get_path()),
		"rel_path": _get_rel_path(sync, mt),
		"auth": sync.get_multiplayer_authority(),
		"is_auth": sync.is_multiplayer_authority(),
		"public_visibility": true,
	}


static func _has_delta_replication(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	for prop in sync.replication_config.get_properties():
		var mode := sync.replication_config.property_get_replication_mode(prop)
		if mode == SceneReplicationConfig.REPLICATION_MODE_ALWAYS \
				or mode == SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE:
			return true
	return false


static func _get_rel_path(node: Node, mt: MultiplayerTree) -> String:
	if not is_instance_valid(node) or not is_instance_valid(mt):
		return "?"
	var tree_root := mt.get_path()
	var node_path := node.get_path()
	var s_root := str(tree_root)
	var s_node := str(node_path)
	
	if s_node.begins_with(s_root):
		var rel := s_node.trim_prefix(s_root)
		if rel.begins_with("/"):
			rel = rel.substr(1)
		return rel
	return s_node
