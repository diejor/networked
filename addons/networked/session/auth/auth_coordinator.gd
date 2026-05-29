class_name AuthCoordinator
extends RefCounted
## Internal coordinator for [MultiplayerTree] authentication hooks.
##
## Binds [SceneMultiplayer] auth callbacks to a [NetwAuthProvider] and stores
## accepted identities or rejection reasons in [SessionRoster]. Dispatches
## the always-on [code]NPRB[/code]/[code]NHEL[/code] auth protocol so server
## browsers can query metadata without entering [code]get_peers()[/code].
## [br][br]
## [b]auth_timeout dependency:[/b] probe peers are not closed by the server;
## the probing client owns the peer and closes it after receiving the reply.
## Stragglers (crashed or malicious probers) are reaped by
## [code]SceneMultiplayer.auth_timeout[/code] (default 3s). Setting
## [code]auth_timeout = 0[/code] disables this cleanup and lets probe slots
## accumulate up to [constant MAX_ACTIVE_PROBES]; do not do that on
## production hosts.

## Maximum probe replies per second before further probes are answered
## with [constant AuthProtocol.ProbeStatus.BUSY].
const PROBE_RATE_LIMIT := 10

## Upper bound on concurrent pending probes. Bounds the
## [member _probe_peer_ids] dictionary; excess probes get BUSY. Pairs with
## [SceneMultiplayer]'s [code]auth_timeout[/code], which reaps probe peers
## the client never closed.
const MAX_ACTIVE_PROBES := 32

var _api: SceneMultiplayer
var _auth_provider: NetwAuthProvider
var _roster: SessionRoster
var _client_join_payload: JoinPayload
var _tree: MultiplayerTree
var _server_info_source: ServerInfoSource
var _probe_timestamps_ms: Array[int] = []
var _probe_peer_ids: Dictionary[int, bool] = {}


func _init(roster = null) -> void:
	_roster = roster as SessionRoster


## Sets the [SessionRoster] used for peer auth state.
func set_roster(roster: SessionRoster) -> void:
	_roster = roster


## Rebinds auth hooks to [param api].
func bind_api(api: SceneMultiplayer) -> void:
	if _api == api:
		return
	
	_unbind_api()
	_api = api
	_connect_auth_signals()


## Sets the provider used by the auth handshake.
func set_auth_provider(provider: NetwAuthProvider) -> void:
	_auth_provider = provider


## Stores the join payload used to build client auth credentials.
func set_client_join_payload(payload: JoinPayload) -> void:
	_client_join_payload = payload


## Stores the owning tree so probe replies can build a [ServerInfo] from
## live session state.
func set_tree(tree: MultiplayerTree) -> void:
	_tree = tree


## Sets the [ServerInfoSource] used to build probe replies. When
## [code]null[/code], a [DefaultServerInfoSource] is created on first use.
func set_server_info_source(source: ServerInfoSource) -> void:
	_server_info_source = source


## Runs provider preparation before transport opens.
func prepare_join_payload(join_payload: JoinPayload) -> Error:
	if not _auth_provider:
		return OK
	
	Netw.dbg.info("Auth: running prepare for '%s'", [
		join_payload.username
	])
	var prepare_err := await _auth_provider.prepare(join_payload)
	if prepare_err != OK:
		Netw.dbg.error(
			"Auth prepare failed: %s",
			[error_string(prepare_err)],
 			func(m): push_error(m)
		)
		return prepare_err
	Netw.dbg.info("Auth: prepare succeeded for '%s'", [
		join_payload.username
	])
	return OK


## Installs the Networked auth dispatcher on the tree's SceneMultiplayer.
##
## The callback is installed unconditionally so the dispatcher can
## multiplex hello packets and probe requests; whether a
## [NetwAuthProvider] is configured only affects how HELLO bodies are
## validated.
func prepare() -> void:
	if not _api:
		return
	_api.auth_callback = _on_auth_received


