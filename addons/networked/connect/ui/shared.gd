## Static formatting helpers shared by [ConnectBrowser] and its
## merged popup. Lifted out of the old server_browser scripts so the
## same label/address rendering applies everywhere.
class_name ConnectUiShared
extends RefCounted

## Human-readable label for a [BackendPeer] template (class name or
## resource filename).
static func format_backend_label(backend: BackendPeer) -> String:
	if backend == null:
		return "-"
	if backend.has_method("get_display_name"):
		var custom_name := backend.get_display_name()
		if not custom_name.is_empty() and custom_name != "Generic":
			return custom_name
	var name: String
	if backend.resource_path.is_empty() or "::" in backend.resource_path:
		name = _backend_class_name(backend)
	else:
		name = backend.resource_path.get_file().get_basename()
	if name.ends_with("Backend"):
		name = name.trim_suffix("Backend")
	elif name.ends_with("_backend"):
		name = name.trim_suffix("_backend")
	elif name.ends_with("-backend"):
		name = name.trim_suffix("-backend")
	return name


## Displayable address for [param target] — its explicit address if
## set, otherwise the backend's join URL / address. "-" when nothing
## is known.
static func format_address(target: JoinTarget) -> String:
	if target == null:
		return "-"
	var address := target.address.strip_edges()
	if not address.is_empty():
		return address
	if target.backend == null:
		return "-"
	if target.backend.has_method("build_url"):
		return str(target.backend.call("build_url", ""))
	return target.backend.get_join_address()


## Label for a [SceneNodePath] spawner option in the picker.
static func format_spawner_label(path: SceneNodePath) -> String:
	if path == null:
		return "(none)"
	if path.node_path.is_empty():
		return path.scene_path
	return path.node_path


static func _backend_class_name(backend: BackendPeer) -> String:
	var script := backend.get_script()
	if script and not script.get_global_name().is_empty():
		return script.get_global_name()
	return backend.get_class()
