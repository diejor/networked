## Resource that builds a [ServerDescriptor.Info] in response to a probe.
##
## Assign a subclass (or an instance of this class itself) to
## [member MultiplayerTree.server_info_source] to control what
## server-browser-style clients see. By default, it reports live player counts
## derived from the session roster.
@tool
@abstract
class_name ServerDescriptor
extends Resource

## Server-side metadata reported by a probe reply.
##
## Built on the host by a [ServerDescriptor] in response to an [code]NPRB[/code]
## auth packet (see [AuthProtocol]). Encoded onto the wire via
## [method to_payload] using a dictionary projection, so adding optional fields
## later does not break older probers.
class Info:
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
	static func to_payload(info: Info) -> PackedByteArray:
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


	## Decodes a wire-format byte array into a fresh [Info].
	##
	## Returns [code]null[/code] if [param bytes] does not decode to a dictionary.
	static func from_payload(bytes: PackedByteArray) -> Info:
		if bytes.is_empty():
			return null
		var decoded = bytes_to_var(bytes)
		if typeof(decoded) != TYPE_DICTIONARY:
			return null
		var info := Info.new()
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


## Builds a fresh [Info] for [param tree]. Called on the host inside
## the auth callback; must not mutate session state.
##
## [br][br][b]Server Only.[/b]
@abstract
func build_server_info(tree: MultiplayerTree) -> Info
