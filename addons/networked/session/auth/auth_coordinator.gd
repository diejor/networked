class_name AuthCoordinator
extends RefCounted
## Internal coordinator for [MultiplayerTree] authentication hooks.
##
## Binds [SceneMultiplayer] auth callbacks to a [NetwAuthProvider] and stores
## accepted identities or rejection reasons in [SessionRoster]. Validates
## [code]NHEL[/code] client hellos; [code]NPRB[/code] server-browser probes
## that ride the same auth phase are dispatched to [AuthProbeResponder] so
## this class stays about authentication.

var _api: SceneMultiplayer
var _auth_provider: NetwAuthProvider
var _roster: SessionRoster
var _client_join_payload: JoinPayload
var _probe_responder := AuthProbeResponder.new()
var _app_tag: int = 0


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
	_probe_responder.bind_api(api)
	_connect_auth_signals()


## Sets the provider used by the auth handshake.
func set_auth_provider(provider: NetwAuthProvider) -> void:
	_auth_provider = provider


## Sets the local game-build tag stamped on the hello and required of every
## joining peer. 0 disables the build gate.
func set_app_tag(tag: int) -> void:
	_app_tag = tag


## Stores the join payload used to build client auth credentials.
func set_client_join_payload(payload: JoinPayload) -> void:
	_client_join_payload = payload


## Stores the owning tree so probe replies can build a [ServerInfo] from
## live session state. Delegates to [AuthProbeResponder].
func set_tree(tree: MultiplayerTree) -> void:
	_probe_responder.set_tree(tree)


## Sets the [ServerInfoSource] used to build probe replies. When
## [code]null[/code], a [DefaultServerInfoSource] is created on first use.
## Delegates to [AuthProbeResponder].
func set_server_info_source(source: ServerInfoSource) -> void:
	_probe_responder.set_server_info_source(source)


## Runs provider preparation before transport opens.
func prepare_join_payload(join_payload: JoinPayload) -> Error:
	if not _auth_provider:
		return OK

	Netw.dbg.info(
		"Auth: running prepare for '%s'",
		[
			join_payload.username,
		],
	)
	var prepare_err := await _auth_provider.prepare(join_payload)
	if prepare_err != OK:
		Netw.dbg.error(
			"Auth prepare failed: %s",
			[error_string(prepare_err)],
			func(m): push_error(m)
		)
		return prepare_err
	Netw.dbg.info(
		"Auth: prepare succeeded for '%s'",
		[
			join_payload.username,
		],
	)
	return OK


## Installs the Networked auth dispatcher on the tree's SceneMultiplayer.
##
## The callback is installed unconditionally so the dispatcher can
## multiplex hello packets and probe requests. Whether a
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
			[host_identity.username, host_identity.service],
		)
		_roster.get_peer_context(
			MultiplayerPeer.TARGET_PEER_SERVER,
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
		NetwIdentityBucket,
	)
	if bucket.identity:
		Netw.dbg.info(
			"Auth: overriding username '%s' with bucket identity '%s' "
			+ "(service=%s)",
			[
				join_payload.username,
				bucket.identity.username,
				bucket.identity.service,
			],
		)
		join_payload.username = bucket.identity.username
	else:
		Netw.dbg.warn(
			"Auth: provider configured but no identity for peer %d; "
			+ "falling back to client-claimed username '%s'",
			[peer_id, join_payload.username],
			func(m): push_warning(m)
		)


## Clears runtime auth state and disconnects API hooks.
func clear() -> void:
	bind_api(null)
	_auth_provider = null
	_client_join_payload = null
	_probe_responder.clear()


func _unbind_api() -> void:
	if not _api:
		return

	_probe_responder.bind_api(null)
	_api.auth_callback = Callable()
	if _api.peer_authenticating.is_connected(_on_peer_authenticating):
		_api.peer_authenticating.disconnect(_on_peer_authenticating)
	if _api.peer_authentication_failed.is_connected(
		_on_peer_authentication_failed,
	):
		_api.peer_authentication_failed.disconnect(
			_on_peer_authentication_failed,
		)
	_api = null


