## Round-trip and rejection tests for [AuthProtocol].
class_name TestAuthProtocol
extends NetwTestSuite


func test_classify_hello_magic() -> void:
	var packet := AuthProtocol.encode_client_hello(PackedByteArray())
	assert_that(AuthProtocol.classify(packet)).is_equal(
		AuthProtocol.Kind.HELLO
	)


func test_classify_probe_magic() -> void:
	var packet := AuthProtocol.encode_probe_request()
	assert_that(AuthProtocol.classify(packet)).is_equal(
		AuthProtocol.Kind.PROBE
	)


func test_classify_unknown_magic() -> void:
	var packet := PackedByteArray([0x00, 0x01, 0x02, 0x03, 0x04, 0x05])
	assert_that(AuthProtocol.classify(packet)).is_equal(
		AuthProtocol.Kind.UNKNOWN
	)


func test_classify_short_packet_is_unknown() -> void:
	var packet := PackedByteArray([0x4E, 0x48])  # truncated "NH"
	assert_that(AuthProtocol.classify(packet)).is_equal(
		AuthProtocol.Kind.UNKNOWN
	)


func test_hello_round_trip_with_payload() -> void:
	var provider_payload := PackedByteArray([0xDE, 0xAD, 0xBE, 0xEF])
	var packet := AuthProtocol.encode_client_hello(provider_payload, 0x42)
	var decoded := AuthProtocol.decode_client_hello(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.version).is_equal(AuthProtocol.PROTOCOL_VERSION)
	assert_that(decoded.flags).is_equal(0x42)
	assert_that(decoded.provider_payload).is_equal(provider_payload)


func test_hello_round_trip_with_empty_payload() -> void:
	var packet := AuthProtocol.encode_client_hello(PackedByteArray())
	var decoded := AuthProtocol.decode_client_hello(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.provider_payload).is_equal(PackedByteArray())


func test_probe_request_round_trip() -> void:
	var packet := AuthProtocol.encode_probe_request(0x07)
	var decoded := AuthProtocol.decode_probe_request(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.version).is_equal(AuthProtocol.PROTOCOL_VERSION)
	assert_that(decoded.flags).is_equal(0x07)


func test_probe_reply_round_trip() -> void:
	var payload := PackedByteArray([0xCA, 0xFE])
	var packet := AuthProtocol.encode_probe_reply(
		AuthProtocol.ProbeStatus.OK, payload
	)
	var decoded := AuthProtocol.decode_probe_reply(packet)

	assert_that(decoded.ok).is_true()
	assert_that(decoded.status).is_equal(AuthProtocol.ProbeStatus.OK)
	assert_that(decoded.payload).is_equal(payload)


func test_decode_hello_rejects_probe_magic() -> void:
	var packet := AuthProtocol.encode_probe_request()
	var decoded := AuthProtocol.decode_client_hello(packet)
	assert_that(decoded.ok).is_false()


func test_decode_probe_rejects_hello_magic() -> void:
	var packet := AuthProtocol.encode_client_hello(PackedByteArray())
	var decoded := AuthProtocol.decode_probe_request(packet)
	assert_that(decoded.ok).is_false()


func test_decode_rejects_version_mismatch() -> void:
	# Hand-craft a packet with a wrong version byte.
	var packet := PackedByteArray()
	packet.append_array(AuthProtocol.MAGIC_HELLO)
	packet.append(0xFF)  # bogus version
	packet.append(0x00)
	var decoded := AuthProtocol.decode_client_hello(packet)
	assert_that(decoded.ok).is_false()
