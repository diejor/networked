class_name SessionRoster
extends RefCounted
## Manages the connected peers, accepted players, and authentication state for a MultiplayerTree.

var _peer_contexts: Dictionary[int, NetwPeerContext] = {}
var _joined_players: Dictionary[int, ResolvedJoin] = {}
var _auth_rejection_reasons: Dictionary[int, String] = {}

## Returns the [NetwPeerContext] for [param peer_id], creating one on first access.
func get_peer_context(peer_id: int) -> NetwPeerContext:
	if peer_id not in _peer_contexts:
		_peer_contexts[peer_id] = NetwPeerContext.new()
	return _peer_contexts[peer_id]


## Returns [code]true[/code] if a [NetwPeerContext] exists for [param peer_id].
func has_peer_context(peer_id: int) -> bool:
	return _peer_contexts.has(peer_id)

## Returns accepted player join data known by this peer.
func get_joined_players() -> Array[ResolvedJoin]:
	var players: Array[ResolvedJoin] = []
	for rj: ResolvedJoin in _joined_players.values():
		players.append(rj)
	return players

## Returns the accepted player data for [param peer_id], or [code]null[/code].
func get_joined_player(peer_id: int) -> ResolvedJoin:
	return _joined_players.get(peer_id) as ResolvedJoin

## Stores resolved join data. Returns true if it was newly added.
func remember_joined_player(rj: ResolvedJoin) -> bool:
	if _joined_players.has(rj.peer_id):
		return false
	
	_joined_players[rj.peer_id] = rj
	return true

## Serializes the locally known accepted player roster.
func serialize_joined_players() -> Array[PackedByteArray]:
	var payloads: Array[PackedByteArray] = []
	for rj: ResolvedJoin in _joined_players.values():
		payloads.append(rj.serialize())
	return payloads

## Erases a peer from the roster.
func forget_peer(peer_id: int) -> void:
	_peer_contexts.erase(peer_id)
	_joined_players.erase(peer_id)
	_auth_rejection_reasons.erase(peer_id)

## Clears all state.
func clear() -> void:
	_peer_contexts.clear()
	_joined_players.clear()
	_auth_rejection_reasons.clear()

## Sets the authentication rejection reason for a peer.
func set_auth_rejection_reason(peer_id: int, reason: String) -> void:
	_auth_rejection_reasons[peer_id] = reason

## Returns [code]true[/code] if the join should proceed, [code]false[/code]
## if the peer should be rejected due to a username collision.
func resolve_username_collision(rj: ResolvedJoin, existing_players: Array[Node], disconnect_peer: Callable) -> bool:
	var existing_names: Array[StringName] = []
	for player in existing_players:
		var entity := NetwEntity.of(player)
		if entity and not entity.entity_id.is_empty():
			existing_names.append(entity.entity_id)
		else:
			var client := SpawnerComponent.unwrap(player)
			if client:
				existing_names.append(client.entity_id)
			else:
				var parsed := player.name.get_slice("|", 0)
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
			[original_name, new_name]
		)
		rj.username = new_name
		return true
	
	var bucket := get_peer_context(rj.peer_id).get_bucket(
		NetwIdentityBucket
	)
	if bucket.identity:
		var reason := "Username '%s' is already in use" % original_name
		_auth_rejection_reasons[rj.peer_id] = reason
		Netw.dbg.error(
			"Authenticated username collision for '%s'. Rejecting join.",
			[original_name]
		)
		if disconnect_peer.is_valid():
			disconnect_peer.call(rj.peer_id)
		return false
	
	Netw.dbg.warn(
		"Username collision detected for '%s'. "
		+ "Topology nameplates may break.", [original_name],
		func(m): push_warning(m)
	)
	return true
