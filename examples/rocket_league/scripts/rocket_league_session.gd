extends Node

const LOBBY := preload("res://examples/rocket_league/scenes/lobby_level.tscn")
const ARENA := "res://examples/rocket_league/scenes/arena.tscn"

@onready var browser: ConnectBrowser = %ConnectBrowser
@onready var session: NetwSessionHandle = Netw.session(self)

var stepped := false
var level: Node
var roster: Array[NetwParticipant] = []


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)
	Netw.configure_lagcomp(self)
	Netw.configure_session(self).app_id(&"netw-example-rocket-league")
	Netw.configure_clock(self).tickrate(60)
	Netw.configure_join(self, enter_lobby)


func _ready() -> void:
	session.scene_live.connect(on_scene_live)
	session.presentation_changed.connect(on_presentation_changed)
	session.participant_left.connect(leave_match)
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_lobby() -> Node:
	return LOBBY.instantiate()


func open_lobby() -> void:
	if is_instance_valid(level):
		return
	level = Netw.spawn(spawn_lobby)
	add_child(level)


func enter_lobby(participant: NetwParticipant) -> void:
	roster.append(participant)
	var arena: NetwSceneHandle = Netw.scene(self, &"Arena")
	if arena != null:
		arena.watch(participant)
		car_spawner(arena).add_car(participant, roster.size() - 1)
		return
	open_lobby()
	Netw.scene(level).watch(participant)


func leave_match(participant: NetwParticipant) -> void:
	roster.erase(participant)


func start_match() -> void:
	Netw.change_scene_to_file(self, ARENA)


func car_spawner(arena: NetwSceneHandle) -> RocketPlayerSpawner:
	return arena.root.get_node(^"PlayerSpawner")


func open_match(arena: NetwSceneHandle) -> void:
	if not arena.root.is_node_ready():
		arena.root.ready.connect(open_match.bind(arena), CONNECT_ONE_SHOT)
		return
	var spawner := car_spawner(arena)
	for slot in roster.size():
		spawner.add_car(roster[slot], slot)


func on_scene_live(scene: NetwSceneHandle) -> void:
	if scene.label == &"Arena":
		install_stepper(scene.root as Node3D, scene)
		scene.observe(NetwMultiplayer.SCENE_EVENT_PLAYER, car_edge.bind(scene))
		declare_islands(scene)

	if scene.label == &"Lobby":
		var in_lobby: InLobby = scene.root.find_child("InLobby", true, false)
		in_lobby.start_pressed.connect(start_match)

	session.present(scene)
	if scene.label == &"Arena" and multiplayer.is_server():
		open_match(scene)


func install_stepper(root: Node3D, arena: NetwSceneHandle) -> void:
	if not root.is_inside_tree():
		root.tree_entered.connect(
			install_stepper.bind(root, arena),
			CONNECT_ONE_SHOT,
		)
		return
	var space: RID = root.get_world_3d().space
	multiplayer.predict_stepper_install(space, RocketJoltStepper.new())
	stepped = multiplayer.predict_get_stepper(space) != null
	declare_islands(arena)


func car_edge(_entered: bool, _car: NetwEntity, arena: NetwSceneHandle) -> bool:
	declare_islands.call_deferred(arena)
	return false


func declare_islands(arena: NetwSceneHandle) -> void:
	var bodies := simulated_bodies(arena)
	for member: NetwEntity in bodies:
		if not (member.owner is RocketCar):
			continue
		var island := member.prediction.island
		island.exact_claim = stepped
		island.approximate = not stepped
		island.reconcile = NetwPredict.RECONCILE_JOINT if stepped \
		else NetwPredict.RECONCILE_INDEPENDENT
		for other: NetwEntity in bodies:
			if other != member:
				island.add(other)


func simulated_bodies(arena: NetwSceneHandle) -> Array[NetwEntity]:
	var out: Array[NetwEntity] = []
	for entity: NetwEntity in arena.entities:
		if entity.owner is RocketCar or entity.owner is RocketBall:
			out.append(entity)
	return out


func on_presentation_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	browser.visible = to == null


func show_browser() -> void:
	browser.visible = true
