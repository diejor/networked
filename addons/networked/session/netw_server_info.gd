## Server metadata carried by an [code]NPRB[/code] probe reply.
class_name NetwServerInfo
extends Resource

@export var motd: String = ""
@export var players: int = 0
@export var max_players: int = 0
@export var game_mode: StringName = &""
@export var version: String = ""
@export var app_id: StringName = &""
@export var is_local_listener: bool = false
@export var metadata: Dictionary = { }


## Builds the default probe reply from live session state on [param api].
##
## Reports the live [member NetwMultiplayer.participants] count, the
## [member NetwSessionHandle.app_id], and
## [member NetwSessionHandle.advertised_max_players], and marks
## [member is_local_listener] so a caller can tell a live local host from a
## closed port. This is the built-in provider a probe answers with when neither
## a per-session override nor a [method Netw.configure_server_info] registration
## is present.
static func from_session(api: NetwMultiplayer) -> NetwServerInfo:
	var info := NetwServerInfo.new()
	info.is_local_listener = true
	if api:
		info.players = api.participants.size()
		info.app_id = api.session.app_id
		info.max_players = api._session.advertised_max_players
	return info


## Serializes [param info] to the probe wire format.
static func to_payload(info: NetwServerInfo) -> PackedByteArray:
	if info == null:
		return PackedByteArray()
	return var_to_bytes(
		{
			"motd": info.motd,
			"players": info.players,
			"max_players": info.max_players,
			"game_mode": info.game_mode,
			"version": info.version,
			"app_id": info.app_id,
			"is_local_listener": info.is_local_listener,
			"metadata": info.metadata,
		},
	)


## Decodes [param bytes] into a fresh [NetwServerInfo].
static func from_payload(bytes: PackedByteArray) -> NetwServerInfo:
	if bytes.is_empty():
		return null
	var decoded = bytes_to_var(bytes)
	if typeof(decoded) != TYPE_DICTIONARY:
		return null
	var info := NetwServerInfo.new()
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
