@tool
class_name TubeBackend
extends BackendPeer

@export_node_path("Node") var tube_client_path: NodePath

var tube: TubeWrapper

func setup(tree: MultiplayerTree) -> Error:
	NetLog.trace("TubeBackend: setup called.")
	if tube_client_path.is_empty():
		NetLog.error("TubeBackend: TubeClient path is empty.")
		return ERR_UNCONFIGURED
		
	var node = tree.get_node_or_null(tube_client_path)
	tube = TubeWrapper.new(node)
	
	if not tube.is_valid():
		NetLog.error("TubeBackend: Assigned node is not a valid TubeClient.")
		tube = null
		return ERR_INVALID_DATA
	
	tube.multiplayer_root_node = tree
	tree._disconnect_backend_signals()
	api = tube.multiplayer_api
	tree._connect_backend_signals()
	
	return OK

func host() -> Error:
	NetLog.trace("TubeBackend: host called.")
	assert(tube != null, "Backend needs to `setup()` first.")
		
	tube.create_session()
	NetLog.debug("Tube state after create_session: %d" % tube.state)
	
	if tube.state == TubeWrapper.State.CREATING_SESSION or tube.state == TubeWrapper.State.SESSION_CREATED:
		NetLog.info("Tube session ready at `%s` (saved to clipboard). " % tube.session_id)
		DisplayServer.clipboard_set(tube.session_id)
		return OK
		
	NetLog.error("Tube failed to create session. State: %d" % tube.state)
	return ERR_CANT_CREATE

func join(server_address: String, _username: String = "") -> Error:
	NetLog.trace("TubeBackend: join called at %s" % server_address)
	assert(tube != null, "Backend needs to `setup()` first.")
		
	tube.join_session(server_address)
	NetLog.debug("Tube state after join_session: %d" % tube.state)
	
	if tube.state == TubeWrapper.State.JOINING_SESSION or tube.state == TubeWrapper.State.SESSION_JOINED:
		return OK
		
	NetLog.error("Tube failed to join session. State: %d" % tube.state)
	return ERR_CANT_CONNECT

func peer_reset_state() -> void:
	if tube != null and tube.state != TubeWrapper.State.IDLE:
		tube.leave_session()
	super.peer_reset_state()

func get_join_address() -> String:
	if tube != null and not tube.session_id.is_empty():
		return tube.session_id
		
	return super.get_join_address()

func _get_backend_warnings(tree: MultiplayerTree) -> PackedStringArray:
	var warnings := PackedStringArray()
	
	if tube_client_path.is_empty():
		warnings.append("TubeClient path is empty. Please assign a TubeClient node.")
		return warnings
		
	var node = tree.get_node_or_null(tube_client_path)
	if node == null:
		return warnings
		
	var wrapper = TubeWrapper.new(node)
	if not wrapper.is_valid():
		warnings.append("Node assigned is not a valid TubeClient.")
		
	if not tree.is_ancestor_of(node):
		warnings.append("`%s` node is not a child of `%s`." % [node.name, tree.name])

	return warnings


class TubeWrapper:
	enum State { IDLE, CREATING_SESSION, SESSION_CREATED, JOINING_SESSION, SESSION_JOINED }
	
	var _node: Variant 
	
	func _init(target_node: Node) -> void:
		_node = target_node
		
	var state: int:
		get: return _node.state
		
	var session_id: String:
		get: return _node.session_id
		
	var multiplayer_root_node: Node:
		get: return _node.multiplayer_root_node
		set(value): _node.multiplayer_root_node = value
			
	var multiplayer_api: MultiplayerAPI:
		get: return _node.multiplayer_api
		
	func is_valid() -> bool:
		return _node != null and _node.has_method("create_session") and "multiplayer_root_node" in _node
		
	func create_session() -> void:
		_node.create_session()
		
	func join_session(address: String) -> void:
		_node.join_session(address)
		
	func leave_session() -> void:
		_node.leave_session()
