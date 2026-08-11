## Development [NetwAuthFlow] that trusts the client supplied username.
##
## [method verify] turns the [JoinPayload] username mirrored by
## [method credentials] into a [NetwIdentity]. It is only for tests, examples,
## and local development.
## [codeblock]
## api.session.set_auth_flow(DummyAuth.new())
## await api._session.prepare_join(payload)
## [/codeblock]
class_name DummyAuth
extends NetwAuthFlow

func prepare(_payload: JoinPayload) -> Error:
	return OK


func credentials(payload: JoinPayload) -> PackedByteArray:
	return var_to_bytes(
		{
			"service": "dummy",
			"username": payload.username,
		},
	)


func verify(peer_id: int, data: PackedByteArray) -> AuthResult:
	var creds := bytes_to_var(data)
	if not creds or creds.get("service") != "dummy":
		return AuthResult.reject("Invalid dummy auth credentials")
	var identity := NetwIdentity.new()
	identity.username = creds.username
	identity.external_id = "dummy_%d" % peer_id
	identity.service = &"dummy"
	return AuthResult.accept(identity)


func host_identity() -> NetwIdentity:
	var identity := NetwIdentity.new()
	identity.username = "host"
	identity.external_id = "dummy_host"
	identity.service = &"dummy"
	return identity
