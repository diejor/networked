extends RefCounted

## Turns a Nakama match into a [NakamaRelayPeer].
##
## Gives each player in the match a Godot peer id, and forwards packets between
## the Nakama socket and [member multiplayer_peer].

enum MatchState {
	DISCONNECTED,
	JOINING,
	CONNECTED,
	SOCKET_CLOSED,
}

enum MetaMessageType {
	CLAIM_HOST,
	ASSIGN_PEER_ID,
	TRANSFER,
}

const _TERM_STRIDE := 1000
const _SERVER_MARK := 1

# Read-only variables.
var _nakama_socket
var nakama_socket:
	get:
		return _nakama_socket
	set(_v):
		pass
var _match_state: MatchState = MatchState.DISCONNECTED
var match_state: MatchState:
	get:
		return _match_state
	set(_v):
		pass
var _match_id := ''
var match_id: String:
	get:
		return _match_id
	set(_v):
		pass
var _multiplayer_peer: NakamaRelayPeer = NakamaRelayPeer.new()
var multiplayer_peer: NakamaRelayPeer:
	get:
		return _multiplayer_peer
	set(_v):
		pass

# Configuration that can be set by the developer.
var meta_op_code: int = 9001
var rpc_op_code: int = 9002

## When the host leaves, the peers left elect a new server and keep the match.
var migrate_on_host_loss := false

## Picks the new server from the surviving peer ids, and must answer the same
## on every peer. When empty, the lowest id takes over.
var migration_policy := Callable()

# Internal variables.
var _my_session_id: String
var _my_peer_id: int = 0
var _id_map := { }
var _users := { }
var _matchmaker_ticket := ''
var _term := 0
var _scheduled_term := 0
var _held: Array = []
var _deferred_leaves: Array = []
var _used_ids := { }
var _warned_term := 0
var _elders := { }


class User extends RefCounted:
	var presence
	var peer_id: int = 0


	func _init(p_presence) -> void:
		presence = p_presence

## Emitted when creating or joining a match fails.
signal match_join_error(exception)

## Emitted after [member multiplayer_peer] is ready for the joined match.
signal match_joined()


func _set_readonly(_value) -> void:
	pass


func _init(p_nakama_socket) -> void:
	_nakama_socket = p_nakama_socket
	_nakama_socket.received_match_presence.connect(self._on_nakama_socket_received_match_presence)
	_nakama_socket.received_matchmaker_matched.connect(
		self._on_nakama_socket_received_matchmaker_matched,
	)
	_nakama_socket.received_match_state.connect(self._on_nakama_socket_received_match_state)
	_nakama_socket.closed.connect(self._on_nakama_socket_closed)

	_multiplayer_peer.packet_generated.connect(self._on_multiplayer_peer_packet_generated)
	_multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)
	_multiplayer_peer._transfer_handler = _transfer_server


## Creates a new relay match and claims host peer id [code]1[/code].
func create_match() -> void:
	if _match_state != MatchState.DISCONNECTED:
		push_error(
			"The Nakama relay was asked to create a match while %s. Leave "
			% MatchState.keys()[_match_state].to_lower()
			+ "the current match first.",
		)
		return

	_match_state = MatchState.JOINING
	multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)

	var res = await _nakama_socket.create_match_async()
	if res.is_exception():
		match_join_error.emit(res.get_exception())
		leave()
		return

	_setup_match(res)
	_setup_host()


## Joins the relay match named by [param p_match_id].
func join_match(p_match_id: String) -> void:
	if _match_state != MatchState.DISCONNECTED:
		push_error(
			"The Nakama relay was asked to join a match while %s. Leave "
			% MatchState.keys()[_match_state].to_lower()
			+ "the current match first.",
		)
		return

	_match_state = MatchState.JOINING
	multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)

	var res = await _nakama_socket.join_match_async(p_match_id)
	if res.is_exception():
		match_join_error.emit(res.get_exception())
		leave()
		return

	_setup_match(res)


