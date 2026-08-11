## Renders the swarm straight through [RenderingServer], with no node for any
## mob and no [SceneTree] involvement per row.
##
## Rows deliberately do not enter the display pump: that pump is one runtime,
## one history, and one virtual write per row per frame, which is exactly the
## per-row crossing tables exist to avoid. Batch display is this loop instead.
## [codeblock]
## table_received   prev = next (a rebind, free)
##                  next = read_column().duplicate()
## _process         blend prev toward next on my own clock
##                  one multimesh_set_buffer for the whole swarm
## [/codeblock]
## Interpolation is a caller pattern with no new surface, because a read is a
## live view: rebinding keeps the old buffer alive and one
## [method PackedVector3Array.duplicate] per wave is the whole history cost.
extends Node3D
class_name SwarmView

## How far behind the newest wave the render lags, as a fraction of the gap
## between waves. Higher hides more jitter and shows older state.
@export var interpolation_delay: float = 1.0

## Size of the cube drawn for one mob.
@export var mob_size: float = 0.6

## Colour a mob is drawn in while it carries a [SwarmTables.Burning] row.
@export var burning_tint: Color = Color(1.0, 0.35, 0.1)

var _multimesh := RID()
var _instance := RID()
var _mesh: BoxMesh
var _material: StandardMaterial3D

var _mobs: RID
var _burning: RID

var _prev := PackedVector3Array()
var _next := PackedVector3Array()
var _wave_seconds: float = 0.0
var _since_wave: float = 0.0

var api: NetwMultiplayer:
	get:
		return multiplayer as NetwMultiplayer


func _ready() -> void:
	SwarmTables.declare_all()
	_mesh = BoxMesh.new()
	_mesh.size = Vector3.ONE * mob_size
	_material = StandardMaterial3D.new()
	_material.vertex_color_use_as_albedo = true
	_mesh.surface_set_material(0, _material)

	_multimesh = RenderingServer.multimesh_create()
	RenderingServer.multimesh_set_mesh(_multimesh, _mesh.get_rid())
	_instance = RenderingServer.instance_create2(
		_multimesh,
		get_world_3d().scenario,
	)


func _exit_tree() -> void:
	if _instance.is_valid():
		RenderingServer.free_rid(_instance)
	if _multimesh.is_valid():
		RenderingServer.free_rid(_multimesh)


## Subscribes to the session's table waves. Call once the session is online.
func start() -> void:
	_mobs = api.table_find(SwarmTables.Mobs.NAME)
	_burning = api.table_find(SwarmTables.Burning.NAME)
	if not api.table_received.is_connected(_on_table_received):
		api.table_received.connect(_on_table_received)


func _on_table_received(table: RID, _tick: int) -> void:
	if table != _mobs:
		return
	_prev = _next
	_next = api.table_read_column(_mobs, SwarmTables.Mobs.pos).duplicate()
	_wave_seconds = maxf(_since_wave, 0.001)
	_since_wave = 0.0


func _process(delta: float) -> void:
	if not _mobs.is_valid():
		return
	_since_wave += delta
	var count := mini(_prev.size(), _next.size())
	if count == 0:
		count = _next.size()
		_prev = _next
	if count == 0:
		RenderingServer.multimesh_set_visible_instances(_multimesh, 0)
		return

	var blend := clampf(
		_since_wave / (_wave_seconds * maxf(interpolation_delay, 0.01)),
		0.0,
		1.0,
	)
	RenderingServer.multimesh_allocate_data(
		_multimesh,
		count,
		RenderingServer.MULTIMESH_TRANSFORM_3D,
		true,
	)
	RenderingServer.multimesh_set_buffer(
		_multimesh,
		_instance_buffer(count, blend),
	)
	RenderingServer.multimesh_set_visible_instances(_multimesh, count)


# Builds one flat float buffer for the whole swarm: twelve transform floats and
# four colour floats per instance, which is what multimesh_set_buffer expects.
func _instance_buffer(count: int, blend: float) -> PackedFloat32Array:
	var alight := _burning_rows(count)
	var buffer := PackedFloat32Array()
	buffer.resize(count * 16)
	for i in count:
		var at := i * 16
		var p := _prev[i].lerp(_next[i], blend)
		buffer[at + 0] = 1.0
		buffer[at + 5] = 1.0
		buffer[at + 10] = 1.0
		buffer[at + 3] = p.x
		buffer[at + 7] = p.y
		buffer[at + 11] = p.z
		var tint := burning_tint if alight[i] else Color.WHITE
		buffer[at + 12] = tint.r
		buffer[at + 13] = tint.g
		buffer[at + 14] = tint.b
		buffer[at + 15] = tint.a
	return buffer


# Marks which mob rows carry a Burning row, through one bulk join rather than
# one lookup per rendered instance.
func _burning_rows(count: int) -> PackedByteArray:
	var alight := PackedByteArray()
	alight.resize(count)
	if not _burning.is_valid():
		return alight
	var burning_routes := api.table_read_routes(_burning)
	if burning_routes.is_empty():
		return alight
	var rows := api.table_get_rows(_mobs, burning_routes)
	for i in rows.size():
		if rows[i] >= 0 and rows[i] < count:
			alight[rows[i]] = 1
	return alight
