## Typed manifest for [code]ZOMBIE_PLAYER_DETECTED[/code] events.
class_name NetZombieManifest
extends NetManifest

## Node paths of zombie player nodes still owned by the disconnected peer.
var errors: Array[String]
var disconnected_peer_id: int


func to_dict() -> Dictionary:
	var d := super.to_dict()
	d["errors"] = errors
	return d