## Creates or joins the named Nakama match.
##
## The first peer to arrive claims host peer id [code]1[/code], and so does a
## peer once everyone who arrived before it left without serving.
func join_named_match(_match_name: String) -> void:
	if _match_state != MatchState.DISCONNECTED:
		push_error(
			"The Nakama relay was asked to join a room while %s. Leave "
			% MatchState.keys()[_match_state].to_lower()
			+ "the current match first.",
		)
		return

	_match_state = MatchState.JOINING
	multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)

	var res = await _nakama_socket.create_match_async(_match_name)
	if res.is_exception():
		match_join_error.emit(res.get_exception())
		leave()
		return

	_setup_match(res)
	if res.size == 0 or (res.size == 1 and res.presences.size() == 0):
		_setup_host()
		return
	for presence in res.presences:
		if presence.session_id != _my_session_id:
			_elders[presence.session_id] = true


## Starts Nakama matchmaking with [param ticket].
func start_matchmaking(ticket) -> void:
	if _match_state != MatchState.DISCONNECTED:
		push_error(
			"The Nakama relay was asked to matchmake while %s. Leave "
			% MatchState.keys()[_match_state].to_lower()
			+ "the current match first.",
		)
		return
	if ticket.is_exception():
		push_error(
			"The Nakama matchmaker ticket failed, so matchmaking did not "
			+ "start. Check the ticket for an exception first.",
		)
		return

	_match_state = MatchState.JOINING
	multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)

	_matchmaker_ticket = ticket.ticket


## Joins the match a matchmaker event [param matched] found, as if this
## bridge had queued its ticket.
func join_matched(matched) -> void:
	if _match_state != MatchState.DISCONNECTED:
		push_error(
			"The Nakama relay was asked to join a matched game while %s. "
			% MatchState.keys()[_match_state].to_lower()
			+ "Leave the current match first.",
		)
		return

	_match_state = MatchState.JOINING
	multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTING)
	_matchmaker_ticket = matched.ticket
	_on_nakama_socket_received_matchmaker_matched(matched)


func _on_nakama_socket_received_matchmaker_matched(matchmaker_matched) -> void:
	if _matchmaker_ticket != matchmaker_matched.ticket:
		return

	# Get a list of sorted session ids.
	var session_ids := []
	for matchmaker_user in matchmaker_matched.users:
		session_ids.append(matchmaker_user.presence.session_id)
	session_ids.sort()

	var res = await _nakama_socket.join_matched_async(matchmaker_matched)
	if res.is_exception():
		match_join_error.emit(res.get_exception())
		leave()
		return

	_setup_match(res)

	# If our session is the first alphabetically, then we'll be the host.
	if _my_session_id == session_ids[0]:
		_setup_host()

		# Add all of the existing peers.
		for presence in res.presences:
			if presence.session_id != _my_session_id:
				_host_add_peer(presence)


func _on_nakama_socket_closed() -> void:
	_match_state = MatchState.SOCKET_CLOSED
	_cleanup()


## Returns the Nakama presence mapped to [param peer_id].
func get_user_presence_for_peer(peer_id: int):
	var session_id = _id_map.get(peer_id)
	if session_id == null:
		return null
	var user = _users.get(session_id)
	if user == null:
		return null
	return user.presence


## Leaves the match and resets the relay peer.
func leave() -> void:
	if _match_state == MatchState.DISCONNECTED:
		return
	_match_state = MatchState.DISCONNECTED

	_cleanup()


func _cleanup() -> void:
	for peer_id in _id_map:
		_multiplayer_peer.unregister_peer(peer_id)

	_match_id = ''
	_matchmaker_ticket = ''
	_my_session_id = ''
	_my_peer_id = 0
	_id_map.clear()
	_users.clear()
	_term = 0
	_scheduled_term = 0
	_held.clear()
	_deferred_leaves.clear()
	_used_ids.clear()
	_warned_term = 0
	_elders.clear()

	_multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_DISCONNECTED)


func _setup_match(res) -> void:
	_match_id = res.match_id
	_my_session_id = res.self_user.session_id

	_users[_my_session_id] = User.new(res.self_user)

	for presence in res.presences:
		if not _users.has(presence.session_id):
			_users[presence.session_id] = User.new(presence)


