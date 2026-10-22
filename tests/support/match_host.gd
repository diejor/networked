extends Node

const MATCH := "res://tests/support/match_level.tscn"

var warm: PackedScene
var loads: NetwGroupPromise
var arena: NetwScene
var players: Array[Node] = []


func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_rpc(preload_level).timeout(60.0)
	Netw.configure_spawn(make_player)


func start_match() -> void:
	loads = Netw.request_all(preload_level, MATCH)
	await loads.wait()
	if loads.is_failed or false in loads.results.values():
		return
	Netw.change_scene_to_file(self, MATCH).then(open_match)


func open_match(opened: NetwScene) -> void:
	arena = opened
	for player: NetwPlayer in arena.viewers:
		var made: Node = Netw.spawn_player(player, make_player, player.peer_id)
		arena.root.add_child(made)
		players.append(made)


func make_player(peer_id: int) -> Node:
	var made := Node.new()
	made.name = "Player%d" % peer_id
	return made


@rpc("authority")
func preload_level(path: String) -> bool:
	ResourceLoader.load_threaded_request(path)
	while ResourceLoader.load_threaded_get_status(path) \
			== ResourceLoader.THREAD_LOAD_IN_PROGRESS:
		await get_tree().process_frame
	warm = ResourceLoader.load_threaded_get(path)
	return warm != null
