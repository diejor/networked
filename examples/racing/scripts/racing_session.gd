extends Node

const TRACK := preload("res://examples/racing/scenes/track.tscn")

@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSessionHandle = Netw.session(self)

var track: Node


func _init() -> void:
	Netw.configure_spawn(spawn_track)
	Netw.configure_session(self).app(&"netw-example-racing")
	Netw.configure_clock(self).ticks_per_second(60)
	Netw.configure_join(enter_race)


func _ready() -> void:
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_track() -> Node:
	return TRACK.instantiate()


func open_track() -> Node:
	if not is_instance_valid(track):
		track = Netw.spawn(spawn_track)
		add_child(track)
	return Netw.scene(track).root


func enter_race(player: NetwPlayer) -> void:
	var root := open_track()
	var grid: Node = root.get_node(^"Vehicles")
	grid.add_child(
		Netw.spawn_player(
			player,
			root.spawn_vehicle,
			grid.get_child_count(),
		),
	)


func show_browser() -> void:
	browser.visible = true
