@tool
class_name TubeBackend
extends BackendPeer

## Path to the TubeClient node relative to the MultiplayerTree.
@export_node_path("TubeClient") var tube_client_path: NodePath

var tube: TubeClient

func setup(tree: MultiplayerTree) -> Error:
	if tube_client_path.is_empty():
		push_error("TubeBackend: TubeClient path is empty.")
		return ERR_UNCONFIGURED
		
	if not tree.has_node(tube_client_path):
		push_error("TubeBackend: Cannot find TubeClient at path: ", tube_client_path)
		return ERR_DOES_NOT_EXIST
		
	tube = tree.get_node(tube_client_path) as TubeClient
	if not tube:
		push_error("TubeBackend: Node at path is not a TubeClient.")
		return ERR_INVALID_DATA
	
	tube.multiplayer_root_node = tree
	tree._disconnect_backend_signals()
	api = tube.multiplayer_api
	tree._connect_backend_signals()
	
	return OK

func host() -> Error:
	assert(tube, "Backend needs to `setup()` first.")
		
	tube.create_session()
	if tube.state == TubeClient.State.CREATING_SESSION or tube.state == TubeClient.State.SESSION_CREATED:
		print("Tube session ready at `%s` (saved to clipboard). " % tube.session_id)
		DisplayServer.clipboard_set(tube.session_id)
		return OK
		
	return ERR_CANT_CREATE

func join(server_address: String, _username: String = "") -> Error:
	assert(tube, "Backend needs to `setup()` first.")
		
	tube.join_session(server_address)
	if tube.state == TubeClient.State.JOINING_SESSION or tube.state == TubeClient.State.SESSION_JOINED:
		return OK
		
	return ERR_CANT_CONNECT


func peer_reset_state() -> void:
	if is_instance_valid(tube) and tube.state != TubeClient.State.IDLE:
		tube.leave_session()
	super.peer_reset_state()

func get_join_address() -> String:
	if is_instance_valid(tube) and not tube.session_id.is_empty():
		return tube.session_id
		
	return super.get_join_address()

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	var warnings := PackedStringArray()
	
	var tube: TubeClient = tree.get_node_or_null(tube_client_path)
	if tube_client_path.is_empty():
		warnings.append("TubeClient path is empty. Please assign a TubeClient node.")
	elif not tree.is_ancestor_of(tube):
		warnings.append("`%s` node is not a child of `%s`.\
		" % [tube.name, tree.name])

	return warnings
