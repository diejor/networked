## Typed event for session registration and unregistration.
class_name NetSessionEvent
extends RefCounted

var tree_name: String
var is_server: bool = false
var backend_class: String = ""


func to_dict() -> Dictionary:
	var d := {"tree_name": tree_name}
	# Only include optional fields if they are populated (e.g., for registration)
	if not backend_class.is_empty() or is_server:
		d["is_server"] = is_server
		d["backend_class"] = backend_class
	return d
