@tool
class_name DefaultServerDescriptor
extends ServerDescriptor
## The default [ServerDescriptor]. Reports live player counts derived from the session roster.

func build_server_info(tree: MultiplayerTree) -> ServerDescriptor.Info:
	var info := ServerDescriptor.Info.new()
	info.is_local_listener = true
	if tree:
		info.players = tree.get_joined_players().size()
		info.app_id = tree.app_id
		if tree.backend and "max_clients" in tree.backend:
			info.max_players = tree.backend.max_clients
	return info
