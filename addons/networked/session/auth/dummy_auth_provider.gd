## Development [NetwAuth] that trusts the client supplied username.
##
## [method authenticate] turns the [JoinPayload] username mirrored by
## [method get_credentials] into a [NetwIdentity]. It is only for tests,
## examples, and local development.
## [codeblock]
## tree.auth_provider = DummyAuth.new()
## await tree.join(target, payload)
## [/codeblock]
class_name DummyAuth
extends NetwAuth

func prepare(payload: JoinPayload) -> Error:
	return OK


func get_credentials(payload: JoinPayload) -> PackedByteArray:
	return var_to_bytes(
		{
			"service": "dummy",
			"username": payload.username,
		},
	)


func authenticate(peer_id: int, data: PackedByteArray) -> NetwIdentity:
	var creds := bytes_to_var(data)
	if not creds or creds.get("service") != "dummy":
		rejection_reason = "Invalid dummy auth credentials"
		return null
	var identity := NetwIdentity.new()
	identity.username = creds.username
	identity.external_id = "dummy_%d" % peer_id
	identity.service = &"dummy"
	return identity


func get_host_identity() -> NetwIdentity:
	var identity := NetwIdentity.new()
	identity.username = "host"
	identity.external_id = "dummy_host"
	identity.service = &"dummy"
	return identity
