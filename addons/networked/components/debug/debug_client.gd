## Debug overlay that displays the peer UID and username for a [ClientComponent] owner.
##
## Automatically freed in non-debug builds by [DebugFeature]. Attach inside the player scene.
class_name DebugClient
extends Control

@onready var client: ClientComponent = owner if owner is ClientComponent else null
@onready var uid_label: RichTextLabel = %UIDLabel
@onready var username_label: RichTextLabel = %UsernameLabel

var original_pos: Vector2

func _init() -> void:
	DebugFeature.free_if_debug(self)

func _enter_tree() -> void:
	original_pos = position

func _ready() -> void:
	if not is_instance_valid(client):
		queue_free()
		return
	
	var username := client.username if not client.username.is_empty() else "Spawner"
	var uid := owner.get_multiplayer_authority()
	
	uid_label.text = str(uid)
	username_label.text = username
