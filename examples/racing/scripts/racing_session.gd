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
	# Every car on the track shares one approximate island, so contact against
	# another car classifies as a boundary breach instead of an undeclared
	# contact. Promotion to simulated fidelity stays opt-in.
	#
	# This is the only island declaration racing makes, and it has to be. A car
	# that declared its own would opt out of this rule entirely rather than
	# refine it, taking the promotion below with it.
	var island := scene.prediction_island() \
			.approximate() \
			.from_interest(&"race")
	if simulate_nearest_opponent \
			or not OS.get_environment(SIMULATE_NEAREST_VAR).is_empty():
		island.simulate_nearest(1)
	if not api.is_server():
		return
	for participant: NetwParticipant in api.participants:
		if participant.current_scene == null:
			scene.admit(participant)


# The browser steps aside once the local participant is racing and returns when
# the session ends.
func _on_local_scene_changed(_from: MultiplayerScene, to: MultiplayerScene) -> void:
	_browser.visible = to == null


func _show_browser() -> void:
	_browser.visible = true
