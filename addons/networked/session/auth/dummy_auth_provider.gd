## Concrete [NetwAuthProvider] for testing and development.
##
## Echoes the client-claimed username as the server-authoritative
## identity. Service is set to [code]&"dummy"[/code].
##
## [b]Note:[/b] This provider does not perform any real authentication.
## Use it to verify the auth pipeline wiring without depending on
## external services.
class_name DummyAuthProvider
extends NetwAuthProvider


func _prepare(payload: JoinPayload) -> Error:
	return OK


func _get_credentials(payload: JoinPayload) -> PackedByteArray:
	return var_to_bytes({
		"service": "dummy",
		"username": payload.username,
	})


func _authenticate(peer_id: int, data: PackedByteArray) -> NetwIdentity:
	var creds := bytes_to_var(data)
	if not creds or creds.get("service") != "dummy":
		rejection_reason = "Invalid dummy auth credentials"
		return null
	var identity := NetwIdentity.new()
	identity.username = creds.username
	identity.external_id = "dummy_%d" % peer_id
	identity.service = &"dummy"
	return identity


func _get_host_identity() -> NetwIdentity:
	var identity := NetwIdentity.new()
	identity.username = "host"
	identity.external_id = "dummy_host"
	identity.service = &"dummy"
	return identity
