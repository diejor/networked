extends Node

const ARENA := "res://examples/rocket_league/scenes/arena.tscn"

@onready var session: NetwSession = Netw.session(self)


func _init() -> void:
	Netw.configure_lagcomp(self)
	Netw.configure_session(self).app(&"netw-example-rocket-league")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_join(join_match)


func _ready() -> void:
	%InLobby.start_pressed.connect(start_match)


func start_match() -> void:
	Netw.change_scene_to_file(self, ARENA).then(open_match)


func open_match(arena: NetwScene) -> void:
	for player: NetwPlayer in arena.viewers:
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
