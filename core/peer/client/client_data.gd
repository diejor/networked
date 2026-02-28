class_name MultiplayerClientData
extends Serde

@export var username: StringName
@export_file var scene_path: String
@export var url: String
var peer_id: int

func serialize() -> PackedByteArray:
	return var_to_bytes({
		username=username, 
		scene_path=scene_path,
		url=url,
		peer_id=peer_id
	})

func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)
	
	username = data.username
	scene_path = data.scene_path
	if "url" in data:
		data.url = url
	if "peer_id" in data:
		data.peer_id = peer_id
