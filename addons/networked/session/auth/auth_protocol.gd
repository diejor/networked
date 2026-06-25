## Wire-format codec for Networked auth-phase packets.
##
## The first packet exchanged during [SceneMultiplayer]'s auth phase is
## framed with a 4-byte magic prefix identifying its purpose:
## [br][br]
## [code]"NHEL"[/code] (Networked Hello) - a normal client opening a
## session. Its header carries a 4-byte app tag right after the version, so a
## peer running a different game build ([member MultiplayerTree.app_id]) is
## rejected before the provider payload is even read. The provider payload (if
## any) is wrapped by this header so [NetwAuth] implementations see only
## their own bytes.
## [br][br]
## [code]"NPRB"[/code] (Networked Probe) - a transient browser/probe peer
## requesting server metadata. The server replies with an [code]NPRB[/code]
## packet and disconnects without completing auth, so probes never enter
## [code]get_peers()[/code].
## [br][br]
## Packets that match neither magic are treated as
## [constant Kind.UNKNOWN] and fail closed.
class_name AuthProtocol
extends RefCounted

## Current protocol version. Bumped when the framing changes in a way
## that older peers cannot decode.
const PROTOCOL_VERSION := 2

static var MAGIC_HELLO := PackedByteArray([0x4E, 0x48, 0x45, 0x4C]) # "NHEL"
static var MAGIC_PROBE := PackedByteArray([0x4E, 0x50, 0x52, 0x42]) # "NPRB"

# Hello carries a 4-byte app tag the probe does not need, since probes never
# join the session they query.
const _HELLO_HEADER_LEN := 10 # magic(4) + version(1) + app_tag(4) + flags(1)
const _PROBE_HEADER_LEN := 6 # magic(4) + version(1) + status-or-flags(1)

## Categorical outcome of [method classify] for a received auth packet.
enum Kind {
	UNKNOWN,
	HELLO,
	PROBE,
}

## Status byte values used in probe replies.
enum ProbeStatus {
	OK,
	BUSY,
	UNSUPPORTED,
	ERROR,
}


## Returns which [enum Kind] [param data] represents based on its 4-byte
## magic prefix. Length-short or unmagic-prefixed packets return
## [constant Kind.UNKNOWN].
static func classify(data: PackedByteArray) -> Kind:
	if data.size() < _PROBE_HEADER_LEN:
		return Kind.UNKNOWN
	if _matches_magic(data, MAGIC_HELLO):
		return Kind.HELLO
	if _matches_magic(data, MAGIC_PROBE):
		return Kind.PROBE
	return Kind.UNKNOWN


## Builds a client-hello packet wrapping [param provider_payload].
##
## [param app_tag] is the game-build tag from [member MultiplayerTree.app_id]
## (0 when no build gate is set). [param flags] is reserved for future use
## (default 0).
static func encode_client_hello(
		provider_payload: PackedByteArray,
		app_tag: int = 0,
		flags: int = 0,
) -> PackedByteArray:
	var buf := PackedByteArray()
	buf.append_array(MAGIC_HELLO)
	buf.append(PROTOCOL_VERSION)
	_append_u32(buf, app_tag)
	buf.append(flags & 0xFF)
	buf.append_array(provider_payload)
	return buf


