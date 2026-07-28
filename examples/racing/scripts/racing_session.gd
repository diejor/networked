extends Node
## Tree-less session bootstrap for the racing example.
##
## The example carries no [MultiplayerTree] node. This root script registers the
## session's [NetwSessionConfig] and a single-scene [NetwSceneConfig] whose only
## declared scene is the track, so joining a host through the child
## [ConnectBrowser] admits the participant straight onto the track and spawns
## their car. The session is already mounted when this runs, by the
## [code]networked/install_as_default[/code] autoload at startup.

const TRACK := preload("res://examples/racing/scenes/track.tscn")
const SIMULATE_NEAREST_VAR := "NETW_RACING_SIMULATE_NEAREST"

## Simulates the nearest replicated opponent inside each local car's island.
@export var simulate_nearest_opponent := false

@onready var _browser: ConnectBrowser = %ConnectBrowser

var api: NetwMultiplayer:
	get:
		return multiplayer


func _enter_tree() -> void:
	var session := NetwSessionConfig.new()
	session.app_id = &"netw-example-racing"
	api.object_configuration_add(self, session)

	var scenes := NetwSceneConfig.new()
	scenes.concurrency = NetwSceneConfig.Concurrency.SINGLE
	scenes.initial_scenes = [TRACK]
	scenes.scenes = { &"Track": TRACK }
	api.object_configuration_add(self, scenes)


func _ready() -> void:
	api.participant_joined.connect(_on_participant_joined)
	api.scenes.scene_spawned.connect(_on_scene_spawned)
	api.local_scene_changed.connect(_on_local_scene_changed)
	api.session_ended.connect(_show_browser)
	api.server_disconnected.connect(_show_browser)
	if RacingRegime.armed():
		_run_regime.call_deferred()


# Hands the process to the scripted capture peer: [RacingRegime] contributes
# the gestures and session hooks, [NetwRegimePeer] owns the lifecycle and the
# summary, and the process exits when the run completes.
func _run_regime() -> void:
	await RacingRegime.attach(self, api).run()


func _on_participant_joined(participant: NetwParticipant) -> void:
	var track: MultiplayerScene = api.scenes.scene(&"Track")
	if api.is_server() and is_instance_valid(track):
		track.admit(participant)


# The listen host is accepted before the startup scene spawns, so its join
# admission finds no scene. Admission re-runs when the scene arrives, keeping
# join order and scene order decoupled.
func _on_scene_spawned(scene: MultiplayerScene) -> void:
	scene.player_entered.connect(_on_car_entered.bind(scene))
	scene.player_left.connect(_on_car_left.bind(scene))
	_declare_islands(scene)
	if not api.is_server():
		return
	for participant: NetwParticipant in api.participants:
		if participant.current_scene == null:
			scene.admit(participant)


# Names every other car on the track as a participant in this car's island, and
# claims that the island reproduces exactly.
#
# The roster is written rather than produced, and that is the whole point. Two
# peers each resolve their own interest scope, so a roster produced from interest
# is not a fact both of them can claim, and an island that cannot claim it is
# compared by tolerance forever. Naming the cars is the game asserting what the
# engine is not entitled to assume, which is what
# [method PredictionHandle.IslandConfig.exact] means.
#
# Contact is unaffected by the claim. Touching another car still opens an
# out-of-domain window, because a car this peer only displays is a stale stand-in
# its solver cannot reproduce.
func _declare_islands(scene: MultiplayerScene) -> void:
	var cars := scene.get_players()
	for car: NetwEntity in cars:
		var island := car.prediction.island().exact()
		for other: NetwEntity in cars:
			if other != car:
				island.add(other)
		if _simulates_nearest():
			island.simulate_nearest(1)


# A join changes an antecedent every car's fingerprint carries, so every roster
# is rebuilt rather than only the newcomer's.
func _on_car_entered(_car: NetwEntity, scene: MultiplayerScene) -> void:
	_declare_islands(scene)


func _on_car_left(car: NetwEntity, scene: MultiplayerScene) -> void:
	for other: NetwEntity in scene.get_players():
		if other != car:
			other.prediction.island().remove(car)


func _simulates_nearest() -> bool:
	return simulate_nearest_opponent \
			or not OS.get_environment(SIMULATE_NEAREST_VAR).is_empty()


# The browser steps aside once the local participant is racing and returns when
# the session ends.
func _on_local_scene_changed(_from: MultiplayerScene, to: MultiplayerScene) -> void:
	_browser.visible = to == null


func _show_browser() -> void:
	_browser.visible = true