func _setup_host() -> void:
	# Claim id 1 and start the match.
	_my_peer_id = 1
	_map_id_to_session(1, _my_session_id)
	_match_state = MatchState.CONNECTED
	_multiplayer_peer.initialize(_my_peer_id)
	match_joined.emit()


func _generate_id(session_id: String) -> int:
	# Peer ids can only be positive 32-bit signed integers.
	var peer_id: int = session_id.hash() & 0x7FFFFFFF

	# If this peer id is already taken, try to find another.
	while peer_id <= 1 or _used_ids.has(peer_id):
		peer_id += 1
		if peer_id > 0x7FFFFFFF or peer_id <= 0:
			peer_id = randi() & 0x7FFFFFFF

	return peer_id


func _fresh_id() -> int:
	var peer_id: int = randi() & 0x7FFFFFFF
	while peer_id <= 1 or _used_ids.has(peer_id):
		peer_id = randi() & 0x7FFFFFFF
	_used_ids[peer_id] = true
	return peer_id


func _map_id_to_session(peer_id: int, session_id: String) -> void:
	if not _users.has(session_id):
		_users[session_id] = User.new(null)
	_id_map[peer_id] = session_id
	_users[session_id].peer_id = peer_id
	_used_ids[peer_id] = true


func _host_add_peer(presence) -> void:
	var peer_id = _generate_id(presence.session_id)
	_map_id_to_session(peer_id, presence.session_id)

	# Tell them we are the host.
	_nakama_socket.send_match_state_async(
		_match_id,
		meta_op_code,
		JSON.stringify(
			{
				type = MetaMessageType.CLAIM_HOST,
				term = _term,
			},
		),
		[presence],
	)

	# Tell them about all the other connected peers.
	for other_peer_id in _id_map:
		var other_session_id = _id_map[other_peer_id]
		if other_session_id == presence.session_id or other_session_id == _my_session_id:
			continue
		_nakama_socket.send_match_state_async(
			_match_id,
			meta_op_code,
			JSON.stringify(
				{
					type = MetaMessageType.ASSIGN_PEER_ID,
					term = _term,
					session_id = other_session_id,
					peer_id = other_peer_id,
				},
			),
			[presence],
		)

	# Assign them a peer_id (tell everyone about it).
	_nakama_socket.send_match_state_async(
		_match_id,
		meta_op_code,
		JSON.stringify(
			{
				type = MetaMessageType.ASSIGN_PEER_ID,
				term = _term,
				session_id = presence.session_id,
				peer_id = peer_id,
			},
		),
	)

	_multiplayer_peer.register_peer(peer_id)


func _on_nakama_socket_received_match_presence(event) -> void:
	if _match_state == MatchState.DISCONNECTED:
		return
	if event.match_id != _match_id:
		return

	for presence in event.joins:
		if not _users.has(presence.session_id):
			_users[presence.session_id] = User.new(presence)
		elif _users[presence.session_id].presence == null:
			_users[presence.session_id].presence = presence
		if _users[presence.session_id].peer_id != 0:
			_multiplayer_peer.register_peer(_users[presence.session_id].peer_id)

		# If we are the host, and they don't yet have a peer id, then let's
		# generate a new id for them and send all the necessary messages.
		if _my_peer_id == 1 and _users[presence.session_id].peer_id == 0:
			_host_add_peer(presence)

	for presence in event.leaves:
		if _scheduled_term > _term:
			_deferred_leaves.append(presence)
		else:
			_on_presence_left(presence)


func _on_presence_left(presence) -> void:
	if not _users.has(presence.session_id):
		return
	if presence.session_id == _id_map.get(1, "") and _migrates():
		_elect_successor()
		return

	var peer_id = _users[presence.session_id].peer_id

	_multiplayer_peer.unregister_peer(peer_id)

	_users.erase(presence.session_id)
	_id_map.erase(peer_id)

	if _elders.erase(presence.session_id) and _elders.is_empty():
		_setup_host()
		_host_unassigned_presences()


func _migrates() -> bool:
	return migrate_on_host_loss and _my_peer_id > 1


