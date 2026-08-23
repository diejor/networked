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

# Capture-only switch that drops the island's exactness claim, so the A/B runs
# from one build instead of two. The claim is the sole gate on IN_DOMAIN, and
# therefore on whether the withheld-field machinery does anything at all: an
# out-of-domain divergence restores the whole closure, teleport_only() marks
# included. Under .approximate() the wall case takes that escape hatch, which
# is the antecedent the momentum-demote brief could not rule out.
# TODO: delete this once the island A/B is settled either way.
const ISLAND_APPROXIMATE_VAR := "NETW_RACING_ISLAND_APPROXIMATE"

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
	scenes.declare_scene(&"Track", TRACK, true)
	api.object_configuration_add(self, scenes)


func _ready() -> void:
	api.participant_joined.connect(_on_participant_joined)
	api.scene_live.connect(_on_scene_live)
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
	var track := api.scene_handle(api.scene_find(&"Track"))
	if api.is_server() and track != null:
		track.admit(participant)


# The listen host is accepted before the startup scene spawns, so its join
# admission finds no scene. Admission re-runs when the scene arrives, keeping
# join order and scene order decoupled.
func _on_scene_live(track: NetwSceneHandle) -> void:
	track.on_player_entered(_on_car_entered.bind(track))
	track.on_player_left(_on_car_left.bind(track))
	_declare_islands(track)
	if not api.is_server():
		return
	for participant: NetwParticipant in api.participants:
		if participant.current_scene == null:
			track.admit(participant)


# Names every other car on the track as a participant in this car's island, and
# claims that the island reproduces exactly.
#
# The roster is written rather than produced, and that is the whole point. Two
# peers each resolve their own interest scope, so a roster produced from interest
# is not a fact both of them can claim, and an island that cannot claim it is
# compared by tolerance forever. Naming the cars is the game asserting what the
# engine is not entitled to assume, which is what
# [member NetwPredictIsland.exact_claim] means.
#
# Contact is unaffected by the claim. Touching another car still opens an
# out-of-domain window, because a car this peer only displays is a stale stand-in
# its solver cannot reproduce.
func _declare_islands(scene: NetwSceneHandle) -> void:
	var approximate := not OS.get_environment(ISLAND_APPROXIMATE_VAR).is_empty()
	var cars := scene.players
	for car: NetwEntity in cars:
		var prediction: NetwPredictionHandle = car.prediction
		var island := prediction.island
		if approximate:
			island.approximate = true
		else:
			island.exact_claim = true
		for other: NetwEntity in cars:
			if other != car:
				island.add(other)
		if _simulates_nearest():
			island.simulate_nearest(1)


# A join changes an antecedent every car's fingerprint carries, so every roster
# is rebuilt rather than only the newcomer's.
func _on_car_entered(_car: NetwEntity, scene: NetwSceneHandle) -> void:
	_declare_islands(scene)


func _on_car_left(car: NetwEntity, scene: NetwSceneHandle) -> void:
	for other: NetwEntity in scene.players:
		if other != car:
			other.prediction.island.remove(car)


func _simulates_nearest() -> bool:
	return simulate_nearest_opponent \
			or not OS.get_environment(SIMULATE_NEAREST_VAR).is_empty()


# The browser steps aside once the local participant is racing and returns when
# the session ends.
func _on_local_scene_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	_browser.visible = to == null


func _show_browser() -> void:
	_browser.visible = true
