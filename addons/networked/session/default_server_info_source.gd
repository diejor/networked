## Default [ServerInfoSource] used when none is assigned.
##
## Populates [member ServerInfo.players] from
## [method SessionRoster.get_joined_players] and marks
## [member ServerInfo.is_local_listener] so localhost probes can distinguish
## a live host from a closed port.
@tool
class_name DefaultServerInfoSource
extends ServerInfoSource


func build_server_info(tree: MultiplayerTree) -> ServerInfo:
	var info := ServerInfo.new()
	info.is_local_listener = true
	if tree:
		info.players = tree.get_joined_players().size()
	return info
