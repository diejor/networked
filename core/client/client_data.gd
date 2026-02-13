class_name MultiplayerClientData
extends Serde

@export var username: StringName
@export_file var scene_path: String
var peer_id: int


func is_valid_scene() -> String:
	if scene_path in Networked.config.clients:
		return ""
	return	"Provided `scene_path: %s` is not tracked by `%s`. Add \
the scene to `ProjectSettings/%s`." % [
	scene_path, 'Networked.config.clients', Networked.SETTING_PATH]

func serialize() -> PackedByteArray:
	assert(not username.is_empty())
	assert(not scene_path.is_empty())
	return var_to_bytes({
		username=username, 
		scene_path=scene_path, 
		peer_id=peer_id
	})

func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)
	username = data.username
	scene_path = data.scene_path
	peer_id = data.peer_id
	assert(not username.is_empty())
	assert(not scene_path.is_empty())
	assert(peer_id != 0)
