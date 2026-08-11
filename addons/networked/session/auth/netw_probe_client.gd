## One-shot client for the same-port [code]NPRB[/code] probe protocol.
class_name NetwProbeClient
extends RefCounted

const AuthProtocol := preload("res://addons/networked/session/auth/auth_protocol.gd")

var _api: SceneMultiplayer
var _peer: MultiplayerPeer
var _result: NetwProbeResult
var _start_ms := 0


## Queries through [param peer] without admitting it to the server roster.
func query(
		peer: MultiplayerPeer,
		timeout: float = 2.0,
		label: String = "server",
) -> NetwProbeResult:
	var loop := Engine.get_main_loop() as SceneTree
	if loop == null:
		return NetwProbeResult.error("no SceneTree available")
	if peer == null:
		return NetwProbeResult.unreachable("transport produced no probe peer")
	_peer = peer
	_result = null
	_start_ms = Time.get_ticks_msec()
	_api = SceneMultiplayer.new()
	_api.multiplayer_peer = peer
	_api.peer_authenticating.connect(_on_authenticating)
	_api.connection_failed.connect(_on_connection_failed)
	_api.peer_authentication_failed.connect(_on_authentication_failed)
	_api.auth_callback = _on_auth_received

	var deadline := Time.get_ticks_msec() + int(timeout * 1000.0)
	while _result == null and Time.get_ticks_msec() < deadline:
		_api.poll()
		await loop.process_frame
	_cleanup()
	await loop.process_frame
	await loop.process_frame
	if _result == null:
		return NetwProbeResult.timeout(
			"probe_server_info(%s) expired after %.2fs" % [label, timeout],
		)
	return _result


func _on_authenticating(peer_id: int) -> void:
	if peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		_api.send_auth(peer_id, AuthProtocol.encode_probe_request())


func _on_auth_received(_peer_id: int, data: PackedByteArray) -> void:
	var decoded := AuthProtocol.decode_probe_reply(data)
	if not decoded.ok:
		_result = NetwProbeResult.error("malformed NPRB reply")
		return
	match int(decoded.status):
		AuthProtocol.ProbeStatus.OK:
			var info := NetwServerInfo.from_payload(decoded.payload)
			if info:
				_result = NetwProbeResult.ok(
					info,
					Time.get_ticks_msec() - _start_ms,
				)
			else:
				_result = NetwProbeResult.error("malformed NPRB info payload")
		AuthProtocol.ProbeStatus.BUSY:
			_result = NetwProbeResult.busy("server reported BUSY")
		AuthProtocol.ProbeStatus.UNSUPPORTED:
			_result = NetwProbeResult.unsupported()
		_:
			_result = NetwProbeResult.error("server reported an invalid status")


func _on_connection_failed() -> void:
	if _result == null:
		_result = NetwProbeResult.unreachable("connection failed")


func _on_authentication_failed(_peer_id: int) -> void:
	if _result == null:
		_result = NetwProbeResult.unreachable("peer authentication failed")


func _cleanup() -> void:
	if _api == null:
		return
	_api.auth_callback = Callable()
	if _peer:
		_peer.close()
	_api.multiplayer_peer = null
	_peer = null
	_api = null
