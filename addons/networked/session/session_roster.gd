extends RefCounted
## Manages connected peers, accepted participants, and authentication state.

var _peer_contexts: Dictionary[int, NetwPeerContext] = { }
var _accepted_joins: Dictionary[int, ResolvedJoin] = { }
var _auth_rejection_reasons: Dictionary[int, String] = { }


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
	for rj: ResolvedJoin in _accepted_joins.values():
		joins.append(rj)
	return joins


## Returns the accepted join record for [param peer_id], or [code]null[/code].
func get_accepted_join(peer_id: int) -> ResolvedJoin:
	return _accepted_joins.get(peer_id) as ResolvedJoin


## Stores resolved join data. Returns [code]true[/code] if it was newly added,
## or enriched from an argless state to an arg-carrying state.
func remember_accepted_join(rj: ResolvedJoin) -> bool:
	if _accepted_joins.has(rj.peer_id):
		var existing := _accepted_joins[rj.peer_id]
		if existing.arg_values.is_empty() and not rj.arg_values.is_empty():
			_accepted_joins[rj.peer_id] = rj
			return true
		return false

	_accepted_joins[rj.peer_id] = rj
	return true


## Serializes the locally known accepted participant roster.
func serialize_accepted_joins() -> Array[PackedByteArray]:
	var payloads: Array[PackedByteArray] = []
	for rj: ResolvedJoin in _accepted_joins.values():
		payloads.append(rj.serialize())
	return payloads


## Erases a peer from the roster.
func forget_peer(peer_id: int) -> void:
	_peer_contexts.erase(peer_id)
	_accepted_joins.erase(peer_id)
	_auth_rejection_reasons.erase(peer_id)


## Clears all state.
func clear() -> void:
	_peer_contexts.clear()
	_accepted_joins.clear()
	_auth_rejection_reasons.clear()


## Sets the authentication rejection reason for a peer.
func set_auth_rejection_reason(peer_id: int, reason: String) -> void:
	_auth_rejection_reasons[peer_id] = reason


## Returns [code]true[/code] if the join should proceed, [code]false[/code]
## if the peer should be rejected due to a username collision.
func resolve_username_collision(
		rj: ResolvedJoin,
		existing_players: Array[NetwEntity],
		disconnect_peer: Callable,
) -> bool:
	var existing_names: Array[StringName] = []
	for entity in existing_players:
		if entity != null:
			if not entity.entity_id.is_empty():
				existing_names.append(entity.entity_id)
			else:
				var parsed := entity.owner.name.get_slice("|", 0)
				if not parsed.is_empty():
					existing_names.append(StringName(parsed))

	var original_name := rj.username
	if not original_name in existing_names:
		return true

	if rj.is_debug:
		var suffix := 1
		var new_name := StringName(str(original_name) + str(suffix))
		while new_name in existing_names:
			suffix += 1
			new_name = StringName(str(original_name) + str(suffix))

		Netw.dbg.info(
			"Debug name collision: renaming %s to %s",
			[original_name, new_name],
		)
		rj.username = new_name
		return true

	var bucket := get_peer_context(rj.peer_id).get_bucket(
		NetwIdentityBucket,
	)
	if bucket.identity:
		var reason := "Username '%s' is already in use" % original_name
		_auth_rejection_reasons[rj.peer_id] = reason
		Netw.dbg.error(
			"Authenticated username collision for '%s'. Rejecting join.",
			[original_name],
		)
		if disconnect_peer.is_valid():
			disconnect_peer.call(rj.peer_id)
		return false

	Netw.dbg.warn(
		"Username collision detected for '%s'. "
		+ "Topology nameplates may break.",
		[original_name],
		func(m): push_warning(m)
	)
	return true
