## Abstract base resource for network transports used by [MultiplayerTree].
##
## Subclass this to implement a new transport (ENet, WebSocket, WebRTC, etc.).
## Override [method create_host_peer] and [method create_join_peer] to produce
## a [MultiplayerPeer]; the [MultiplayerTree] owns the [SceneMultiplayer] and
## assigns the returned peer onto it.
@tool
@abstract
class_name BackendPeer
extends Resource



@export_group("Lag Simulation")
## If true, simulates network latency and packet loss using [LaggyMultiplayerPeer].
@export var simulate_lag: bool = false
## Minimum packet delay (latency) in seconds.
@export_range(0.0, 1.0, 0.001, "or_greater", "suffix:s") var lag_min_delay: float = 0.1
## Maximum packet delay (latency) in seconds.
@export_range(0.0, 1.0, 0.001, "or_greater", "suffix:s") var lag_max_delay: float = 0.1
## Packet loss ratio (0.0 to 1.0).
@export_range(0.0, 1.0, 0.01) var lag_packet_loss: float = 0.0


## Automatically decorates the [param base_peer] with [code]LaggyMultiplayerPeer[/code] if enabled.
##
## This uses dynamic reflection to avoid direct script dependencies on the GDExtension, 
## meaning it is safe to use in projects that don't have the GDExtension loaded.
func wrap_peer(base_peer: MultiplayerPeer) -> MultiplayerPeer:
	if not base_peer:
		return null
	if not simulate_lag:
		return base_peer
	
	if not ClassDB.class_exists(&"LaggyMultiplayerPeer"):
		Netw.dbg.warn(
			"Lag simulation is enabled but GDExtension 'LaggyMultiplayerPeer' is not present in ClassDB.",
			func(m): push_warning(m)
		)
		return base_peer
	
	Netw.dbg.info("Wrapping peer in LaggyMultiplayerPeer (delay: %.1f-%.1f ms, packet loss: %d%%)", [
		lag_min_delay * 1000.0,
		lag_max_delay * 1000.0,
		int(lag_packet_loss * 100.0)
	])
	
	var laggy_instance: Object = ClassDB.instantiate(&"LaggyMultiplayerPeer")
	var wrapped_peer: MultiplayerPeer = laggy_instance.call(&"create", base_peer)
	if wrapped_peer:
		wrapped_peer.set(&"delay_minimum", lag_min_delay)
		wrapped_peer.set(&"delay_maximum", lag_max_delay)
		wrapped_peer.set(&"packet_loss", lag_packet_loss)
		return wrapped_peer
	
	return base_peer


## Optional one-time setup hook called by [MultiplayerTree] before
## [method create_host_peer] or [method create_join_peer]. Use it to resolve
## scene-relative nodes or external services. Return [code]OK[/code] on success.
func setup(_tree: MultiplayerTree) -> Error:
	return OK


## Produces a [MultiplayerPeer] in server mode. May [code]await[/code].
##
## Return [code]null[/code] to signal failure; the tree will treat this as
## [code]ERR_CANT_CREATE[/code]. The tree assigns the returned peer onto its
## owned [SceneMultiplayer].
@abstract
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer


## Produces a [MultiplayerPeer] in client mode connecting to [param _address].
## May [code]await[/code]. Return [code]null[/code] to signal failure.
@abstract
func create_join_peer(
	_tree: MultiplayerTree, _address: String, _username: String = ""
) -> MultiplayerPeer


## Returns editor configuration warnings specific to this backend for the
## given [param _tree].
@abstract
func get_backend_warnings(_tree: MultiplayerTree) -> PackedStringArray


## Per-frame poll hook for backends that drive their own internal state
## (e.g. WebRTC signaling sockets, in-process loopback queues).
##
## The tree polls the owned [SceneMultiplayer] separately - do not poll the
## api here.
func poll(_dt: float) -> void:
	pass


## Closes and clears any backend-side state. Called by [MultiplayerTree] before
## opening a new session and on teardown. Override to release transport-specific
## handles (e.g. tracker sockets, lobby memberships).
func peer_reset_state() -> void:
	pass


## Returns the address clients should use to join a hosted session.
##
## Override in subclasses that use dynamic addresses (e.g. room codes). Defaults to [code]"localhost"[/code].
func get_join_address() -> String:
	return "localhost"


