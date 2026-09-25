extends SceneTree

const ProbeMain := preload("res://probe/probe_main.gd")
const FRAMES := 30

var main: Node
var frame := 0
var last_usec := 0
var longest_frame := 0
var join_started := 0
var warmup := 0


func _initialize() -> void:
	main = ProbeMain.new()
	main.name = &"Main"
	root.add_child(main)


func host() -> void:
	var peer := ENetMultiplayerPeer.new()
	var port := 20000 + randi() % 20000
	var error := peer.create_server(port)
	if error != OK:
		printerr("PROBE create_server failed ", error)
		quit(1)
		return
	join_started = Time.get_ticks_usec()
	main.multiplayer.multiplayer_peer = peer
	Netw.prepare_join(main, &"host")
	last_usec = Time.get_ticks_usec()


func _process(_delta: float) -> bool:
	if join_started == 0:
		warmup += 1
		var wait_ms := int(OS.get_environment("PROBE_WARMUP_MS"))
		if warmup >= 5 and Time.get_ticks_msec() >= wait_ms:
			host()
		return false
	var now := Time.get_ticks_usec()
	longest_frame = maxi(longest_frame, now - last_usec)
	last_usec = now
	frame += 1
	if frame < FRAMES:
		return false
	var linger_usec := int(OS.get_environment("PROBE_LINGER_MS")) * 1000
	if now - join_started < linger_usec:
		return false
	var cubes := 0
	var players := 0
	if is_instance_valid(main.playground):
		cubes = main.playground.get_node(^"Cubes").get_child_count()
		players = main.playground.get_node(^"Players").get_child_count()
	print("PROBE rows_env=", OS.get_environment("PLAYGROUND_ROWS"),
			" cubes=", cubes, " players=", players)
	for stage: String in main.stages:
		print("PROBE %-24s ms=%10.3f" % [stage, main.stages[stage] / 1000.0])
	print("PROBE %-24s ms=%10.3f" % ["longest frame", longest_frame / 1000.0])
	return true
