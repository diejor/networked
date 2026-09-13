extends Node

const TRACK := preload("res://examples/racing/scenes/track.tscn")

@onready var browser: ConnectBrowser = %ConnectBrowser

var track: Node


func _init() -> void:
	Netw.configure_spawn(spawn_track)
	Netw.configure_session(self).app_id(&"netw-example-racing")
	Netw.configure_clock(self).tickrate(60)
	Netw.configure_join(self, enter_race)


func _ready() -> void:
	var session: NetwSessionHandle = Netw.session(self)
	session.local_scene_changed.connect(on_local_scene_changed)
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_track() -> Node:
	return TRACK.instantiate()


func enter_race(_participant: NetwParticipant) -> NetwSceneHandle:
	if not is_instance_valid(track):
		track = Netw.spawn(spawn_track)
		add_child(track)
	return Netw.scene(track)


func on_local_scene_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	browser.visible = to == null


func show_browser() -> void:
	browser.visible = true
