## Turns a Discord Activity instance id into a [MultiplayerTree] session.
##
## Every participant in one Activity sees the same [code]instance_id[/code]. The
## implementation decides whether that means joining a server room or resolving a
## relay match.
## [codeblock]
## instance_id
## └── connect_session(instance_id, tree, username, join_args)
##     ├── OK
##     └── Error
## [/codeblock]
@abstract
class_name DiscordRendezvous
extends Resource

# How long a provider is given to answer one peer creation, in milliseconds.
const SETUP_TIMEOUT_MS := 20000

# How long a submitted local player is given to reach the roster.
const ADMISSION_TIMEOUT_MS := 15000

## Wires backend-specific seams after [param tree] is available.
##
## Default implementation does nothing.
func bind(_tree: MultiplayerTree) -> void:
	pass


## Connects [param tree] into the session keyed by [param instance_id].
##
## Implementations compose [method bring_up] with [param username] and
## [param join_args]. Returns [constant OK] after the tree is online.
@abstract
func connect_session(
		instance_id: String,
		tree: MultiplayerTree,
		username: StringName,
		join_args: Array,
) -> Error


## Brings [param tree] online by building a peer, staging the local player and
## assigning it, then waits until that player is seated.
##
## The ordinary bring-up, composed here rather than on the session, because a
## session is entered by assigning a peer and publishes no verb that waits.
## [method NetwConnectHandle.create_peer] offers the peer inside the
## completion window, and returning from that window without assigning
## declines the offer, so the preparation and the assignment both happen
## inside it.
## [codeblock]
## create_peer(peer_class, mode, address, settings, completed)
## └── completed(peer, error, detail)
##     ├── session_prepare_join(username, join_args)
##     └── multiplayer_peer = peer
## [/codeblock]
## An empty [param username] hosts without seating a local player, which is
## what a dedicated server wants, and skips the wait.
func bring_up(
		tree: MultiplayerTree,
		mode: NetwMultiplayer.TransportMode,
		address: String,
		username: StringName,
		join_args: Array,
) -> Error:
	var api := tree.api
	var settled: Array[bool] = [false]
	var result: Array[Error] = [OK]

	var completed := func(
			peer: MultiplayerPeer,
			error: Error,
			detail: String,
	) -> void:
		settled[0] = true
		if error != OK or peer == null:
			result[0] = error if error != OK else ERR_CANT_CREATE
			push_error(
				"DiscordRendezvous: %s answered no peer (%s)."
				% [String(tree.peer_class), detail]
			)
			return
		if not String(username).is_empty():
			Netw.join(tree, username, join_args)
		api.multiplayer_peer = peer
		if api.multiplayer_peer != peer:
			result[0] = ERR_CANT_CONNECT

	var ticket := Netw.connection(tree).create_peer(
			tree.peer_class,
			mode,
			address,
			tree.transport_settings,
			completed,
	)
	if not ticket.is_valid():
		push_error(
			"DiscordRendezvous: no transport answers '%s'."
			% [String(tree.peer_class)]
		)
		return ERR_UNCONFIGURED

	var loop := tree.get_tree()
	var deadline := Time.get_ticks_msec() + SETUP_TIMEOUT_MS
	while not settled[0]:
		if loop == null or Time.get_ticks_msec() > deadline:
			Netw.connection(tree).cancel_peer_creation(ticket)
			return ERR_TIMEOUT
		await loop.process_frame

	if result[0] != OK or String(username).is_empty():
		return result[0]
	return await _await_local_admission(tree)


# Waits until the local player this tree submitted is seated in the roster,
# which is the condition a join actually asked for.
func _await_local_admission(tree: MultiplayerTree) -> Error:
	var loop := tree.get_tree()
	if loop == null:
		return ERR_UNCONFIGURED
	var deadline := Time.get_ticks_msec() + ADMISSION_TIMEOUT_MS
	var session: NetwSessionHandle = Netw.session(tree)
	while session.local_participant == null:
		if Time.get_ticks_msec() > deadline:
			return ERR_TIMEOUT
		await loop.process_frame
	return OK
