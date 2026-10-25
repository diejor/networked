## The transport a [NetwTransferContract] judges, subclassed once per transport.
##
## Every endpoint declares
## [code]server_relabelled(successor_was: int, former_now: int)[/code] and
## installs a transfer from inside its own [method MultiplayerPeer.poll].
## [codeblock]
## class MyDriver extends NetwTransferDriver:
##     func make_endpoints(count: int) -> Array[MultiplayerPeer]:
##         return MyPeer.mesh(count)
##
##     func lose_server() -> void:
##         MyPeer.crash_server()
##
## var failures := NetwTransferContract.new(MyDriver.new(), node).run()
## [/codeblock]
@abstract
class_name NetwTransferDriver
extends RefCounted

## Builds [param count] connected endpoints linked any to any, the server
## first.
@abstract func make_endpoints(count: int) -> Array[MultiplayerPeer]


## Makes the server vanish the way a crash would.
@abstract func lose_server() -> void


## Hands the server role to [param peer_id], or answers
## [constant @GlobalScope.ERR_UNAVAILABLE] when the transport moves it only on
## loss.
func hand_off(server: MultiplayerPeer, peer_id: int) -> Error:
	if not server.has_method(&"transfer_server"):
		return ERR_UNAVAILABLE
	return server.call(&"transfer_server", peer_id)


## A peer joining while the survivors install, or [code]null[/code] when the
## transport cannot express one.
func join() -> MultiplayerPeer:
	return null


## Releases what [method make_endpoints] built.
func close() -> void:
	pass
