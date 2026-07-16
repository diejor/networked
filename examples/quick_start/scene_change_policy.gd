extends Node

func _ready() -> void:
	var tree := MultiplayerTree.resolve(self)
	assert(tree != null, "Scene change policy requires a MultiplayerTree.")
	tree.session_entered.connect(_configure_scene_change)


# Installs this policy on server authority after the session starts.
func _configure_scene_change() -> void:
	var api := Netw.of(self)
	if api and api.is_server():
		Netw.configure_scene_change(_allow_scene_change)


# Allows the example's one declared destination.
func _allow_scene_change(
		_participant: NetwParticipant,
		scene_name: StringName,
		_args: Array,
) -> bool:
	return scene_name == &"Level2"
