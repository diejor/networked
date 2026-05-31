## [BackendPeer] implementation that delegates transport to a duck-typed
## TubeClient node.
##
## Add exactly one [code]TubeClient[/code] descendant under the
## [MultiplayerTree]. Call [method MultiplayerTree.host] or
## [method MultiplayerTree.join] as normal. The TubeClient owns its own
## [MultiplayerAPI], which this backend installs onto the tree during setup.
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

var tube: TubeWrapper

## Finds the [code]TubeClient[/code] under [param tree] and wires it into the
## backend.
##
## Returns [code]ERR_UNCONFIGURED[/code] if no [code]TubeClient[/code] exists,
## or [code]ERR_INVALID_DATA[/code] if the match is ambiguous or invalid.
func setup(tree: MultiplayerTree) -> Error:
	Netw.dbg.trace("TubeBackend: setup called.")
	var matches := tree.find_children("*", "TubeClient", true, false)
	if matches.is_empty():
		Netw.dbg.error(
			"TubeBackend: no TubeClient descendant found.",
			func(m): push_error(m)
		)
		return ERR_UNCONFIGURED

	if matches.size() > 1:
		Netw.dbg.error(
			"TubeBackend: expected one TubeClient descendant, found %d.",
			[matches.size()],
			func(m): push_error(m)
		)
		return ERR_INVALID_DATA

	var node := matches[0] as Node
	tube = TubeWrapper.new(node)

	if not tube.is_valid():
		Netw.dbg.error(
			"TubeBackend: found node is not a valid TubeClient.",
			func(m): push_error(m)
		)
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

## Tube routes through session IDs rather than a reachable address, so the
## SceneMultiplayer-based probe handshake does not apply.
func query_server_info(
	_address: String, _timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


func get_address_hint() -> AddressHint:
	return AddressHint.make(
		"Session ID",
		"",
		"Tube session identifier copied from a host. Leave empty to create "
		+ "a new session.",
		true,
		false
	)

## Returns the user-facing friendly name for this backend.
func get_display_name() -> String:
	return "Tube"


class TubeWrapper:
	enum State {
		IDLE,
		CREATING_SESSION,
		SESSION_CREATED,
		JOINING_SESSION,
		SESSION_JOINED,
	}

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
		return _node != null \
			and _node.has_method("create_session") \
			and "multiplayer_root_node" in _node

	func create_session() -> void:
		_node.create_session()

	func join_session(address: String) -> void:
		_node.join_session(address)

	func leave_session() -> void:
		_node.leave_session()
