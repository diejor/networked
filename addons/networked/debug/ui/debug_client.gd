class_name DebugClient
extends Control

const OFFSET_2D := Vector2(0, -60)
const OFFSET_3D := Vector3(0, 2.0, 0)

@onready var uid_label: RichTextLabel = %UIDLabel
@onready var username_label: RichTextLabel = %UsernameLabel

var _client: SpawnerComponent
var _target: Node
var _username: String = ""


func _exit_tree() -> void:
	if is_instance_valid(_client):
		Netw.dbg.trace(
			"DebugClient: Freed nameplate for %s" % [_client.owner.name]
		)
	elif is_instance_valid(_target):
		Netw.dbg.trace(
			"DebugClient: Freed nameplate for %s" % [_target.name]
		)


func _ready() -> void:
	layout_mode = 0
	anchors_preset = -1


func _process(_delta: float) -> void:
	var node := _resolve_target()
	if not is_instance_valid(node) or not node.is_inside_tree():
		visible = false
		return

	visible = true
	var auth := node.get_multiplayer_authority()
	_update_visuals(NetworkedDebugReporter.get_peer_debug_color(auth))
	_update_position(node)


func follow_client(client: SpawnerComponent) -> void:
	_client = client
	_target = null
	_username = client.username if client else ""


func follow_target(target: Node, username: String) -> void:
	_client = null
	_target = target
	_username = username


func _resolve_target() -> Node:
	if is_instance_valid(_client):
		return _client.owner
	if is_instance_valid(_target):
		return _target
	return null


func _update_position(target: Node) -> void:
	if target is Node2D:
		global_position = target.global_position + OFFSET_2D
	elif target is Node3D:
		var cam := get_viewport().get_camera_3d()
		if cam and not cam.is_position_behind(
			target.global_position + OFFSET_3D
		):
			global_position = cam.unproject_position(
				target.global_position + OFFSET_3D
			)
		else:
			visible = false


func _update_visuals(color: Color) -> void:
	var color_str := color.to_html(false)
	var safe_name := _username.replace("[", "[lb]")
	var node := _resolve_target()
	var auth := node.get_multiplayer_authority() if node else 0

	uid_label.text = (
		"[center][color=#%s]%d[/color][/center]"
		% [color_str, auth]
	)
	username_label.text = (
		"[center][color=#%s]%s[/color][/center]"
		% [color_str, safe_name]
	)
