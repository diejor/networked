extends Node

const LEVEL := preload("res://examples/fps/scenes/main.tscn")

@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSession = Netw.session(self)

var level: Node


func _init() -> void:
	Netw.configure_spawn(spawn_level)
	Netw.configure_session(self).app(&"netw-example-fps")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_join(enter_level)


func _ready() -> void:
	session.exited.connect(on_exited)


func spawn_level() -> Node:
	return LEVEL.instantiate()


func enter_level(player: NetwPlayer) -> void:
	if not is_instance_valid(level):
		level = Netw.spawn(spawn_level)
		add_child(level)
	var players: Node = level.get_node(^"Players")
	players.add_child(
		Netw.spawn_player(player, level.spawn_player, players.get_child_count()),
	)


func on_exited(_reason: NetwSession.ExitReason, _detail: String) -> void:
	Input.mouse_mode = Input.MOUSE_MODE_VISIBLE
	browser.visible = true
