## [DiscordRendezvous] that maps a Discord instance to a dedicated WSS room.
##
## There is no shared store and no client-side host election. The client joins
## the dedicated server, and the server groups rooms by [code]instance[/code].
## [codeblock]
## bring_up(tree, TRANSPORT_MODE_CLIENT, address, username, join_args)
## └── address = "wss://<public_host>/?instance=<instance_id>"
## [/codeblock]
class_name DedicatedDiscordRendezvous
extends DiscordRendezvous

## Public host of the dedicated WSS server, without scheme.
@export var public_host: String = ""

## TCP port the dedicated server listens on.
@export var port: int = 21253


## Joins the dedicated room keyed by [param instance_id].
func connect_session(
		instance_id: String,
		tree: MultiplayerTree,
		username: StringName,
		join_args: Array,
) -> Error:
	if instance_id.is_empty():
		push_warning("DedicatedDiscordRendezvous: empty instance_id.")
		return ERR_INVALID_PARAMETER
	if public_host.is_empty():
		push_warning("DedicatedDiscordRendezvous: public_host unset.")
		return ERR_UNCONFIGURED
	tree.peer_class = &"WebSocketMultiplayerPeer"
	return await bring_up(
		tree,
		NetwMultiplayer.TRANSPORT_MODE_CLIENT,
		_address_for(instance_id),
		username,
		join_args,
	)


# The dedicated room's address for the instance id carried in the query string.
func _address_for(instance_id: String) -> String:
	return "wss://%s/?instance=%s" % [public_host, instance_id]