## Returns [code]true[/code] if this backend supports spinning up an embedded
## server on a local machine (e.g. ENet, WebSocket).
##
## Return [code]false[/code] for backends that rely on external lobby systems
## (e.g. Steam).
func supports_embedded_server() -> bool:
	return true


## Queries [param address] for live [ServerInfo] without allocating a
## persistent peer or entering the server's [code]get_peers()[/code].
##
## The default implementation opens a transient [SceneMultiplayer], asks the
## backend for a client peer via [method create_join_peer], sends an
## [code]NPRB[/code] auth packet (see [AuthProtocol]), and decodes the reply
## into a [ServerInfo]. Backends that cannot run a SceneMultiplayer auth
## handshake (session-id transports like Steam, in-process Local) override
## this to return [method ServerInfoResult.unsupported].
## [br][br]
## [param timeout] is the maximum total time to wait for a reply.
func query_server_info(
	address: String, timeout: float = 2.0,
) -> ServerInfoResult:
	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return ServerInfoResult.error("no SceneTree available")

	var transient_api := SceneMultiplayer.new()
	var peer: MultiplayerPeer = await create_join_peer(null, address, "")
	if peer == null:
		return ServerInfoResult.unreachable(
			"backend produced no peer for %s" % address
		)

	transient_api.multiplayer_peer = peer
	var start_ms := Time.get_ticks_msec()
	var state := { result = null }

	var on_authenticating := func(peer_id: int) -> void:
		if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
			return
		var req := AuthProtocol.encode_probe_request()
		transient_api.send_auth(peer_id, req)

	var on_auth_received := func(_peer_id: int, data: PackedByteArray) -> void:
		var decoded := AuthProtocol.decode_probe_reply(data)
		if not decoded.ok:
			state.result = ServerInfoResult.error("malformed NPRB reply")
			return
		var status: int = decoded.status
		match status:
			AuthProtocol.ProbeStatus.OK:
				var info := ServerInfo.from_payload(decoded.payload)
				var latency := Time.get_ticks_msec() - start_ms
				state.result = ServerInfoResult.ok(info, latency)
			AuthProtocol.ProbeStatus.BUSY:
				state.result = ServerInfoResult.busy(
					"server reported BUSY"
				)
			AuthProtocol.ProbeStatus.UNSUPPORTED:
				state.result = ServerInfoResult.unsupported()
			_:
				state.result = ServerInfoResult.error(
					"server reported status %d" % status
				)

	var on_connection_failed := func() -> void:
		if state.result == null:
			state.result = ServerInfoResult.unreachable(
				"connection failed"
			)

	var on_authentication_failed := func(_peer_id: int) -> void:
		if state.result == null:
			state.result = ServerInfoResult.unreachable(
				"peer authentication failed"
			)

	transient_api.peer_authenticating.connect(on_authenticating)
	transient_api.connection_failed.connect(on_connection_failed)
	transient_api.peer_authentication_failed.connect(
		on_authentication_failed
	)
	transient_api.auth_callback = on_auth_received

	var deadline_ms := Time.get_ticks_msec() + int(timeout * 1000.0)
	while state.result == null and Time.get_ticks_msec() < deadline_ms:
		transient_api.poll()
		await loop.process_frame

	transient_api.auth_callback = Callable()
	if transient_api.peer_authenticating.is_connected(on_authenticating):
		transient_api.peer_authenticating.disconnect(on_authenticating)
	if transient_api.connection_failed.is_connected(on_connection_failed):
		transient_api.connection_failed.disconnect(on_connection_failed)
	if transient_api.peer_authentication_failed.is_connected(
		on_authentication_failed
	):
		transient_api.peer_authentication_failed.disconnect(
			on_authentication_failed
		)
	if peer:
		peer.close()
	transient_api.multiplayer_peer = null

	if state.result == null:
		return ServerInfoResult.timeout(
			"query_server_info(%s) expired after %.2fs" % [address, timeout]
		)
	return state.result


## Returns UI metadata describing the address string this backend expects.
##
## Used by generic connect dialogs to render appropriate labels,
## placeholders, and probe affordances. Default returns a generic
## [AddressHint].
func get_address_hint() -> AddressHint:
	var hint := AddressHint.new()
	hint.label = "Address"
	hint.accepts_empty = true
	return hint


## Called after this backend is duplicated by [MultiplayerTree]'s backend setter.
##
## Override to preserve shared references that [method Resource.duplicate] would reset
## to their default values.
func copy_from(_source: BackendPeer) -> void:
	pass
