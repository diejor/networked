## [NetwAuth] that verifies a joining peer's Nakama presence identity.
##
## The host trusts the Nakama server's presence list, which maps each peer to
## their authenticated Nakama user id. This is what makes the identity
## spoof-proof even though the listen-server host is itself an untrusted browser
## in the relay topology.
class_name NakamaAuth
extends NetwAuth

# Bound by the installer (DiscordActivityService) so the hooks can reach the
# authenticated session and the active relay presence.
var _session: Variant
var _tree: MultiplayerTree


## Binds the authenticated [NakamaSessionService] used to read identity.
func bind_session(session: Variant) -> void:
	_session = session


## Binds the [MultiplayerTree] used to reach the active presence.
func bind_tree(tree: MultiplayerTree) -> void:
	_tree = tree


## Prepares the provider by ensuring the Nakama session is authenticated.
func prepare(_payload: JoinPayload) -> Error:
	if _session == null or not _session.has_method("is_authenticated") \
			or not _session.is_authenticated():
		rejection_reason = "Nakama session not authenticated"
		return ERR_UNAUTHORIZED
	return OK


## Sends a trivial credentials payload indicating Nakama authentication.
func get_credentials(_payload: JoinPayload) -> PackedByteArray:
	return var_to_bytes({ "service": "nakama" })


## Verifies the credentials and binds the peer to their Nakama presence.
##
## [br][br][b]Server Only.[/b]
func authenticate(peer_id: int, data: PackedByteArray) -> NetwIdentity:
	var creds: Variant = bytes_to_var(data)
	if typeof(creds) != TYPE_DICTIONARY or String(creds.get("service", "")) != "nakama":
		rejection_reason = "Invalid Nakama auth credentials"
		return null
	var wrapper := _active_wrapper()
	if wrapper == null:
		rejection_reason = "Nakama relay presence unavailable"
		return null
	var attested_uid := wrapper.user_id_for_peer(peer_id)
	if attested_uid.is_empty():
		rejection_reason = "Peer Nakama identity not found in presence"
		return null
	var attested_username := wrapper.username_for_peer(peer_id)
	return _identity(attested_uid, StringName(attested_username))


## Returns the local host's own Nakama identity from the session.
func get_host_identity() -> NetwIdentity:
	var local_uid := _local_user_id()
	if local_uid.is_empty():
		rejection_reason = "Local Nakama session unavailable"
		return null
	var local_username := _local_username()
	return _identity(local_uid, StringName(local_username))


func _active_wrapper() -> NakamaWrapper:
	if _tree == null:
		return null
	var dir := _tree.get_service(NakamaLobbyDirectory) as NakamaLobbyDirectory
	return dir.wrapper() if dir != null else null


func _local_user_id() -> String:
	if _session == null or not _session.has_method("local_user_id"):
		return ""
	return _session.local_user_id()


func _local_username() -> String:
	if _session == null or not _session.has_method("local_username"):
		return ""
	return _session.local_username()


func _identity(external_id: String, username: StringName) -> NetwIdentity:
	var identity := NetwIdentity.new()
	identity.username = username if not username.is_empty() else StringName(external_id)
	identity.external_id = external_id
	identity.service = &"nakama"
	identity.metadata = { "verified": true }
	return identity
