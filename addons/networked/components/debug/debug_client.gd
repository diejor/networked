class_name DebugClient
extends Control

const OFFSET_2D := Vector2(0, -60)
const OFFSET_3D := Vector3(0, 2.0, 0)

@onready var uid_label: RichTextLabel = %UIDLabel
@onready var username_label: RichTextLabel = %UsernameLabel

var _client: ClientComponent


func _exit_tree() -> void:
	if is_instance_valid(_client):
		Netw.dbg.trace("DebugClient: Freed nameplate for %s" % [_client.owner.name])


func _ready() -> void:
	layout_mode = 0 # Manual position
	anchors_preset = -1


func _process(_delta: float) -> void:
	if not is_instance_valid(_client) or not _client.owner.is_inside_tree():
		visible = false
		return

	# Visibility is now controlled by the NetDebugTreeContext (Add/Remove)
	visible = true

	var auth := _client.owner.get_multiplayer_authority()
	_update_visuals(NetworkedDebugReporter.get_peer_debug_color(auth))
	_update_position(_client.owner)


func follow_client(client: ClientComponent) -> void:
	_client = client


func _update_position(target: Node) -> void:
	if target is Node2D:
		global_position = target.global_position + OFFSET_2D
	elif target is Node3D:
		var cam := get_viewport().get_camera_3d()
		if cam and not cam.is_position_behind(target.global_position + OFFSET_3D):
			global_position = cam.unproject_position(target.global_position + OFFSET_3D)
		else:
			visible = false


func _update_visuals(color: Color) -> void:
	var color_str := color.to_html(false)
	var safe_name := _client.username.replace("[", "[lb]")
	var auth := _client.owner.get_multiplayer_authority()
	
	uid_label.text = "[center][color=#%s]%d[/color][/center]" % [color_str, auth]
	username_label.text = "[center][color=#%s]%s[/color][/center]" % [color_str, safe_name]
