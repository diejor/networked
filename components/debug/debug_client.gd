class_name DebugClient
extends Control

@export var client: ClientComponent
@onready var uid_label: RichTextLabel = %UIDLabel
@onready var username_label: RichTextLabel = %UsernameLabel

var original_pos: Vector2

func _enter_tree() -> void:
	original_pos = position

func _ready() -> void:
	var username := client.username if not client.username.is_empty() else "Spawner"
	var uid := owner.get_multiplayer_authority()
	
	uid_label.text = str(uid)
	username_label.text = username
