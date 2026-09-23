extends Node

const LOBBY := preload("res://examples/rocket_league/scenes/lobby_level.tscn")
const ARENA := "res://examples/rocket_league/scenes/arena.tscn"

@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSessionHandle = Netw.session(self)

var level: Node


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)
	Netw.configure_lagcomp(self)
	Netw.configure_session(self).app(&"netw-example-rocket-league")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_join(enter_lobby)


func _ready() -> void:
	session.scene_live.connect(on_scene_live)
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_lobby() -> Node:
	return LOBBY.instantiate()


func open_lobby() -> void:
	if is_instance_valid(level):
		return
	level = Netw.spawn(spawn_lobby)
	add_child(level)


func enter_lobby(player: NetwPlayer) -> void:
	var arena: NetwSceneHandle = Netw.scene(self, &"Arena")
	if arena == null:
		open_lobby()
		Netw.scene(level).watch(player)
		return
	add_car(arena, player)


func add_car(arena: NetwSceneHandle, player: NetwPlayer) -> void:
	var players: Node = arena.root.get_node(^"Players")
	if not player.bodies.is_empty():
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
		)
	)


func start_match() -> void:
	Netw.change_scene_to_file(self, ARENA)


func open_match(arena: NetwSceneHandle) -> void:
	for player: NetwPlayer in session.players:
		if player.is_active:
			add_car(arena, player)


func on_scene_live(scene: NetwSceneHandle) -> void:
	if scene.label == &"Arena":
		install_stepper(scene.root as Node3D)

	if scene.label == &"Lobby":
		var in_lobby: InLobby = scene.root.find_child("InLobby", true, false)
		in_lobby.start_pressed.connect(start_match)

	if scene.label == &"Arena" and multiplayer.is_server():
		open_match(scene)


func install_stepper(root: Node3D) -> void:
	multiplayer.predict_stepper_install(
		root.get_world_3d().space,
		RocketJoltStepper.new(),
	)


func show_browser() -> void:
	browser.visible = true
