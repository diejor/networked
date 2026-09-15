extends Node

const TRACK := preload("res://examples/racing/scenes/track.tscn")

@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSessionHandle = Netw.session(self)

var track: Node
var spawner: RacingVehicleSpawner
var grid: Array[NetwParticipant] = []


func _init() -> void:
	Netw.configure_spawn(spawn_track)
	Netw.configure_session(self).app_id(&"netw-example-racing")
	Netw.configure_clock(self).tickrate(60)
	Netw.configure_join(self, enter_race)


func _ready() -> void:
	session.scene_live.connect(on_scene_live)
	session.presentation_changed.connect(on_presentation_changed)
	session.participant_left.connect(leave_race)
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_track() -> Node:
	return TRACK.instantiate()


func open_track() -> void:
	if is_instance_valid(track):
		return
	track = Netw.spawn(spawn_track)
	add_child(track)
	spawner = Netw.scene(track).root.get_node(^"VehicleSpawner")


func enter_race(participant: NetwParticipant) -> void:
	open_track()
	grid.append(participant)
	Netw.scene(track).watch(participant)
	spawner.spawn_vehicle(participant, grid.size() - 1)


func leave_race(participant: NetwParticipant) -> void:
	grid.erase(participant)


func on_scene_live(scene: NetwSceneHandle) -> void:
	session.present(scene)


func on_presentation_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	browser.visible = to == null


func show_browser() -> void:
	browser.visible = true
