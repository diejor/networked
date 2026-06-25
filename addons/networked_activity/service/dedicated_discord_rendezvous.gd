## [DiscordRendezvous] that maps a Discord instance to a room on a game-owned
## dedicated WSS server.
##
## This is the "no Nakama" path. There is no shared store and no host election:
## the client always joins, and the server groups connections into rooms by the
## [code]instance[/code] query parameter. Because a browser cannot open a
## listening socket, the [WebSocketBackend] this produces reports
## [method WebSocketBackend.can_host] as [code]false[/code], so the topology is
## always client-joins-server.
## [codeblock]
## address = "wss://<public_host>/?instance=<instance_id>"
##           # server keys rooms by ?instance=; every participant joins
## [/codeblock]
## The reference server is documented but not shipped in v1, so this resolves a
## target whenever [member public_host] is set and otherwise refuses. It exists
## so the second transport seam is real and selectable, not aspirational.
class_name DedicatedDiscordRendezvous
extends DiscordRendezvous

## Public host of the dedicated WSS server, without scheme. Behind the Discord
## proxy this is the mapped [code]discordsays.com[/code] prefix host. Empty means
## no server is configured and [method resolve] refuses.
@export var public_host: String = ""

## TCP port the dedicated server listens on. Use [code]443[/code] behind a TLS
## terminating proxy.
@export var port: int = 21253


func connect_session(
		instance_id: String, tree: MultiplayerTree, payload: JoinPayload,
) -> Error:
	if instance_id.is_empty():
		Netw.dbg.warn("DedicatedDiscordRendezvous: empty instance_id.")
		return ERR_INVALID_PARAMETER
	if public_host.is_empty():
		Netw.dbg.warn(
			"DedicatedDiscordRendezvous: public_host unset; no server configured.",
		)
		return ERR_UNCONFIGURED
	# The server keys rooms by ?instance=, so there is no host election: every
	# participant simply joins, and the server groups them into one room.
	return await tree.join(_target_for(instance_id), payload)


# Builds the join target for instance_id: a WebSocketBackend pointed at the
# dedicated server with the instance carried in the query string.
func _target_for(instance_id: String) -> JoinTarget:
	var backend := WebSocketBackend.new()
	backend.public_host = public_host
	backend.port = port

	var target := JoinTarget.new()
	target.display_name = "Discord Activity"
	target.address = "wss://%s/?instance=%s" % [public_host, instance_id]
	target.backend = backend
	target.metadata = { "instance_id": instance_id }
	return target