func _connect_auth_signals() -> void:
	if not _api:
		return
	if not _api.peer_authenticating.is_connected(_on_peer_authenticating):
		_api.peer_authenticating.connect(_on_peer_authenticating)
	if not _api.peer_authentication_failed.is_connected(
		_on_peer_authentication_failed,
	):
		_api.peer_authentication_failed.connect(
			_on_peer_authentication_failed,
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
				[peer_id],
			)
			_api.disconnect_peer(peer_id)
			return

	var hello := AuthProtocol.encode_client_hello(provider_payload, _app_tag)
	Netw.dbg.debug(
		"Auth: sending NHEL for peer %d with app_tag=0x%08x "
		+ "(%d provider bytes).",
		[peer_id, _app_tag, provider_payload.size()],
	)

	var send_err := _api.send_auth(peer_id, hello)
	if send_err != OK:
		Netw.dbg.error(
			"Auth: failed to send NHEL to peer %d: %s",
			[peer_id, error_string(send_err)],
		)
		_api.disconnect_peer(peer_id)
		return

	var complete_err := _api.complete_auth(peer_id)
	if complete_err != OK:
		Netw.dbg.error(
			"Auth: failed to complete local auth for peer %d: %s",
			[peer_id, error_string(complete_err)],
		)
		_api.disconnect_peer(peer_id)


func _on_peer_authentication_failed(peer_id: int) -> void:
	if _probe_responder.note_auth_failed(peer_id):
		return
	Netw.dbg.warn(
		"Auth failed for peer %d",
		[peer_id],
		func(m): push_warning(m)
	)


func _on_auth_received(peer_id: int, data: PackedByteArray) -> void:
	match AuthProtocol.classify(data):
		AuthProtocol.Kind.HELLO:
			_handle_hello(peer_id, data)
		AuthProtocol.Kind.PROBE:
			_probe_responder.handle(peer_id)
		_:
			Netw.dbg.warn(
				"Auth: peer %d sent unknown auth payload (%d bytes); "
				+ "fail-closed disconnect.",
				[peer_id, data.size()],
				func(m): push_warning(m)
			)
			_api.disconnect_peer(peer_id)


func _handle_hello(peer_id: int, data: PackedByteArray) -> void:
	var decoded := AuthProtocol.decode_client_hello(data, _app_tag)
	Netw.dbg.debug(
		"Auth: received NHEL from peer %d with app_tag=0x%08x. "
		+ "Expected app_tag=0x%08x.",
		[peer_id, int(decoded.get("app_tag", 0)), _app_tag],
	)
	if not decoded.ok:
		if decoded.get("reason", "") == "app":
			Netw.dbg.warn(
				"Auth: peer %d build/app mismatch. Received app_tag=0x%08x. "
				+ "Expected app_tag=0x%08x. Fail closed disconnect.",
				[peer_id, int(decoded.get("app_tag", 0)), _app_tag],
				func(m): push_warning(m)
			)
			_roster.set_auth_rejection_reason(peer_id, "Incompatible game build")
		else:
			Netw.dbg.warn(
				"Auth: peer %d NHEL decode failed (%s); fail-closed disconnect.",
				[peer_id, decoded.get("reason", "framing")],
				func(m): push_warning(m)
			)
		_api.disconnect_peer(peer_id)
		return

	var provider_payload: PackedByteArray = decoded.provider_payload

	if not _auth_provider:
		Netw.dbg.debug(
			"Auth: no provider, completing auth for peer %d",
			[peer_id],
		)
		_api.complete_auth(peer_id)
		return

	Netw.dbg.info("Auth: validating credentials for peer %d", [peer_id])
	var identity := _auth_provider.authenticate(peer_id, provider_payload)
	if identity:
		Netw.dbg.info(
			"Auth: peer %d accepted as '%s' (service=%s)",
			[peer_id, identity.username, identity.service],
		)
		_roster.get_peer_context(peer_id).get_bucket(
			NetwIdentityBucket,
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
			"Auth rejected for peer %d: %s",
			[peer_id, reason],
			func(m): push_warning(m)
		)
		_api.disconnect_peer(peer_id)
