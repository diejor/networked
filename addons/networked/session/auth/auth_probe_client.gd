## Client side of the same-port [code]NPRB[/code] server probe.
##
## Direct, brokerless transports (ENet, WebSocket) discover live servers by
## riding [SceneMultiplayer]'s auth phase: a transient api connects with the
## backend's own client peer, sends an [code]NPRB[/code] request (see
## [AuthProtocol]) instead of a hello, and decodes the [ServerInfo] reply
## without ever entering the server's [method MultiplayerAPI.get_peers]. 
## The server answers via [AuthProbeResponder].
##
## This is [b]not[/b] a universal probe: it only fits cheap direct
## [SceneMultiplayer] transports. Brokered transports (Steam lobbies, WebRTC
## trackers) discover through their own mechanisms and override
## [method BackendPeer.query_server_info] with their own logic or
## [method ServerInfoResult.unsupported].
@tool
class_name AuthProbeClient
extends RefCounted

var _backend: BackendPeer
var _address := ""
var _api: SceneMultiplayer
var _peer: MultiplayerPeer
var _result: ServerInfoResult
var _start_ms := 0


func _init(backend: BackendPeer = null) -> void:
	_backend = backend


## Probes [param address] using the configured backend's client peer and
## returns the decoded [ServerInfoResult]. [param timeout] is the maximum
## total time to wait for a reply.
##
## The transient peer is closed before returning, so the probe never joins the
## session it queried. Create a fresh [AuthProbeClient] per query.
func query(address: String, timeout: float = 2.0) -> ServerInfoResult:
	_address = address
	_result = null
	_start_ms = Time.get_ticks_msec()

	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return ServerInfoResult.error("no SceneTree available")

	if _backend == null:
		return ServerInfoResult.error("no backend available")

	_api = SceneMultiplayer.new()
	_peer = await _backend.create_join_peer(null, address, "")
	if _peer == null:
		return ServerInfoResult.unreachable(
			"backend produced no peer for %s" % address,
		)

	_api.multiplayer_peer = _peer
	_bind_signals()

	var deadline_ms := Time.get_ticks_msec() + int(timeout * 1000.0)
	while _result == null and Time.get_ticks_msec() < deadline_ms:
		_api.poll()
		await loop.process_frame

	_cleanup()
	await loop.process_frame
	await loop.process_frame

	if _result == null:
		return ServerInfoResult.timeout(
			"query_server_info(%s) expired after %.2fs" % [
				_address,
				timeout,
			],
		)
	return _result


# Connects the transient api to this one-shot probe client.
func _bind_signals() -> void:
	_api.peer_authenticating.connect(_on_authenticating)
	_api.connection_failed.connect(_on_connection_failed)
	_api.peer_authentication_failed.connect(_on_authentication_failed)
	_api.auth_callback = _on_auth_received


# Sends the NPRB request when the transient peer enters auth.
func _on_authenticating(peer_id: int) -> void:
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		return
	var req := AuthProtocol.encode_probe_request()
	_api.send_auth(peer_id, req)


# Decodes a probe reply from the server auth callback.
func _on_auth_received(_peer_id: int, data: PackedByteArray) -> void:
	var decoded := AuthProtocol.decode_probe_reply(data)
	if not decoded.ok:
		_result = ServerInfoResult.error("malformed NPRB reply")
		return
	var status: int = decoded.status
	match status:
		AuthProtocol.ProbeStatus.OK:
			_handle_ok_reply(decoded.payload)
		AuthProtocol.ProbeStatus.BUSY:
			_result = ServerInfoResult.busy("server reported BUSY")
		AuthProtocol.ProbeStatus.UNSUPPORTED:
			_result = ServerInfoResult.unsupported()
		_:
			_result = ServerInfoResult.error(
				"server reported status %d" % status,
			)


# Converts an OK probe payload into ServerInfoResult.
func _handle_ok_reply(payload: PackedByteArray) -> void:
	# OK replies carry ServerInfo; empty packets are request-shaped.
	if payload.is_empty():
		return
	var info := ServerInfo.from_payload(payload)
	if info == null:
		_result = ServerInfoResult.error("malformed NPRB info payload")
		return
	var latency := Time.get_ticks_msec() - _start_ms
	_result = ServerInfoResult.ok(info, latency)


# Records a transport-level connection failure.
func _on_connection_failed() -> void:
	if _result == null:
		_result = ServerInfoResult.unreachable("connection failed")


# Records an auth failure before a probe reply completes.
func _on_authentication_failed(_peer_id: int) -> void:
	if _result == null:
		_result = ServerInfoResult.unreachable(
			"peer authentication failed",
		)


# Disconnects signals and closes the transient peer.
func _cleanup() -> void:
	if _api == null:
		return
	_api.auth_callback = Callable()
	if _api.peer_authenticating.is_connected(_on_authenticating):
		_api.peer_authenticating.disconnect(_on_authenticating)
	if _api.connection_failed.is_connected(_on_connection_failed):
		_api.connection_failed.disconnect(_on_connection_failed)
	if _api.peer_authentication_failed.is_connected(
		_on_authentication_failed,
	):
		_api.peer_authentication_failed.disconnect(
			_on_authentication_failed,
		)
	if _peer:
		_peer.close()
	_api.multiplayer_peer = null
	_peer = null
	_api = null
