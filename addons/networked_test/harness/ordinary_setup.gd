## Prepares a player and hosts or joins through a [MultiplayerTree]'s own
## transport, for fixtures whose backend cannot build a peer by itself.
##
## A test that already knows how to build its peer assigns it directly and
## needs nothing here.
## [codeblock]
## var settled := await NetwOrdinarySetup.connect_through(
##         tree,
##         NetwConnection.TRANSPORT_MODE_CLIENT,
##         room,
##         { },
##         &"valeria",
## )
## assert(settled.error == OK)
## [/codeblock]
## The transport is named by [member MultiplayerTree.peer_class], and an empty
## username connects without seating a local player, which is what a dedicated
## host wants.
class_name NetwOrdinarySetup
extends RefCounted

## What one [method NetwOrdinarySetup.connect_through] settled on.
class Outcome:
	## [constant @GlobalScope.OK] once the session is online, otherwise the
	## error [signal NetwConnection.failed] reported.
	var error: Error = OK

	## The reason that came with [member error].
	var detail: String = ""

	## The peer the session went online with, or [code]null[/code].
	var peer: MultiplayerPeer


## Prepares [param username], then hosts or joins [param address] with
## [method NetwConnection.host] or [method NetwConnection.join], and settles
## once the session is online or the attempt failed.
static func connect_through(
		tree: MultiplayerTree,
		mode: NetwConnection.TransportMode,
		address: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array = [],
) -> Outcome:
	var outcome := Outcome.new()
	var connection: NetwConnection = Netw.session(tree).connection
	if not String(username).is_empty():
		tree.api.session_prepare_join(username, join_args)
	var attempt: NetwPromise
	if mode == NetwConnection.TRANSPORT_MODE_HOST:
		attempt = connection.host(tree.peer_class, settings)
	else:
		var endpoint := connection.add(tree.peer_class, address)
		attempt = connection.join(endpoint, settings)
	if not attempt.is_settled:
		await attempt.wait()
	if attempt.is_failed:
		outcome.error = attempt.code
		outcome.detail = attempt.detail
		return outcome
	outcome.peer = tree.api.multiplayer_peer
	return outcome
