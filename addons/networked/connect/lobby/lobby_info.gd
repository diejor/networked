## Plain data describing a single discoverable lobby.
##
## Emitted in batches by [signal LobbyProvider.lobby_list_updated]. UIs render
## one entry per [LobbyInfo] without caring about the underlying transport.
class_name LobbyInfo
extends Resource

## Transport-specific lobby identifier (e.g. a Steam lobby ID).
@export var id: int = 0

## Human-readable lobby name as advertised by the host.
@export var lobby_name: String = ""

## Current member count, including the host.
@export var players: int = 0

## Maximum member count the host configured.
@export var max_players: int = 0

## Free-form provider-specific metadata (host name, map, mode, etc.).
@export var metadata: Dictionary = {}


static func make(
	id: int,
	lobby_name: String,
	players: int,
	max_players: int,
	metadata: Dictionary = {}
) -> LobbyInfo:
	var info := LobbyInfo.new()
	info.id = id
	info.lobby_name = lobby_name
	info.players = players
	info.max_players = max_players
	info.metadata = metadata
	return info
