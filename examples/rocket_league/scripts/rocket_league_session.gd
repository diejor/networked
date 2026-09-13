extends Node

const LOBBY := preload("res://examples/rocket_league/scenes/lobby_level.tscn")

@onready var browser: ConnectBrowser = %ConnectBrowser

var stepped := false
var level: Node


func _init() -> void:
	Netw.configure_spawn(spawn_lobby)
	Netw.configure_lagcomp(self)
	Netw.configure_session(self).app_id(&"netw-example-rocket-league")
	Netw.configure_clock(self).tickrate(60)
	Netw.configure_join(self, enter_lobby)


func _ready() -> void:
	var session: NetwSessionHandle = Netw.session(self)
	session.scene_live.connect(on_scene_live)
	session.local_scene_changed.connect(on_local_scene_changed)
	session.ended.connect(show_browser)
	session.disconnected.connect(show_browser)


func spawn_lobby() -> Node:
	return LOBBY.instantiate()


func open_lobby() -> void:
	if is_instance_valid(level):
		return
	level = Netw.spawn(spawn_lobby)
	add_child(level)


func enter_lobby(_participant: NetwParticipant) -> NetwSceneHandle:
	var arena: NetwSceneHandle = Netw.scene(self, &"Arena")
	if arena != null:
		return arena
	open_lobby()
	return Netw.scene(level)


func on_scene_live(scene: NetwSceneHandle) -> void:
	if scene.label == &"Arena":
		install_stepper(scene.root as Node3D, scene)
		scene.observe(NetwMultiplayer.SCENE_EVENT_PLAYER, car_edge.bind(scene))
		declare_islands(scene)

	if not multiplayer.is_server():
		return
	for participant: NetwParticipant in Netw.session(self).participants:
		if participant.current_scene == null:
			scene.admit(participant)


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


func on_local_scene_changed(_from: NetwSceneHandle, to: NetwSceneHandle) -> void:
	browser.visible = to == null


func show_browser() -> void:
	browser.visible = true
