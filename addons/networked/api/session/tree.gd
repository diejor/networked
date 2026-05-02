## Thin facade over [MultiplayerTree] exposing only session-level APIs.
##
## Components obtain this via [member NetwContext.tree] rather than
## holding a direct reference to [MultiplayerTree]. This keeps component code
## off the concrete [MultiplayerTree] class (backend, multiplayer_api, etc.)
## while preserving the existing session API unchanged.
## [br][br]
## Service access has been moved to [NetwServices]; use
## [member NetwContext.services] for [method get_scene_manager],
## [method get_clock], and related helpers.
## [br][br]
## Holds a [WeakRef] so components that cache an instance survive tree
## teardown without keeping the [MultiplayerTree] alive.
class_name NetwTree
extends RefCounted

## Emitted when the multiplayer API and scene manager have been configured.
signal configured()
## Emitted after a player's target scene has been activated.
signal player_scene_ready(client_data: MultiplayerClientData, netw_scene: NetwScene)
## Emitted on clients when the server notifies it is shutting down.
signal server_disconnecting(reason: String)
## Emitted on the server when a client requests to kick a peer.
signal kick_requested(requester_id: int, target_id: int, reason: String)
## Emitted on the kicked peer when the server kicks them.
signal kicked(reason: String)
## Emitted on every peer when the game is paused via [method pause].
signal tree_paused(reason: String)
## Emitted on every peer when the game is unpaused via [method unpause].
signal tree_unpaused()

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)
	mt.configured.connect(func(): configured.emit())
	mt.player_scene_ready.connect(_on_player_scene_ready)
	mt.server_disconnecting.connect(func(reason: String): server_disconnecting.emit(reason))
	mt.kick_requested.connect(func(requester, target, reason: String): kick_requested.emit(requester, target, reason))
	mt.kicked.connect(func(reason: String): kicked.emit(reason))
	mt.tree_paused.connect(func(reason: String): tree_paused.emit(reason))
	mt.tree_unpaused.connect(func(): tree_unpaused.emit())


## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return is_instance_valid(_tree_ref.get_ref())


## Returns an array of all active player nodes across all scenes or the
## sceneless world.
func get_all_players() -> Array[Node]:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_all_players() if mt else []


## Returns [code]true[/code] if the current session is hosting as a server.
func is_server() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.is_server if mt else false


## Returns the unique peer ID for this session.
func get_unique_id() -> int:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if mt and mt.multiplayer_api:
		return mt.multiplayer_api.get_unique_id()
	return 0


## Returns the original name of the [MultiplayerTree] node.
func get_tree_name() -> String:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_tree_name() if mt else ""


## Returns [code]true[/code] if the multiplayer peer is in an active connection.
func is_online() -> bool:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.is_online() if mt else false


## Returns the local player node for this tree, or [code]null[/code].
func get_authority_client() -> Node:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.authority_client if mt else null


## Resolves the correct spawn location and causal token for a new player.
func get_spawn_slot(spawner_path: SceneNodePath) -> SpawnSlot:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return SpawnSlot.new()
	return mt.get_spawn_slot(spawner_path)


## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## [b]Server-only.[/b] The pause is sent to each connected peer individually.
func pause(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.pause() must be called on the server.")
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_pause.rpc_id(peer_id, reason)
	mt._rpc_receive_pause(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [b]Server-only.[/b]
func unpause() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.unpause() must be called on the server.")
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_unpause.rpc_id(peer_id)
	mt._rpc_receive_unpause()


## Disconnects [param peer_id] from the session.
##
## [b]Server-only.[/b] If [param reason] is non-empty, the peer receives
## [signal kicked] before the connection is closed.
func kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.kick() must be called on the server.")
	if not reason.is_empty():
		mt._rpc_receive_kicked.rpc_id(peer_id, reason)
	if mt.multiplayer_peer:
		mt.multiplayer_peer.disconnect_peer(peer_id)


## Asks the server to kick [param peer_id].
##
## [b]Client-only.[/b]
func request_kick(peer_id: int, reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt._rpc_request_kick.rpc_id(1, peer_id, reason)


## Disconnects the local peer from the session.
##
## Saves all registered states for the local peer, then closes the
## multiplayer peer.
func disconnect_peer() -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt.disconnect_peer()


## Asks the server for permission to disconnect.
##
## [b]Client-only.[/b]
func request_disconnect(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt._rpc_request_disconnect.rpc_id(1, reason)


## Notifies all clients that the server is shutting down.
##
## [b]Server-only.[/b] Clients receive [signal server_disconnecting].
func notify_disconnect(reason: String = "") -> void:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	assert(mt.is_server, "NetwTree.notify_disconnect() must be called on the server.")
	for peer_id: int in mt.multiplayer_api.get_peers():
		mt._rpc_receive_notify_disconnect.rpc_id(peer_id, reason)


func _on_player_scene_ready(
	client_data: MultiplayerClientData, scene: MultiplayerScene
) -> void:
	var netw_scene := NetwScene.new(scene) if is_instance_valid(scene) else null
	player_scene_ready.emit(client_data, netw_scene)
	if is_instance_valid(scene):
		scene.player_ready.emit(client_data)
