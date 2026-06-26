## [DiscordRendezvous] that maps a Discord instance to a dedicated WSS room.
##
## There is no shared store and no client-side host election. The client joins
## the dedicated server, and the server groups rooms by [code]instance[/code].
## [codeblock]
## JoinTarget
## ├── address = "wss://<public_host>/?instance=<instance_id>"
## ├── backend = WebSocketBackend
## └── metadata.instance_id = instance_id
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
		payload: JoinPayload,
) -> Error:
	if instance_id.is_empty():
		Netw.dbg.warn("DedicatedDiscordRendezvous: empty instance_id.")
		return ERR_INVALID_PARAMETER
	if public_host.is_empty():
		Netw.dbg.warn("DedicatedDiscordRendezvous: public_host unset.")
		return ERR_UNCONFIGURED
	return await tree.join(_target_for(instance_id), payload)


# Builds a join target with the instance id carried in the query string.
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
