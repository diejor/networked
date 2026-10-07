## Finds the server an Activity instance plays on.
##
## [DiscordActivityService] joins what [method find] returns, and hosts when it
## returns [code]null[/code] and [method host_transport] names a transport.
## [codeblock]
## var endpoint := await rendezvous.find(instance_id, connection)
## if endpoint != null:
##     connection.join(endpoint)
## else:
##     connection.host(rendezvous.host_transport(), rendezvous.host_settings())
## [/codeblock]
@abstract
class_name DiscordRendezvous
extends Resource

## The server of [param instance_id], or [code]null[/code] when nobody hosts it.
@abstract func find(
		instance_id: String,
		connection: NetwConnection,
) -> NetwEndpoint


## The transport to host with. Empty means this rendezvous never hosts.
func host_transport() -> StringName:
	return &""


func host_settings() -> Dictionary:
	return { }


## Called after hosting. Returns the endpoint of a host that won a race against
## this one, or [code]null[/code] when this host stays.
func claim(
		_instance_id: String,
		_connection: NetwConnection,
) -> NetwEndpoint:
	return null


## Called once the service enters [param session].
func bind(_session: NetwSession) -> void:
	pass
