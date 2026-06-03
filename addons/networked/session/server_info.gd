## Server-side metadata reported by a probe reply.
##
## Built on the host by a [ServerInfoSource] in response to an [code]NPRB[/code]
## auth packet (see [AuthProtocol]). Encoded onto the wire via
## [method to_payload] using a dictionary projection, so adding optional fields
## later does not break older probers.
@tool
class_name ServerInfo
extends Resource

@export var motd: String = ""
@export var players: int = 0
@export var max_players: int = 0
@export var game_mode: StringName = &""
@export var version: String = ""
@export var app_id: StringName = &""
@export var is_local_listener: bool = false
@export var metadata: Dictionary = { }


## Serializes [param info] to a wire-format byte array.
##
## Returns an empty array if [param info] is [code]null[/code].
static func to_payload(info: ServerInfo) -> PackedByteArray:
	if info == null:
		return PackedByteArray()
	var dict := {
		"motd": info.motd,
		"players": info.players,
		"max_players": info.max_players,
		"game_mode": info.game_mode,
		"version": info.version,
		"app_id": info.app_id,
		"is_local_listener": info.is_local_listener,
		"metadata": info.metadata,
	}
	return var_to_bytes(dict)


## Decodes a wire-format byte array into a fresh [ServerInfo].
##
## Returns [code]null[/code] if [param bytes] does not decode to a dictionary.
static func from_payload(bytes: PackedByteArray) -> ServerInfo:
	if bytes.is_empty():
		return null
	var decoded = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return null
	var info := ServerInfo.new()
	info.motd = decoded.get("motd", "")
	info.players = int(decoded.get("players", 0))
	info.max_players = int(decoded.get("max_players", 0))
	info.game_mode = StringName(decoded.get("game_mode", ""))
	info.version = decoded.get("version", "")
	info.app_id = StringName(decoded.get("app_id", ""))
	info.is_local_listener = bool(decoded.get("is_local_listener", false))
	var meta = decoded.get("metadata", { })
	info.metadata = meta if typeof(meta) == TYPE_DICTIONARY else { }
	return info
