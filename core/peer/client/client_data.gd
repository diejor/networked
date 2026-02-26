class_name MultiplayerClientData
extends Serde

@export var username: StringName
@export_file var scene_path: String
@export var url: String

func serialize() -> PackedByteArray:
	assert(not username.is_empty())
	assert(not scene_path.is_empty())
	
	return var_to_bytes({
		username=username, 
		scene_path=scene_path,
		url=url
	})

func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)
	
	username = data.username
	scene_path = data.scene_path
	if "url" in data:
		data.url = url
	
	assert(not username.is_empty())
	assert(not scene_path.is_empty())
