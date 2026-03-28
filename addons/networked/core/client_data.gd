class_name MultiplayerClientData
extends Serde

@export var username: StringName
@export_custom(PROPERTY_HINT_RESOURCE_TYPE, "SceneNodePath:ClientComponent")
var spawner_path: SceneNodePath
@export var url: String
var peer_id: int

func serialize() -> PackedByteArray:
	return var_to_bytes({
		username=username, 
		spawner_path=spawner_path.as_uid(),
		url=url,
		peer_id=peer_id
	})

func deserialize(bytes: PackedByteArray) -> void:
	var data := bytes_to_var(bytes)
	assert(data)
	
	username = data.username
	spawner_path = SceneNodePath.new(data.spawner_path)
	url = data.url
	peer_id = data.peer_id
