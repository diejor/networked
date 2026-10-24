extends Node

const MATCH := "res://tests/support/match_level.tscn"

var warm: PackedScene
var loads: NetwGroupPromise
var arena: Node
var players: Array[Node] = []


func _init() -> void:
	Netw.configure_entity(self)
	Netw.configure_rpc(preload_level).timeout(60.0)
	Netw.configure_spawn(make_match)
	Netw.configure_spawn(make_player)


func start_match() -> void:
	loads = Netw.request_all(preload_level, MATCH)
	await loads.wait()
	if loads.is_failed or false in loads.results.values():
		return
	arena = Netw.spawn(make_match)
	add_child(arena)
	for player: NetwPlayer in Netw.session(self).players:
		var made: Node = Netw.spawn_player(player, make_player, player.peer_id)
		arena.add_child(made)
		players.append(made)


func make_match() -> Node:
	return load(MATCH).instantiate()


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
