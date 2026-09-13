## Composes provider creation, player preparation and peer assignment for
## fixtures whose backend cannot build a peer by itself.
##
## A test that already knows how to build its peer assigns it directly and
## needs nothing here. This exists for a provider-backed backend, where the
## peer arrives through [method NetwConnectHandle.create_peer] and the
## assignment window is the completion callback itself.
## [codeblock]
## var settled := await NetwOrdinarySetup.connect_through(
##         tree,
##         NetwMultiplayer.TRANSPORT_MODE_CLIENT,
##         room,
##         { },
##         &"valeria",
## )
## assert(settled.error == OK)
## [/codeblock]
## The transport is named by [member MultiplayerTree.peer_class], the settings
## passed through are the provider's own, and an empty username assigns
## without seating a local player, which is what a dedicated host wants.
class_name NetwOrdinarySetup
extends RefCounted

## How long [method connect_through] waits for the provider, in milliseconds.
## It is the fixture's own patience, not a session deadline.
const CREATION_TIMEOUT_MS := 20000


## What one [method NetwOrdinarySetup.connect_through] settled on.
class Outcome:
	## The provider's error, or [constant @GlobalScope.ERR_CANT_CONNECT] when
	## the session refused the peer it built.
	var error: Error = OK

	## The provider's own explanation of [member error].
	var detail: String = ""

	## The peer that was assigned, or [code]null[/code] when none was.
	var peer: MultiplayerPeer


## Builds a peer for [param tree] through its provider, prepares
## [param username], and assigns the peer.
##
## Preparation and assignment both happen inside the creation callback, which
## is the only window the seam offers: returning from it without assigning
## declines the offer and closes the peer.
static func connect_through(
		tree: MultiplayerTree,
		mode: NetwMultiplayer.TransportMode,
		address: String,
		settings: Dictionary,
		username: StringName,
		join_args: Array = [],
) -> Outcome:
	var outcome := Outcome.new()
	var api := tree.api
	var settled := [false]

	var completed := func(
			peer: MultiplayerPeer,
			error: Error,
			detail: String,
	) -> void:
		settled[0] = true
		outcome.error = error
		outcome.detail = detail
		if error != OK or peer == null:
			return
		if not String(username).is_empty():
			api.session_prepare_join(username, join_args)
		api.multiplayer_peer = peer
		if api.multiplayer_peer != peer:
			outcome.error = ERR_CANT_CONNECT
			outcome.detail = "the session refused the created peer."
			return
		outcome.peer = peer

	var ticket := Netw.connection(tree).create_peer(
			tree.peer_class,
			mode,
			address,
			settings,
			completed,
	)
	if not ticket.is_valid():
		outcome.error = ERR_UNCONFIGURED
		outcome.detail = "no provider answers %s." % tree.peer_class
		return outcome

	var deadline := Time.get_ticks_msec() + CREATION_TIMEOUT_MS
	var scene_tree := tree.get_tree()
	while not settled[0]:
		if Time.get_ticks_msec() > deadline or scene_tree == null:
			Netw.connection(tree).cancel_peer_creation(ticket)
			outcome.error = ERR_TIMEOUT
			outcome.detail = "the provider never answered."
			return outcome
		await scene_tree.process_frame
	return outcome
