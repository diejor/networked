## Wire-format codec for Networked auth-phase packets.
##
## The first packet exchanged during [SceneMultiplayer]'s auth phase is
## framed with a 4-byte magic prefix identifying its purpose:
## [br][br]
## [code]"NHEL"[/code] (Networked Hello) - a normal client opening a
## session. Its header carries a 4-byte app tag right after the version, so a
## peer running a different game build ([member MultiplayerTree.app_id]) is
## rejected before the provider payload is even read. The provider payload (if
## any) is wrapped by this header so [NetwAuthProvider] implementations see only
## their own bytes.
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
const PROTOCOL_VERSION := 2

static var MAGIC_HELLO := PackedByteArray([0x4E, 0x48, 0x45, 0x4C]) # "NHEL"
static var MAGIC_PROBE := PackedByteArray([0x4E, 0x50, 0x52, 0x42]) # "NPRB"

# Hello carries a 4-byte app tag the probe does not need, since probes never
# join the session they query.
const _HELLO_HEADER_LEN := 10 # magic(4) + version(1) + app_tag(4) + flags(1)
const _PROBE_HEADER_LEN := 6 # magic(4) + version(1) + status-or-flags(1)

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
	if data.size() < _PROBE_HEADER_LEN:
		return Kind.UNKNOWN
	if _matches_magic(data, MAGIC_HELLO):
		return Kind.HELLO
	if _matches_magic(data, MAGIC_PROBE):
		return Kind.PROBE
	return Kind.UNKNOWN


## Builds a client-hello packet wrapping [param provider_payload].
##
## [param app_tag] is the game-build tag from [member MultiplayerTree.app_id]
## (0 when no build gate is set). [param flags] is reserved for future use
## (default 0).
static func encode_client_hello(
		provider_payload: PackedByteArray,
		app_tag: int = 0,
		flags: int = 0,
) -> PackedByteArray:
	var buf := PackedByteArray()
	buf.append_array(MAGIC_HELLO)
	buf.append(PROTOCOL_VERSION)
	_append_u32(buf, app_tag)
	buf.append(flags & 0xFF)
	buf.append_array(provider_payload)
	return buf


## Decodes a client-hello packet, rejecting it when its app tag differs from
## [param local_app_tag]. Returns
## [code]{ ok, reason, version, app_tag, flags, provider_payload }[/code]. When
## [code]ok[/code] is [code]false[/code], [code]reason[/code] is one of
## [code]"framing"[/code], [code]"version"[/code], or [code]"app"[/code] and the
## remaining fields are zero / empty.
static func decode_client_hello(
		data: PackedByteArray,
		local_app_tag: int = 0,
) -> Dictionary:
	if not _matches_magic(data, MAGIC_HELLO) or data.size() < _HELLO_HEADER_LEN:
		return {
			ok = false,
			reason = "framing",
			version = 0,
			app_tag = 0,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	var version := int(data[4])
	var app_tag := _read_u32(data, 5)
	if version != PROTOCOL_VERSION:
		return {
			ok = false,
			reason = "version",
			version = version,
			app_tag = app_tag,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	if app_tag != (local_app_tag & 0xFFFFFFFF):
		return {
			ok = false,
			reason = "app",
			version = version,
			app_tag = app_tag,
			flags = 0,
			provider_payload = PackedByteArray(),
		}
	return {
		ok = true,
		reason = "",
		version = version,
		app_tag = app_tag,
		flags = int(data[9]),
		provider_payload = data.slice(_HELLO_HEADER_LEN, data.size()),
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
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _PROBE_HEADER_LEN:
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
	if not _matches_magic(data, MAGIC_PROBE) or data.size() < _PROBE_HEADER_LEN:
		return {
			ok = false,
			version = 0,
			status = 0,
			payload = PackedByteArray(),
		}
	var version := int(data[4])
	if version != PROTOCOL_VERSION:
		return {
			ok = false,
			version = version,
			status = 0,
			payload = PackedByteArray(),
		}
	return {
		ok = true,
		version = version,
		status = int(data[5]),
		payload = data.slice(_PROBE_HEADER_LEN, data.size()),
	}


static func _matches_magic(
		data: PackedByteArray,
		magic: PackedByteArray,
) -> bool:
	if data.size() < magic.size():
		return false
	for i in magic.size():
		if data[i] != magic[i]:
			return false
	return true


# Appends value as 4 little-endian bytes.
static func _append_u32(buf: PackedByteArray, value: int) -> void:
	buf.append(value & 0xFF)
	buf.append((value >> 8) & 0xFF)
	buf.append((value >> 16) & 0xFF)
	buf.append((value >> 24) & 0xFF)


# Reads 4 little-endian bytes at offset into an unsigned 32-bit int.
static func _read_u32(data: PackedByteArray, offset: int) -> int:
	return int(data[offset]) \
			| (int(data[offset + 1]) << 8) \
			| (int(data[offset + 2]) << 16) \
			| (int(data[offset + 3]) << 24)
