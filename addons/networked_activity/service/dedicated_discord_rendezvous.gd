## A rendezvous that sends every instance to one dedicated WSS server.
##
## The server groups rooms by [code]instance[/code], so this never hosts.
## [codeblock lang=text]
## wss://<public_host>/?instance=<instance_id>
## [/codeblock]
class_name DedicatedDiscordRendezvous
extends DiscordRendezvous

## Public host of the dedicated server, without scheme.
@export var public_host: String = ""

## TCP port the dedicated server listens on.
@export var port: int = 21253


func find(instance_id: String, connection: NetwConnection) -> NetwEndpoint:
	if public_host.is_empty():
		push_warning(
			"DedicatedDiscordRendezvous has no public_host. Set "
			+ "DedicatedDiscordRendezvous.public_host to the host that "
			+ "serves the activity.",
		)
		return null
	return connection.add(
		&"WebSocketMultiplayerPeer",
		"wss://%s/?instance=%s" % [public_host, instance_id],
	)