## Clears the client-side auth callback after the connection handshake
## completes. Probe replies are handled exclusively by the transient
## [code]SceneMultiplayer[/code] owned by a probe session, so the in-game
## tree's callback is no longer needed once we are online.
func on_connected_to_server() -> void:
	if _api:
		_api.auth_callback = Callable()


## Stores the local host identity returned by the provider.
func synthesize_host_identity() -> void:
	if not _auth_provider:
		return
	Netw.dbg.info("Auth: synthesizing host identity for peer 1")
	var host_identity := _auth_provider.get_host_identity()
	if host_identity:
		Netw.dbg.info(
			"Auth: host identity '%s' (service=%s) stored for peer 1",
			[host_identity.username, host_identity.service]
		)
		_roster.get_peer_context(
			MultiplayerPeer.TARGET_PEER_SERVER
		).get_bucket(NetwIdentityBucket).identity = host_identity
		# Do not call complete_auth(1). The host was never in Godot's
		# pending auth queue, so complete_auth would corrupt auth state.
	else:
		Netw.dbg.debug("Auth: provider returned no host identity")


## Overrides [param join_payload]'s username with a server-authoritative
## identity if one exists for [param peer_id].
func resolve_identity(peer_id: int, join_payload: JoinPayload) -> void:
	if not _auth_provider or peer_id == MultiplayerPeer.TARGET_PEER_SERVER:
		return
	
	var bucket := _roster.get_peer_context(peer_id).get_bucket(
		NetwIdentityBucket
	)
	if bucket.identity:
		Netw.dbg.info(
			"Auth: overriding username '%s' with bucket identity '%s' "
			+ "(service=%s)",
			[join_payload.username, bucket.identity.username,
			bucket.identity.service]
		)
		join_payload.username = bucket.identity.username
	else:
		Netw.dbg.warn(
			"Auth: provider configured but no identity for peer %d; "
			+ "falling back to client-claimed username '%s'",
			[peer_id, join_payload.username]
		)


## Clears runtime auth state and disconnects API hooks.
func clear() -> void:
	bind_api(null)
	_auth_provider = null
	_client_join_payload = null
	_probe_timestamps_ms.clear()
	_probe_peer_ids.clear()


func _unbind_api() -> void:
	if not _api:
		return
	
	_api.auth_callback = Callable()
	if _api.peer_authenticating.is_connected(_on_peer_authenticating):
		_api.peer_authenticating.disconnect(_on_peer_authenticating)
	if _api.peer_authentication_failed.is_connected(
		_on_peer_authentication_failed
	):
		_api.peer_authentication_failed.disconnect(
			_on_peer_authentication_failed
		)
	_api = null


func _connect_auth_signals() -> void:
	if not _api:
		return
	if not _api.peer_authenticating.is_connected(_on_peer_authenticating):
		_api.peer_authenticating.connect(_on_peer_authenticating)
	if not _api.peer_authentication_failed.is_connected(
		_on_peer_authentication_failed
	):
		_api.peer_authentication_failed.connect(
			_on_peer_authentication_failed
		)


func _on_peer_authenticating(peer_id: int) -> void:
	if peer_id != MultiplayerPeer.TARGET_PEER_SERVER:
		return

	var provider_payload := PackedByteArray()
	if _auth_provider and _client_join_payload:
		provider_payload = _auth_provider.get_credentials(_client_join_payload)
		if provider_payload.is_empty():
			Netw.dbg.error(
				"Auth: provider returned empty credentials for peer %d",
				[peer_id]
			)
			_api.disconnect_peer(peer_id)
			return

	var hello := AuthProtocol.encode_client_hello(provider_payload)
	Netw.dbg.debug(
		"Auth: sending NHEL (%d provider bytes) for peer %d",
		[provider_payload.size(), peer_id]
	)

	var send_err := _api.send_auth(peer_id, hello)
	if send_err != OK:
		Netw.dbg.error(
			"Auth: failed to send NHEL to peer %d: %s",
			[peer_id, error_string(send_err)]
		)
		_api.disconnect_peer(peer_id)
		return

	var complete_err := _api.complete_auth(peer_id)
	if complete_err != OK:
		Netw.dbg.error(
			"Auth: failed to complete local auth for peer %d: %s",
			[peer_id, error_string(complete_err)]
		)
		_api.disconnect_peer(peer_id)


