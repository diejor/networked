## Validator that detects [code]simplify_path[/code] races on the server.
##
## A race is a replicated property update that can reach a client before the
## spawn packet for its node, producing "Node not found" errors. The three
## former per-source reporter checks (peer connect, scene spawn, player spawn)
## collapse here: each event kind runs the matching [NetwRaceDetector] scan and,
## on any hit, fails the operation span and emits one [NetwRaceManifest] finding.
class_name SimplifyPathRaceValidator
extends NetwValidator

var _detector := NetwRaceDetector.new()


func interests() -> Array:
	return [
		NetwTreeEvent.Kind.PEER_CONNECTED,
		NetwTreeEvent.Kind.SCENE_SPAWNED,
		NetwTreeEvent.Kind.PLAYER_SPAWNED,
	]


func inspect(event: NetwTreeEvent, report: NetwReport) -> void:
	var mt := event.tree
	if not is_instance_valid(mt) or not mt.is_host:
		return

	var peers: Array = mt.multiplayer_api.get_peers() if mt.multiplayer_api else []
	var races: Array[Dictionary] = []
	var player_name := ""
	var in_tree := false
	var extra_state: Dictionary = { }

	match event.kind:
		NetwTreeEvent.Kind.PEER_CONNECTED:
			races = _detector.find_connect_races(event.peer_id, mt)
			player_name = "peer_%d" % event.peer_id
			in_tree = true
			extra_state = { "new_peer_id": event.peer_id }
		NetwTreeEvent.Kind.SCENE_SPAWNED:
			var scene := event.node as MultiplayerScene
			if not is_instance_valid(scene) or not is_instance_valid(scene.level):
				return
			races = _detector.find_scene_races(scene, mt)
			player_name = scene.level.name
			in_tree = scene.level.is_inside_tree()
			extra_state = { "connected_peers": peers }
		NetwTreeEvent.Kind.PLAYER_SPAWNED:
			var player := event.node
			if not is_instance_valid(player):
				return
			races = _detector.find_player_races(player, mt)
			player_name = player.name
			in_tree = player.is_inside_tree()
			extra_state = { "connected_peers": peers }

	if races.is_empty():
		return

	var m := NetwRaceManifest.new()
	m.trigger = "SERVER_SIMPLIFY_PATH_RACE"
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = player_name
	m.in_tree = in_tree
	for k in extra_state:
		m.network_state[k] = extra_state[k]

	report.fail(m, "simplify_path_race", { "node_count": races.size() })


func _races_to_strings(races: Array[Dictionary]) -> Array[String]:
	var out: Array[String] = []
	for r: Dictionary in races:
		var p := r.get("rel_path", r.get("path", "?"))
		out.append("simplify_path race on %s" % [p])
	return out