## Decodes a client-hello packet, rejecting it when its app tag differs from
## [param local_app_tag]. Returns
## [code]{ ok, reason, version, app_tag, flags, provider_payload }[/code]. When
## [code]ok[/code] is [code]false[/code], [code]reason[/code] is one of
## [code]"framing"[/code], [code]"version"[/code], or [code]"app"[/code] and the
## remaining fields are zero / empty.
static func decode_client_hello(
		data: PackedByteArray,
		local_app_tag: int = 0,
) -> Dictionary:
	if not _matches_magic(data, MAGIC_HELLO) or data.size() < _HELLO_HEADER_LEN:
		return {
			ok = false,
			reason = "framing",
			version = 0,
			app_tag = 0,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	var version := int(data[4])
	var app_tag := _read_u32(data, 5)
	if version != PROTOCOL_VERSION:
		return {
			ok = false,
			reason = "version",
			version = version,
			app_tag = app_tag,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	if app_tag != (local_app_tag & 0xFFFFFFFF):
		return {
			ok = false,
			reason = "app",
			version = version,
			app_tag = app_tag,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	return {
		ok = true,
		reason = "",
		version = version,
		app_tag = app_tag,
		flags = int(data[9]),
		provider_payload = data.slice(_HELLO_HEADER_LEN, data.size()),
	}


## Builds a probe-request packet. [param flags] is reserved for future use.
static func encode_probe_request(flags: int = 0) -> PackedByteArray:
	var buf := PackedByteArray()
	buf.append_array(MAGIC_PROBE)
	buf.append(PROTOCOL_VERSION)
	buf.append(flags & 0xFF)
	return buf


## Decodes a probe-request packet. Returns
## [code]{ ok, version, flags }[/code].
static func decode_probe_request(data: PackedByteArray) -> Dictionary:
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _PROBE_HEADER_LEN:
		return { ok = false, version = 0, flags = 0 }
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return { ok = false, version = version, flags = 0 }
	return { ok = true, version = version, flags = int(data[5]) }


## Builds a probe-reply packet carrying [param payload] (typically a
## [code]var_to_bytes[/code] encoding of [Info]'s dictionary).
##
## [param status] is one of [enum ProbeStatus].
static func encode_probe_reply(
		status: int,
		payload: PackedByteArray = PackedByteArray(),
) -> PackedByteArray:
	var buf := PackedByteArray()
	buf.append_array(MAGIC_PROBE)
	buf.append(PROTOCOL_VERSION)
	buf.append(status & 0xFF)
	buf.append_array(payload)
	return buf


## Decodes a probe-reply packet. Returns
## [code]{ ok, version, status, payload }[/code].
static func decode_probe_reply(data: PackedByteArray) -> Dictionary:
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _PROBE_HEADER_LEN:
		return {
			ok = false,
			version = 0,
			status = 0,
			payload = PackedByteArray(),
		}
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return {
			ok = false,
			version = version,
			status = 0,
			payload = PackedByteArray(),
		}
	return {
		ok = true,
		version = version,
		status = int(data[5]),
		payload = data.slice(_PROBE_HEADER_LEN, data.size()),
	}


static func _matches_magic(
		data: PackedByteArray,
		magic: PackedByteArray,
) -> bool:
	if data.size() < magic.size():
		return false
	for i in magic.size():
		if data[i] != magic[i]:
			return false
	return true


# Appends value as 4 little-endian bytes.
static func _append_u32(buf: PackedByteArray, value: int) -> void:
	buf.append(value & 0xFF)
	buf.append((value >> 8) & 0xFF)
	buf.append((value >> 16) & 0xFF)
	buf.append((value >> 24) & 0xFF)


# Reads 4 little-endian bytes at offset into an unsigned 32-bit int.
static func _read_u32(data: PackedByteArray, offset: int) -> int:
	return int(data[offset]) \
			| (int(data[offset + 1]) << 8) \
			| (int(data[offset + 2]) << 16) \
			| (int(data[offset + 3]) << 24)


## Client side of the same-port [code]NPRB[/code] server probe.
##
## Direct, brokerless transports (ENet, WebSocket) discover live servers by
## riding [SceneMultiplayer]'s auth phase: a transient api connects with the
## backend's own client peer, sends an [code]NPRB[/code] request (see
## [AuthProtocol]) instead of a hello, and decodes the [ServerDescriptor.Info] reply
## without ever entering the server's [method MultiplayerAPI.get_peers]. 
## The server answers via [AuthProtocol.Responder].
class Client:
	extends RefCounted

	var _backend: BackendPeer
	var _address := ""
	var _api: SceneMultiplayer
	var _peer: MultiplayerPeer
	var _result: BackendPeer.ProbeResult
	var _start_ms := 0


	func _init(backend: BackendPeer = null) -> void:
		_backend = backend


	## Probes [param address] using the configured backend's client peer and
	## returns the decoded [BackendPeer.ProbeResult]. [param timeout] is the maximum
	## total time to wait for a reply.
	##
	## The transient peer is closed before returning, so the probe never joins the
	## session it queried. Create a fresh [AuthProtocol.Client] per query.
	func query(address: String, timeout: float = 2.0) -> BackendPeer.ProbeResult:
		_address = address
		_result = null
		_start_ms = Time.get_ticks_msec()

		var loop := Engine.get_main_loop() as SceneTree
		if loop == null:
			return BackendPeer.ProbeResult.error("no SceneTree available")

		if _backend == null:
			return BackendPeer.ProbeResult.error("no backend available")

		_api = SceneMultiplayer.new()
		_peer = await _backend.create_join_peer(null, address, "")
		if _peer == null:
			return BackendPeer.ProbeResult.unreachable(
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
			return BackendPeer.ProbeResult.timeout(
				"probe_server_info(%s) expired after %.2fs" % [
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
			_result = BackendPeer.ProbeResult.error("malformed NPRB reply")
			return
		var status: int = decoded.status
		match status:
			AuthProtocol.ProbeStatus.OK:
				_handle_ok_reply(decoded.payload)
			AuthProtocol.ProbeStatus.BUSY:
				_result = BackendPeer.ProbeResult.busy("server reported BUSY")
			AuthProtocol.ProbeStatus.UNSUPPORTED:
				_result = BackendPeer.ProbeResult.unsupported()
			_:
				_result = BackendPeer.ProbeResult.error(
					"server reported status %d" % status,
				)


	# Converts an OK probe payload into ProbeResult.
	func _handle_ok_reply(payload: PackedByteArray) -> void:
		# OK replies carry Info; empty packets are request-shaped.
		if payload.is_empty():
			return
		var info := ServerDescriptor.Info.from_payload(payload)
		if info == null:
			_result = BackendPeer.ProbeResult.error("malformed NPRB info payload")
			return
		var latency := Time.get_ticks_msec() - _start_ms
		_result = BackendPeer.ProbeResult.ok(info, latency)


	# Records a transport-level connection failure.
	func _on_connection_failed() -> void:
		if _result == null:
			_result = BackendPeer.ProbeResult.unreachable("connection failed")


	# Records an auth failure before a probe reply completes.
	func _on_authentication_failed(_peer_id: int) -> void:
		if _result == null:
			_result = BackendPeer.ProbeResult.unreachable(
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


## Server side of the same-port [code]NPRB[/code] probe.
##
## [AuthCoordinator] dispatches [code]NPRB[/code] auth packets here while it
## handles [code]NHEL[/code] hellos itself. This keeps the coordinator about
## authentication while the probe, its rate limit, active-probe cap, and
## [ServerDescriptor.Info] reply — lives as one cohesive unit on both ends (see
## [AuthProtocol.Client] for the client half).
class Responder:
	extends RefCounted

	## Maximum probe replies per second before further probes are answered
	## with [constant AuthProtocol.ProbeStatus.BUSY].
	const PROBE_RATE_LIMIT := 10

	## Upper bound on concurrent pending probes. Bounds the
	## [member _probe_peer_ids] dictionary, and excess probes get BUSY. Pairs with
	## [SceneMultiplayer]'s [code]auth_timeout[/code], which reaps probe peers
	## the client never closed.
	const MAX_ACTIVE_PROBES := 32

	var _api: SceneMultiplayer
	var _tree: MultiplayerTree
	var _server_info_source: ServerDescriptor
	var _probe_timestamps_ms: Array[int] = []
	var _probe_peer_ids: Dictionary[int, bool] = { }


	## Sets the api used to send probe replies. Pass [code]null[/code] to unbind.
	func bind_api(api: SceneMultiplayer) -> void:
		_api = api


	## Stores the owning tree so probe replies can build a [ServerDescriptor.Info] from
	## live session state.
	func set_tree(tree: MultiplayerTree) -> void:
		_tree = tree


	## Sets the [ServerDescriptor] used to build probe replies. When
	## [code]null[/code], a [DefaultServerDescriptor] is created on first use.
	func set_server_info_source(source: ServerDescriptor) -> void:
		_server_info_source = source


	## Handles a probe request. Builds a [ServerDescriptor.Info] via the configured
	## [ServerDescriptor], encodes it into an NPRB reply, and lets the client
	## close. SceneMultiplayer's auth_timeout reaps stragglers. Excess probes
	## (rate or concurrency cap) are answered with BUSY.
	func handle(peer_id: int) -> void:
		if not _api:
			return
		Netw.dbg.debug("Auth: peer %d probe request received", [peer_id])
		_probe_peer_ids[peer_id] = true

		if _is_rate_limited() or _probe_peer_ids.size() > MAX_ACTIVE_PROBES:
			Netw.dbg.debug(
				"Auth: peer %d probe deferred (busy)",
				[peer_id],
			)
			var busy := AuthProtocol.encode_probe_reply(
				AuthProtocol.ProbeStatus.BUSY,
			)
			_api.send_auth(peer_id, busy)
			return

		var source := _server_info_source
		if source == null:
			source = DefaultServerDescriptor.new()

		var info := source.build_server_info(_tree)
		var payload := ServerDescriptor.Info.to_payload(info)
		var reply := AuthProtocol.encode_probe_reply(
			AuthProtocol.ProbeStatus.OK,
			payload,
		)
		_api.send_auth(peer_id, reply)


	## Releases a probe peer when its auth fails. Returns [code]true[/code] if
	## [param peer_id] was a tracked probe (the caller should then skip the
	## "Auth failed" warning), [code]false[/code] otherwise.
	func note_auth_failed(peer_id: int) -> bool:
		if _probe_peer_ids.erase(peer_id):
			# Expected for probes: client closed its peer after receiving the
			# reply, or [code]auth_timeout[/code] reaped a straggler.
			Netw.dbg.debug("Auth: probe peer %d released", [peer_id])
			return true
		return false


	## Clears tracked probe state.
	func clear() -> void:
		_probe_timestamps_ms.clear()
		_probe_peer_ids.clear()


	# Records the current probe timestamp and returns whether the latest one
	# exceeded the per-second cap. Keeps the ring trimmed to the window.
	func _is_rate_limited() -> bool:
		var now_ms := Time.get_ticks_msec()
		var window_start_ms := now_ms - 1000
		while _probe_timestamps_ms.size() > 0 \
				and _probe_timestamps_ms[0] < window_start_ms:
			_probe_timestamps_ms.pop_front()
		_probe_timestamps_ms.push_back(now_ms)
		return _probe_timestamps_ms.size() > PROBE_RATE_LIMIT
