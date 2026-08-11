## Round-trip and rejection tests for [AuthProtocol].
class_name TestAuthProtocol
extends NetwTestSuite

const AuthProtocol := preload("res://addons/networked/session/auth/auth_protocol.gd")


func test_classify_packets() -> void:
	var rows := [
		[
			AuthProtocol.encode_client_hello(PackedByteArray()),
			AuthProtocol.Kind.HELLO,
		],
		[
			AuthProtocol.encode_probe_request(),
			AuthProtocol.Kind.PROBE,
		],
		[
			PackedByteArray([0x00, 0x01, 0x02, 0x03, 0x04, 0x05]),
			AuthProtocol.Kind.UNKNOWN,
		],
		[
			PackedByteArray([0x4E, 0x48]),
			AuthProtocol.Kind.UNKNOWN,
		],
	]
	for row in rows:
		assert_that(AuthProtocol.classify(row[0])).is_equal(row[1])


func test_hello_round_trips() -> void:
	var provider_payload := PackedByteArray([0xDE, 0xAD, 0xBE, 0xEF])
	var packet := AuthProtocol.encode_client_hello(provider_payload, 0, 0x42)
	var decoded := AuthProtocol.decode_client_hello(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.version).is_equal(AuthProtocol.PROTOCOL_VERSION)
	assert_that(decoded.app_tag).is_equal(0)
	assert_that(decoded.flags).is_equal(0x42)
	assert_that(decoded.provider_payload).is_equal(provider_payload)

	provider_payload = PackedByteArray([0x01, 0x02, 0x03])
	packet = AuthProtocol.encode_client_hello(provider_payload, 0xABCDEF12)
	decoded = AuthProtocol.decode_client_hello(packet, 0xABCDEF12)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.app_tag).is_equal(0xABCDEF12)
	assert_that(decoded.provider_payload).is_equal(provider_payload)

	packet = AuthProtocol.encode_client_hello(PackedByteArray())
	decoded = AuthProtocol.decode_client_hello(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.provider_payload).is_equal(PackedByteArray())


func test_probe_round_trips() -> void:
	var packet := AuthProtocol.encode_probe_request(0x07)
	var decoded := AuthProtocol.decode_probe_request(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.version).is_equal(AuthProtocol.PROTOCOL_VERSION)
	assert_that(decoded.flags).is_equal(0x07)

	var payload := PackedByteArray([0xCA, 0xFE])
	packet = AuthProtocol.encode_probe_reply(
		AuthProtocol.ProbeStatus.OK,
		payload,
	)
	decoded = AuthProtocol.decode_probe_reply(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.status).is_equal(AuthProtocol.ProbeStatus.OK)
	assert_that(decoded.payload).is_equal(payload)


func test_decode_rejections() -> void:
	var packet := AuthProtocol.encode_client_hello(PackedByteArray(), 0x11111111)
	var decoded := AuthProtocol.decode_client_hello(packet, 0x22222222)

	assert_that(decoded.ok).is_false()
	assert_that(decoded.reason).is_equal("app")
	assert_that(decoded.app_tag).is_equal(0x11111111)

	packet = AuthProtocol.encode_probe_request()
	decoded = AuthProtocol.decode_client_hello(packet)
	assert_that(decoded.ok).is_false()

	packet = AuthProtocol.encode_client_hello(PackedByteArray())
	decoded = AuthProtocol.decode_probe_request(packet)
	assert_that(decoded.ok).is_false()

	packet = PackedByteArray()
	packet.append_array(AuthProtocol.MAGIC_HELLO)
	packet.append(0xFF)
	packet.append_array(PackedByteArray([0x00, 0x00, 0x00, 0x00]))
	packet.append(0x00)
	decoded = AuthProtocol.decode_client_hello(packet)
	assert_that(decoded.ok).is_false()
	assert_that(decoded.reason).is_equal("version")
