extends RefCounted
## Manages connected peers, accepted participants, and authentication state.

# Per-peer authentication buckets. They hold script-side objects, so they stay
# here while the joins and the name policy live in the engine book.
var _peer_contexts: Dictionary[int, NetwPeerContext] = { }
# The session whose engine book holds the joins, so a native reader answering
# about an accepted join reads the same rows this does rather than a copy.
var _core: NetwMultiplayerCore


func _init(core: NetwMultiplayerCore) -> void:
	_core = core


## Returns the [NetwPeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = NetwPeerContext.new()
	return _peer_contexts[peer_id]


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return _peer_contexts.has(peer_id)


## Returns accepted participant join records known by this peer.
func get_accepted_joins() -> Array[ResolvedJoin]:
	var joins: Array[ResolvedJoin] = []
	joins.assign(_core.session_accepted_joins())
	return joins


## Returns the accepted join record for [param peer_id], or [code]null[/code].
func get_accepted_join(peer_id: int) -> ResolvedJoin:
	return _core.session_accepted_join(peer_id)


## Stores resolved join data. Returns [code]true[/code] if it was newly added,
## or enriched from an argless state to an arg-carrying state.
func remember_accepted_join(rj: ResolvedJoin) -> bool:
	return _core.session_remember_join(rj)


## Erases a peer from the roster.
func forget_peer(peer_id: int) -> void:
	_peer_contexts.erase(peer_id)
	_core.session_forget_peer(peer_id)


## Clears all state.
func clear() -> void:
	_peer_contexts.clear()
	_core.session_clear_roster()


## Sets the authentication rejection reason for a peer.
func set_auth_rejection_reason(peer_id: int, reason: String) -> void:
	_core.session_refuse(peer_id, reason)


## Returns [code]true[/code] if the join should proceed, [code]false[/code]
## if the peer should be rejected due to a username collision.
func resolve_username_collision(
		rj: ResolvedJoin,
		existing_players: Array[NetwEntity],
		disconnect_peer: Callable,
) -> bool:
	var taken := _taken_names(existing_players)
	var original_name := rj.username
	match _core.session_name_verdict(
		original_name,
		taken,
		rj.is_debug,
		_has_identity(rj.peer_id),
	):
		NetwMultiplayerCore.NAME_RENAME:
			var new_name := _core.session_free_name(original_name, taken)
			Netw.dbg.info(
				"Debug name collision: renaming %s to %s",
				[original_name, new_name],
			)
			rj.username = new_name
			return true
		NetwMultiplayerCore.NAME_REFUSE:
			var reason := "Username '%s' is already in use" % original_name
			_core.session_refuse(rj.peer_id, reason)
			Netw.dbg.error(
				"Authenticated username collision for '%s'. Rejecting join.",
				[original_name],
			)
			if disconnect_peer.is_valid():
				disconnect_peer.call(rj.peer_id)
			return false
	if original_name in taken:
		Netw.dbg.warn(
			"Username collision detected for '%s'. "
			+ "Topology nameplates may break.",
			[original_name],
			func(m): push_warning(m)
		)
	return true


# Whether this peer authenticated. Read without creating a context, because a
# peer that never reached authentication has none, and creating one here would
# put every joining peer in a book that answers who authenticated.
func _has_identity(peer_id: int) -> bool:
	if not has_peer_context(peer_id):
		return false
	return get_peer_context(peer_id).get_bucket(NetwIdentityBucket).identity != null


# The names live players already hold: the entity id when one is stamped, else
# the leading slice of the node name, which is where a nameplate reads it from.
func _taken_names(existing_players: Array[NetwEntity]) -> PackedStringArray:
	var taken := PackedStringArray()
	for entity in existing_players:
		if entity == null:
			continue
		if not entity.entity_id.is_empty():
			taken.append(String(entity.entity_id))
			continue
		var parsed := entity.owner.name.get_slice("|", 0)
		if not parsed.is_empty():
			taken.append(parsed)
	return taken
