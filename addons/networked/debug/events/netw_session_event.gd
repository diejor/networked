## Typed event for session registration and unregistration.
class_name NetwSessionEvent
extends RefCounted

var tree_name: String
var username: String = ""
var role: MultiplayerTree.Role = MultiplayerTree.Role.NONE
var is_server: bool = false
var backend_class: String = ""
var rid: String = ""
var peer_id: int = 0

## True only once the tree reaches [constant MultiplayerTree.State.ONLINE]. The
## offline phase of two-phase registration reports [code]false[/code] so the
## editor shows the tree as not-yet-connected instead of assuming registration
## implies a live peer.
var online: bool = false

## True when the tree is a debugger-spawned clone, so the editor row offers an
## embed-toggle control instead of the "+" spawn control.
var spawned: bool = false


## Serializes this event into a [Dictionary].
func to_dict() -> Dictionary:
	var d := {
		"tree_name": tree_name,
		"username": username,
		"role": role,
		"role_name": MultiplayerTree.Role.keys()[role],
		"peer_id": peer_id,
		"online": online,
	}
	if spawned:
		d["spawned"] = true
	if not rid.is_empty():
		d["_rid"] = rid

	# Only include optional fields if they are populated (e.g., for
	# registration).
	if not backend_class.is_empty() or is_server:
		d["is_server"] = is_server
		d["backend_class"] = backend_class
	return d
