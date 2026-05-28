## Wire-format codec for Networked auth-phase packets.
##
## The first packet exchanged during [SceneMultiplayer]'s auth phase is
## framed with a 4-byte magic prefix identifying its purpose:
## [br][br]
## [code]"NHEL"[/code] (Networked Hello) - a normal client opening a
## session. The provider payload (if any) is wrapped by this header so
## [NetwAuthProvider] implementations see only their own bytes.
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
const PROTOCOL_VERSION := 1

static var MAGIC_HELLO := PackedByteArray([0x4E, 0x48, 0x45, 0x4C]) # "NHEL"
static var MAGIC_PROBE := PackedByteArray([0x4E, 0x50, 0x52, 0x42]) # "NPRB"

const _HEADER_LEN := 6  # magic(4) + version(1) + flags-or-status(1)


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
	if data.size() < _HEADER_LEN:
		return Kind.UNKNOWN
	if _matches_magic(data, MAGIC_HELLO):
		return Kind.HELLO
	if _matches_magic(data, MAGIC_PROBE):
		return Kind.PROBE
	return Kind.UNKNOWN


## Builds a client-hello packet wrapping [param provider_payload].
##
## [param flags] is reserved for future use (default 0).
static func encode_client_hello(
	provider_payload: PackedByteArray,
	flags: int = 0,
) -> PackedByteArray:
	var buf := PackedByteArray()
	buf.append_array(MAGIC_HELLO)
	buf.append(PROTOCOL_VERSION)
	buf.append(flags & 0xFF)
	buf.append_array(provider_payload)
	return buf


## Decodes a client-hello packet. Returns
## [code]{ ok, version, flags, provider_payload }[/code]. When
## [code]ok[/code] is [code]false[/code], the other fields are zero / empty.
static func decode_client_hello(data: PackedByteArray) -> Dictionary:
	if not _matches_magic(data, MAGIC_HELLO) or data.size() < _HEADER_LEN:
		return { ok = false, version = 0, flags = 0,
			provider_payload = PackedByteArray() }
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return { ok = false, version = version, flags = 0,
			provider_payload = PackedByteArray() }
	return {
		ok = true,
		version = version,
		flags = int(data[5]),
		provider_payload = data.slice(_HEADER_LEN, data.size()),
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
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _HEADER_LEN:
		return { ok = false, version = 0, flags = 0 }
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return { ok = false, version = version, flags = 0 }
	return { ok = true, version = version, flags = int(data[5]) }


## Builds a probe-reply packet carrying [param payload] (typically a
## [code]var_to_bytes[/code] encoding of [ServerInfo]'s dictionary).
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
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _HEADER_LEN:
		return { ok = false, version = 0, status = 0,
			payload = PackedByteArray() }
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return { ok = false, version = version, status = 0,
			payload = PackedByteArray() }
	return {
		ok = true,
		version = version,
		status = int(data[5]),
		payload = data.slice(_HEADER_LEN, data.size()),
	}


static func _matches_magic(
	data: PackedByteArray, magic: PackedByteArray
) -> bool:
	if data.size() < magic.size():
		return false
	for i in magic.size():
		if data[i] != magic[i]:
			return false
	return true
