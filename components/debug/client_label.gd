class_name DebugClient
extends Node2D

@export var client: ClientComponent
@onready var label: Label = $Label

func _ready() -> void:
	var username := client.username if not client.username.is_empty() else "Spawner"
	var uid := owner.get_multiplayer_authority()
	reparent.call_deferred(owner.owner)
	label.text = "%s\n%s" % [username, uid]
