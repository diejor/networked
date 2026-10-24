extends Node

@export var arena_scene: PackedScene

@onready var session: NetwSession = Netw.session(self)


func _init() -> void:
	Netw.configure_lagcomp(self)
	Netw.configure_session(self).app(&"netw-example-rocket-league")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_spawn(make_arena)
	Netw.configure_join(join_match)


func _ready() -> void:
	%InLobby.start_pressed.connect(start_match)


func make_arena() -> Node:
	return arena_scene.instantiate()


func start_match() -> void:
	var level: Node = Netw.spawn(make_arena)
	add_child(level)
	var arena: NetwScene = Netw.scene(level)
	for player: NetwPlayer in session.players:
		add_car(arena, player)


func join_match(player: NetwPlayer) -> void:
	var arena: NetwScene = session.find_scene(&"Arena")
	if arena != null:
		add_car(arena, player)


func add_car(arena: NetwScene, player: NetwPlayer) -> void:
	var players: Node = arena.root.get_node(^"Players")
	if not player.entities.is_empty():
		return
	var grid_slot := players.get_child_count()
	@warning_ignore("integer_division")
	var slot := grid_slot / 2
	players.add_child(
		Netw.spawn_player(
			player,
			arena.root.spawn_car,
			grid_slot % 2,
			slot,
		),
	)
