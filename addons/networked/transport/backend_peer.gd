## Abstract transport contract for [MultiplayerTree].
##
## [MultiplayerTree] owns [member MultiplayerTree.api] and calls this resource
## to create, poll, probe, and reset the active [MultiplayerPeer].
## [codeblock]
## func create_host_peer(tree: MultiplayerTree) -> MultiplayerPeer:
##     var peer := ENetMultiplayerPeer.new()
##     peer.create_server(port)
##     return peer
##
## func create_join_peer(
##     tree: MultiplayerTree, address: String, username: String = ""
## ) -> MultiplayerPeer:
##     var peer := ENetMultiplayerPeer.new()
##     peer.create_client(address, port)
##     return peer
## [/codeblock]
@tool
@abstract
class_name BackendPeer
extends Resource



@export_group("Lag Simulation")
## Enables [LaggyMultiplayerPeer] wrapping in [method wrap_peer].
@export var simulate_lag: bool = false
## Minimum simulated packet delay in seconds.
@export_range(
	0.0, 1.0, 0.001, "or_greater", "suffix:s"
) var lag_min_delay: float = 0.1
## Maximum simulated packet delay in seconds.
@export_range(
	0.0, 1.0, 0.001, "or_greater", "suffix:s"
) var lag_max_delay: float = 0.1
## Simulated packet loss ratio.
@export_range(0.0, 1.0, 0.01) var lag_packet_loss: float = 0.0


## Wraps [param base_peer] with [LaggyMultiplayerPeer] when enabled.
##
## Dynamic construction keeps projects without the extension loadable.
func wrap_peer(base_peer: MultiplayerPeer) -> MultiplayerPeer:
	if not base_peer:
		return null
	if not simulate_lag:
		return base_peer
	
	if not ClassDB.class_exists(&"LaggyMultiplayerPeer"):
		Netw.dbg.warn(
			"Lag simulation is enabled but LaggyMultiplayerPeer is missing.",
			func(m): push_warning(m)
		)
		return base_peer
	
	Netw.dbg.info(
		"Wrapping peer in LaggyMultiplayerPeer "
		+ "(delay: %.1f-%.1f ms, packet loss: %d%%)",
		[
			lag_min_delay * 1000.0,
			lag_max_delay * 1000.0,
			int(lag_packet_loss * 100.0),
		]
	)
	
	var laggy_instance: Object = ClassDB.instantiate(&"LaggyMultiplayerPeer")
	var wrapped_peer: MultiplayerPeer = laggy_instance.call(&"create", base_peer)
	if wrapped_peer:
		wrapped_peer.set(&"delay_minimum", lag_min_delay)
		wrapped_peer.set(&"delay_maximum", lag_max_delay)
		wrapped_peer.set(&"packet_loss", lag_packet_loss)
		return wrapped_peer
	
	return base_peer


## Prepares this backend for [method create_host_peer] or
## [method create_join_peer].
##
## Override to resolve scene services or external handles.
func setup(_tree: MultiplayerTree) -> Error:
	return OK


## Produces a [MultiplayerPeer] in server mode. May [code]await[/code].
##
## Return [code]null[/code] to signal [code]ERR_CANT_CREATE[/code].
## [MultiplayerTree] mounts the returned peer on [member MultiplayerTree.api].
@abstract
func create_host_peer(_tree: MultiplayerTree) -> MultiplayerPeer


## Produces a [MultiplayerPeer] in client mode connecting to [param _address].
##
## May [code]await[/code]. Return [code]null[/code] to signal failure.
@abstract
func create_join_peer(
	_tree: MultiplayerTree, _address: String, _username: String = ""
) -> MultiplayerPeer

## Polls backend state outside [member MultiplayerTree.api].
##
## [MultiplayerTree] polls [member MultiplayerTree.api] separately.
func poll(_dt: float) -> void:
	pass


## Clears backend state before a new session or teardown.
func peer_reset_state() -> void:
	pass


## Returns the address clients should use to join a hosted session.
##
## Override in subclasses that use dynamic addresses or room codes.
func get_join_address() -> String:
	return "localhost"


## Returns [code]true[/code] when [method MultiplayerTree.join_or_host] can
## create an embedded server.
##
## Lobby mediated transports should return [code]false[/code].
func supports_embedded_server() -> bool:
	return true


## Looks up [ServerInfo] for [param _address] without joining the server.
##
## The default result is [method ServerInfoResult.unsupported]. Backends with a
## lightweight connection path can override this with [AuthProbeClient].
## Directory based backends can return cached metadata or keep the default.
## [codeblock]
## # Lightweight connection. Probe the same endpoint that join would use.
## return await AuthProbeClient.new(self).query(address, timeout)
##
## # Directory metadata. Use cached lobby data, or report unsupported.
## return ServerInfoResult.unsupported()
## [/codeblock]
func query_server_info(
	_address: String, _timeout: float = 2.0,
) -> ServerInfoResult:
	return ServerInfoResult.unsupported()


## Returns the [AddressHint] for connect UI fields.
func get_address_hint() -> AddressHint:
	var hint := AddressHint.new()
	hint.label = "Address"
	hint.accepts_empty = true
	return hint


## Copies state after [member MultiplayerTree.backend] duplicates this resource.
##
## Override for shared references that [method Resource.duplicate] would reset.
func copy_from(_source: BackendPeer) -> void:
	pass


## Returns a configured copy of this backend template.
##
## [method Resource.duplicate] resets the shared references that
## [method copy_from] restores, so a bare [method Resource.duplicate] yields a
## half-built instance. This pairs the two so no caller can forget the second
## step.
## [codeblock]
## var inst := template.clone()    # duplicate() + copy_from(template)
## [/codeblock]
func clone() -> BackendPeer:
	var inst := duplicate() as BackendPeer
	inst.copy_from(self)
	return inst


## Returns the display name for this backend.
func get_display_name() -> String:
	return "Generic"
