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


func resolve(instance_id: String, _tree: MultiplayerTree) -> JoinTarget:
	if instance_id.is_empty():
		Netw.dbg.warn("DedicatedDiscordRendezvous: empty instance_id.")
		return null
	if public_host.is_empty():
		Netw.dbg.warn(
			"DedicatedDiscordRendezvous: public_host unset; no server configured.",
		)
		return null

	var backend := WebSocketBackend.new()
	backend.public_host = public_host
	backend.port = port

	var target := JoinTarget.new()
	target.display_name = "Discord Activity"
	# The server groups rooms by ?instance=, so every participant joins the same
	# room. A non-empty address keeps the service on the join path, never host.
	target.address = "wss://%s/?instance=%s" % [public_host, instance_id]
	target.backend = backend
	target.metadata = { "instance_id": instance_id }
	return target