func _elect_successor() -> void:
	var survivors: Array[int] = []
	for peer_id: int in _id_map:
		if peer_id != 1:
			survivors.append(peer_id)
	survivors.sort()
	var successor := _pick_successor(survivors)
	_schedule_install(_id_map[successor], 0, _term + 1)


func _pick_successor(survivors: Array[int]) -> int:
	if not migration_policy.is_valid():
		return survivors[0]
	var picked: int = migration_policy.call(survivors.duplicate())
	if survivors.has(picked):
		return picked
	push_warning(
		(
				"The Nakama migration policy answered %d, which is not a "
				+ "surviving peer. Return one of the IDs it is given."
		) % picked,
	)
	return survivors[0]


func _transfer_server(peer_id: int) -> Error:
	if _match_state != MatchState.CONNECTED or _my_peer_id != 1:
		return ERR_UNAUTHORIZED
	if peer_id == 1 or not _id_map.has(peer_id):
		return ERR_INVALID_PARAMETER
	if _scheduled_term > _term:
		return ERR_BUSY
	var successor_session: String = _id_map[peer_id]
	var former := _fresh_id()
	var term := _term + 1
	_nakama_socket.send_match_state_async(
		_match_id,
		meta_op_code,
		JSON.stringify(
			{
				type = MetaMessageType.TRANSFER,
				term = term,
				successor_session = successor_session,
				former = former,
			},
		),
	)
	_schedule_install(successor_session, former, term)
	return OK


func _receive_transfer(data, content: Dictionary) -> void:
	if data.presence.session_id != _id_map.get(1, ""):
		push_error(
			"Nakama user %s sent a server transfer but is not the host, so it "
			% data.presence.session_id
			+ "was ignored.",
		)
		return
	var term := int(content.get("term", 0))
	if term <= maxi(_term, _scheduled_term):
		return
	_schedule_install(
		String(content.get("successor_session", "")),
		int(content.get("former", 0)),
		term,
	)


func _schedule_install(successor_session: String, former: int, term: int) -> void:
	_scheduled_term = term
	_multiplayer_peer.schedule_relabel(
		_install.bind(successor_session, former, term),
	)


func _install(successor_session: String, former: int, term: int) -> Array:
	if term <= _term:
		return []
	if not _users.has(successor_session) or not _id_map.has(1):
		_term = term
		_multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_DISCONNECTED)
		return []
	var host_session: String = _id_map[1]
	var successor_was: int = _users[successor_session].peer_id
	_id_map.erase(successor_was)
	_id_map.erase(1)
	if former == 0:
		_users.erase(host_session)
	else:
		_map_id_to_session(former, host_session)
	_map_id_to_session(1, successor_session)
	_my_peer_id = _users[_my_session_id].peer_id
	_term = term
	var peers: Array[int] = []
	for peer_id: int in _id_map:
		if peer_id != _my_peer_id:
			peers.append(peer_id)
	_multiplayer_peer.relabel(_my_peer_id, peers, term)
	if _my_peer_id == 1:
		_host_unassigned_presences()
	_release_held()
	_release_deferred_leaves()
	return [successor_was, former]


func _host_unassigned_presences() -> void:
	for session_id: String in _users:
		var user: User = _users[session_id]
		if user.peer_id == 0 and user.presence != null:
			_host_add_peer(user.presence)


func _release_held() -> void:
	var held := _held
	_held = []
	for data in held:
		_on_nakama_socket_received_match_state(data)


func _release_deferred_leaves() -> void:
	var leaves := _deferred_leaves
	_deferred_leaves = []
	for presence in leaves:
		_on_presence_left(presence)


func _parse_json(data: String):
	var json = JSON.new()
	if json.parse(data) != OK:
		return null
	var content = json.get_data()
	if not content is Dictionary:
		return null
	return content


