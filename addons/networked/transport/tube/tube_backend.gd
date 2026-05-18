## [BackendPeer] implementation that delegates transport to a duck-typed
## TubeClient node.
##
## Assign a [code]NodePath[/code] to a TubeClient node in the scene. Call
## [method MultiplayerTree.host] or [method MultiplayerTree.join] as normal -
## the TubeClient owns its own [MultiplayerAPI], so this backend asks the tree
## to adopt it via [method MultiplayerTree._adopt_api].
## [br][br]
## [b]Service Registration:[/b]
## During [method setup], this backend registers the [code]TubeClient[/code]
## node as a session-wide service.
## [codeblock]
## # Retrieve the TubeClient from a component
## var tube = ctx.services.get_service(TubeClient)
## if tube:
##     print("Session ID: ", tube.session_id)
## [/codeblock]
@tool
class_name TubeBackend
extends BackendPeer

## Scene-relative path to the TubeClient node that manages the Tube transport.
@export_node_path("Node") var tube_client_path: NodePath

var tube: TubeWrapper

## Resolves the TubeClient node at [param tree] and wires it into the backend.
##
## Returns [code]ERR_UNCONFIGURED[/code] if [member tube_client_path] is empty,
## or [code]ERR_INVALID_DATA[/code] if the node is not a valid TubeClient.
func setup(tree: MultiplayerTree) -> Error:
	Netw.dbg.trace("TubeBackend: setup called.")
	if tube_client_path.is_empty():
		Netw.dbg.error("TubeBackend: TubeClient path is empty.", func(m): push_error(m))
		return ERR_UNCONFIGURED

	var node = tree.get_node_or_null(tube_client_path)
	tube = TubeWrapper.new(node)

	if not tube.is_valid():
		Netw.dbg.error("TubeBackend: Assigned node is not a valid TubeClient.", func(m): push_error(m))
		tube = null
		return ERR_INVALID_DATA

	NetwServices.register(node)
	tube.multiplayer_root_node = tree
	tree._adopt_api(tube.multiplayer_api, "tube_swap")
	Netw.dbg.warn(
		"TubeBackend swaps MultiplayerTree.api during setup. Avoid caching "
		+ "Node.multiplayer before setup completes."
	)

	return OK


## Creates a new Tube session and copies the session ID to the clipboard.
## Returns [code]null[/code] because the Tube transport owns its own peer; the
## tree continues to drive its api directly.
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer:
	Netw.dbg.trace("TubeBackend: create_host_peer called.")
	assert(tube != null, "Backend needs to `setup()` first.")

	tube.create_session()
	Netw.dbg.debug("Tube state after create_session: %d", [tube.state])

	if tube.state == TubeWrapper.State.CREATING_SESSION \
			or tube.state == TubeWrapper.State.SESSION_CREATED:
		Netw.dbg.info(
			"Tube session ready at `%s` (saved to clipboard). ",
			[tube.session_id]
		)
		DisplayServer.clipboard_set(tube.session_id)

	return null

## Joins the Tube session identified by [param server_address]. Returns
## [code]null[/code]; the TubeClient already configured its peer onto the
## adopted api.
func create_join_peer(
	_tree: MultiplayerTree, server_address: String, _username: String = ""
) -> MultiplayerPeer:
	Netw.dbg.trace("TubeBackend: create_join_peer called at %s", [server_address])
	assert(tube != null, "Backend needs to `setup()` first.")

	tube.join_session(server_address)
	Netw.dbg.debug("Tube state after join_session: %d", [tube.state])

	return null

func peer_reset_state() -> void:
	if tube != null:
		if tube.state != TubeWrapper.State.IDLE:
			tube.leave_session()
		NetwServices.unregister(tube._node)

func get_join_address() -> String:

	if tube != null and not tube.session_id.is_empty():
		return tube.session_id

	return super.get_join_address()

## Returns [code]false[/code] because Tube joins use session IDs, not
## [code]"localhost"[/code]. Tube still supports embedded duplicate-host
## startup through [method supports_embedded_server].
func supports_local_probe() -> bool:
	return false

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

	var multiplayer_api: SceneMultiplayer:
		get: return _node.multiplayer_api as SceneMultiplayer

	func is_valid() -> bool:
		return _node != null and _node.has_method("create_session") and "multiplayer_root_node" in _node

	func create_session() -> void:
		_node.create_session()

	func join_session(address: String) -> void:
		_node.join_session(address)

	func leave_session() -> void:
		_node.leave_session()