func _on_peer_authentication_failed(peer_id: int) -> void:
	if _probe_peer_ids.erase(peer_id):
		# Expected for probes: client closed its peer after receiving the
		# reply, or [code]auth_timeout[/code] reaped a straggler.
		Netw.dbg.debug("Auth: probe peer %d released", [peer_id])
		return
	Netw.dbg.warn("Auth failed for peer %d", [peer_id],
		func(m): push_warning(m))


func _on_auth_received(peer_id: int, data: PackedByteArray) -> void:
	match AuthProtocol.classify(data):
		AuthProtocol.Kind.HELLO:
			_handle_hello(peer_id, data)
		AuthProtocol.Kind.PROBE:
			_handle_probe(peer_id, data)
		_:
			Netw.dbg.warn(
				"Auth: peer %d sent unknown auth payload (%d bytes); "
				+ "fail-closed disconnect.",
				[peer_id, data.size()]
			)
			_api.disconnect_peer(peer_id)


func _handle_hello(peer_id: int, data: PackedByteArray) -> void:
	var decoded := AuthProtocol.decode_client_hello(data)
	if not decoded.ok:
		Netw.dbg.warn(
			"Auth: peer %d NHEL decode failed (version mismatch?); "
			+ "fail-closed disconnect.", [peer_id]
		)
		_api.disconnect_peer(peer_id)
		return

	var provider_payload: PackedByteArray = decoded.provider_payload

	if not _auth_provider:
		Netw.dbg.debug(
			"Auth: no provider, completing auth for peer %d", [peer_id]
		)
		_api.complete_auth(peer_id)
		return

	Netw.dbg.info("Auth: validating credentials for peer %d", [peer_id])
	var identity := _auth_provider.authenticate(peer_id, provider_payload)
	if identity:
		Netw.dbg.info(
			"Auth: peer %d accepted as '%s' (service=%s)",
			[peer_id, identity.username, identity.service]
		)
		_roster.get_peer_context(peer_id).get_bucket(
			NetwIdentityBucket
		).identity = identity
		_api.complete_auth(peer_id)
	else:
		var reason := (
			_auth_provider.rejection_reason
			if _auth_provider.rejection_reason
			else "Authentication failed"
		)
		_roster.set_auth_rejection_reason(peer_id, reason)
		Netw.dbg.warn(
			"Auth rejected for peer %d: %s", [peer_id, reason]
		)
		_api.disconnect_peer(peer_id)


# Handles a probe request. Builds a [ServerInfo] via the tree's configured
# [ServerInfoSource], encodes it into an NPRB reply, and lets the client
# close. SceneMultiplayer's auth_timeout reaps stragglers. Excess probes
# (rate or concurrency cap) are answered with BUSY.
#
# Probe peers are tracked in [member _probe_peer_ids] so the matching
# peer_authentication_failed signal (fired when the client closes or
# auth_timeout reaps) does not produce a misleading warning.
func _handle_probe(peer_id: int, _data: PackedByteArray) -> void:
	Netw.dbg.debug("Auth: peer %d probe request received", [peer_id])
	_probe_peer_ids[peer_id] = true

	if _is_rate_limited() or _probe_peer_ids.size() > MAX_ACTIVE_PROBES:
		Netw.dbg.debug(
			"Auth: peer %d probe deferred (busy)", [peer_id]
		)
		var busy := AuthProtocol.encode_probe_reply(
			AuthProtocol.ProbeStatus.BUSY
		)
		_api.send_auth(peer_id, busy)
		return

	var source := _server_info_source
	if source == null:
		source = DefaultServerInfoSource.new()

	var info := source.build_server_info(_tree)
	var payload := ServerInfo.to_payload(info)
	var reply := AuthProtocol.encode_probe_reply(
		AuthProtocol.ProbeStatus.OK, payload
	)
	_api.send_auth(peer_id, reply)


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
