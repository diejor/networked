extends Node3D

const PLAYER := preload("res://examples/playground/player.tscn")
const CUBE := preload("res://examples/playground/cube.tscn")
const ROWS := 30
const CUBE_SIDE := 0.25
const CUBE_GAP := 0.4
const SPAWN_RADIUS := 4.0
const SPAWN_HEIGHT := 6.0
const COLORS: Array[Color] = [
	Color(0.9, 0.15, 0.15),
	Color(0.2, 0.75, 0.2),
	Color(0.2, 0.35, 0.95),
	Color(0.95, 0.85, 0.15),
	Color(1.0, 0.55, 0.1),
	Color(0.6, 0.25, 0.85),
	Color(0.1, 0.7, 0.7),
	Color(1.0, 0.45, 0.7),
]

var free_material := StandardMaterial3D.new()
var cubes := Node3D.new()

@onready var players: Node3D = $Players


func _init() -> void:
	Netw.configure_multiplayer_scene(self).labeled(&"Playground")
	Netw.configure_spawn(spawn_player)
	free_material.albedo_color = Color(0.6, 0.6, 0.6)
	cubes.name = &"Cubes"
	add_child(cubes)
	var rows := ROWS
	if OS.has_environment("PLAYGROUND_ROWS"):
		rows = int(OS.get_environment("PLAYGROUND_ROWS"))
	var spacing := CUBE_SIDE + CUBE_GAP
	var origin := Vector3.ONE * -0.5 * (rows - 1) * spacing
	origin.y = CUBE_SIDE * 0.5
	for row in rows:
		for col in rows:
			var cube := CUBE.instantiate() as PlayCube
			cube.name = "Cube_%d_%d" % [row, col]
			cube.position = origin + Vector3(col, 0.0, row) * spacing
			cube.get_node(^"Visual").material_override = free_material
			cube.entity.control_changed.connect(paint.bind(cube).unbind(2))
			cubes.add_child(cube)


func _ready() -> void:
	players.child_entered_tree.connect(watch)
	for cube: PlayCube in cubes.get_children():
		cube.entity.simulation.mode_changed.connect(paint.bind(cube).unbind(2))


func spawn_player(slot: int) -> Node:
	var player := PLAYER.instantiate() as PlayPlayer
	var angle := TAU * slot / COLORS.size()
	var at := Vector3(sin(angle), 0.0, cos(angle)) * SPAWN_RADIUS
	player.position = at + Vector3.UP * SPAWN_HEIGHT
	player.forward = -at.normalized()
	player.color = COLORS[slot % COLORS.size()]
	return player


func watch(player: Node) -> void:
	player.ready.connect(repaint, CONNECT_ONE_SHOT)


func repaint() -> void:
	for cube: PlayCube in cubes.get_children():
		paint(cube)


func paint(cube: PlayCube) -> void:
	var e := cube.entity
	var claiming := e.is_control_pending and e.is_controlled_locally
	cube.paint(color_of(multiplayer.get_unique_id() if claiming else e.controller))


func color_of(peer: int) -> Color:
	if peer == 0:
		return free_material.albedo_color
	for player: PlayPlayer in players.get_children():
		if player.entity.controller == peer:
			return player.color
	return free_material.albedo_color
