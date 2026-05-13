class_name AuthCoordinator
extends RefCounted
## Internal coordinator for [MultiplayerTree] authentication hooks.
##
## Binds [SceneMultiplayer] auth callbacks to a [NetwAuthProvider] and stores
## accepted identities or rejection reasons in [SessionRoster].

var _api: SceneMultiplayer
var _auth_provider: NetwAuthProvider
var _roster: SessionRoster
var _client_join_payload: JoinPayload


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


## Runs provider preparation before transport opens.
func prepare_join_payload(join_payload: JoinPayload) -> Error:
	if not _auth_provider:
		return OK
	
	Netw.dbg.info("Auth: running prepare for '%s'", [
		join_payload.username
	])
	var prepare_err := await _auth_provider._prepare(join_payload)
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


## Installs or clears Godot auth hooks before transport opens.
func prepare(use_auth: bool) -> void:
	if not _api:
		return
	
	if use_auth:
		_api.auth_callback = _on_auth_received
	else:
		_api.auth_callback = Callable()


## Clears client-side auth callback state after connecting.
func on_connected_to_server() -> void:
	if _auth_provider and _api:
		_api.auth_callback = Callable()


## Stores the local host identity returned by the provider.
func synthesize_host_identity() -> void:
	if not _auth_provider:
		return
	Netw.dbg.info("Auth: synthesizing host identity for peer 1")
	var host_identity := _auth_provider._get_host_identity()
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
	if not _auth_provider or not _client_join_payload:
		return
	
	Netw.dbg.debug("Auth: sending credentials for peer %d", [peer_id])
	var creds := _auth_provider._get_credentials(_client_join_payload)
	if creds.is_empty():
		Netw.dbg.error(
			"Auth: provider returned empty credentials for peer %d",
			[peer_id]
		)
		_api.disconnect_peer(peer_id)
		return
	
	var send_err := _api.send_auth(peer_id, creds)
	if send_err != OK:
		Netw.dbg.error(
			"Auth: failed to send credentials to peer %d: %s",
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
	Netw.dbg.warn("Auth failed for peer %d", [peer_id])


func _on_auth_received(peer_id: int, data: PackedByteArray) -> void:
	if not _auth_provider:
		Netw.dbg.debug("Auth: no provider, completing auth for peer %d", [
			peer_id
		])
		_api.complete_auth(peer_id)
		return
	Netw.dbg.info("Auth: validating credentials for peer %d", [peer_id])
	var identity := _auth_provider._authenticate(peer_id, data)
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