func _on_nakama_socket_received_match_state(data) -> void:
	if _match_state == MatchState.DISCONNECTED:
		return
	if data.match_id != _match_id:
		return

	if data.op_code == meta_op_code:
		var content = _parse_json(data.data)
		if content == null:
			return
		var type = content['type']
		var term := int(content.get("term", 0))

		if type == MetaMessageType.TRANSFER:
			_receive_transfer(data, content)
			return

		if type == MetaMessageType.CLAIM_HOST:
			if _id_map.has(1):
				# @todo Can we mediate this dispute?
				push_error(
					"Nakama user %s claimed to be host, but user %s already is, so "
					% [data.presence.session_id, _id_map[1]]
					+ "the claim was ignored.",
				)
			else:
				_elders.clear()
				_map_id_to_session(1, data.presence.session_id)
				_term = term
				_scheduled_term = term
				_release_held()
			return

		if term < _term:
			return
		if term > _term:
			_held.append(data)
			return

		# Ensure that any meta messages are coming from the host!
		if data.presence.session_id != _id_map.get(1, ""):
			return

		if type == MetaMessageType.ASSIGN_PEER_ID:
			var session_id = content['session_id']
			var peer_id = content['peer_id']

			if _users.has(session_id) and _users[session_id].peer_id != 0:
				push_error(
					(
							"The host gave peer id %s to Nakama user %s, who "
							+ "already has peer id %s, so it was ignored."
					) % [
						peer_id,
						session_id,
						_users[session_id].peer_id,
					],
				)
				return

			_map_id_to_session(peer_id, session_id)

			if _my_session_id == session_id:
				_match_state = MatchState.CONNECTED
				_my_peer_id = peer_id
				_multiplayer_peer.initialize(peer_id)
				_multiplayer_peer.set_connection_status(MultiplayerPeer.CONNECTION_CONNECTED)
				_multiplayer_peer.register_peer(1)
				match_joined.emit()
			else:
				_multiplayer_peer.register_peer(peer_id)
		else:
			_nakama_socket.logger.error("Received meta message with unknown type: %s" % type)
	elif _is_rpc_op(data.op_code):
		_receive_rpc(data)


func _is_rpc_op(op_code: int) -> bool:
	return (
			op_code >= rpc_op_code
			and (op_code - rpc_op_code) % _TERM_STRIDE <= _SERVER_MARK
	)


func _receive_rpc(data) -> void:
	var term: int = (data.op_code - rpc_op_code) / _TERM_STRIDE
	if term < _term:
		return
	if term > _term:
		_held.append(data)
		return
	var from_session_id: String = data.presence.session_id
	if not _users.has(from_session_id) or _users[from_session_id].peer_id == 0:
		push_error(
			"A packet from Nakama user %s arrived before it had a peer id, "
			% data.presence.session_id
			+ "so it was dropped.",
		)
		return
	var from_peer_id: int = _users[from_session_id].peer_id
	if (data.op_code - rpc_op_code) % _TERM_STRIDE == _SERVER_MARK:
		_check_server(from_session_id, from_peer_id)
	_multiplayer_peer.deliver_packet(data.binary_data, from_peer_id, term)


func _check_server(from_session_id: String, from_peer_id: int) -> void:
	if _term == 0 or _warned_term == _term:
		return
	if from_session_id == _id_map.get(1, ""):
		return
	_warned_term = _term
	push_warning(
		(
				"Peer %d sends as the server, but this peer elected another. "
				+ "Make migration_policy answer the same on every peer."
		) % from_peer_id,
	)


func _on_multiplayer_peer_packet_generated(peer_id: int, buffer: PackedByteArray) -> void:
	if match_state == MatchState.CONNECTED:
		var target_presences = null
		if peer_id > 0:
			if not _id_map.has(peer_id):
				if not _used_ids.has(peer_id):
					push_error("A packet for unknown peer %s was dropped." % peer_id)
				return
			target_presences = [_users[_id_map[peer_id]].presence]
		var op_code := rpc_op_code + _term * _TERM_STRIDE
		if _my_peer_id == 1:
			op_code += _SERVER_MARK
		_nakama_socket.send_match_state_raw_async(_match_id, op_code, buffer, target_presences)
	else:
		push_error(
			"A packet was sent before the Nakama relay connected, so it was "
			+ "dropped.",
		)
