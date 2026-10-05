## The laws a [MultiplayerPeer] that hands the server role on must meet.
##
## Each scenario mounts one [SceneMultiplayer] per endpoint a
## [NetwTransferDriver] builds, and each broken law is reported as a
## [code]"law x scenario: detail"[/code] line.
## [codeblock]
## var contract := NetwTransferContract.new(MyDriver.new(), node)
## var failures := contract.run()
## assert(failures.is_empty(), "\n".join(failures))
## [/codeblock]
## [br][br]
## - [b]C1[/b] no endpoint relays, and every survivor reaches every other.
## [br]- [b]C2[/b] every survivor installs the same transfer once.
## [br]- [b]C3[/b] an endpoint answers its new id inside the signal.
## [br]- [b]C4[/b] a packet is read only under its sender's transfers.
## [br]- [b]C5[/b] no peer edge is emitted for a relabelled id.
## [br]- [b]C6[/b] every survivor stays connected.
## [br]- [b]C7[/b] no id is handed out twice.
## [br]- [b]C8[/b] a joiner is refused, or completes under the new labels.
## [br]- [b]inner[/b] a [SceneMultiplayer] reset inside the signal stays whole.
class_name NetwTransferContract
extends RefCounted

## Loses the server through [method NetwTransferDriver.lose_server].
const SCENARIO_LOST := &"lost"

## Hands the role to the second client through
## [method NetwTransferDriver.hand_off].
const SCENARIO_HANDED_ON := &"handed_on"

const _CLIENTS := 3
const _CONNECT_POLLS := 32
const _PRE_ROUNDS := 4
const _MID_ROUNDS := 8
const _SETTLE_POLLS := 6
const _POST_ROUNDS := 4
const _POLLS_PER_ROUND := 4
const _JOIN_ROUND := 1
const _INSTALL_ROUNDS: Array[int] = [0, 1, 2, 4]

const _PHASE_ALPHA := "alpha"
const _PHASE_PRE := "pre"
const _PHASE_MID := "mid"
const _PHASE_POST := "post"

const _KIND_RPC := "rpc"
const _KIND_BYTES := "bytes"

var driver: NetwTransferDriver

## A node inside the [SceneTree] that every endpoint mounts under.
var parent: Node

var failures: Array[String] = []

## Every [code]"law x scenario"[/code] label the last [method run] judged.
var judged: Array[String] = []

## Every engine error the last [method run] logged.
var logged_errors: Array[String] = []


func _init(p_driver: NetwTransferDriver, p_parent: Node) -> void:
	driver = p_driver
	parent = p_parent


## Runs both scenarios and returns [member failures].
func run() -> Array[String]:
	failures.clear()
	judged.clear()
	logged_errors.clear()
	for scenario: StringName in [SCENARIO_LOST, SCENARIO_HANDED_ON]:
		var evidence := _drive(scenario)
		if evidence != null:
			_judge(evidence)
	return failures


## The law names in [member failures], each once.
func failed_laws() -> PackedStringArray:
	var names := PackedStringArray()
	for failure in failures:
		var law := failure.get_slice(" x ", 0)
		if not names.has(law):
			names.append(law)
	return names


func _drive(scenario: StringName) -> _Evidence:
	var evidence := _Evidence.new(scenario)
	var counter := _ErrorCounter.new()
	OS.add_logger(counter)
	var closed := _build_world(evidence) and _cross(evidence, counter)
	OS.remove_logger(counter)
	logged_errors.append_array(counter.errors)
	_teardown(evidence)
	return evidence if closed else null


func _judge(evidence: _Evidence) -> void:
	var laws: Dictionary[String, Callable] = {
		"C1": _law_c1_any_to_any,
		"C2": _law_c2_agreement,
		"C3": _law_c3_synchronous,
		"C4": _law_c4_cut,
		"C5": _law_c5_no_peer_edges,
		"C6": _law_c6_status,
		"C7": _law_c7_fresh_ids,
		"inner": _law_inner,
	}
	for law in laws:
		_judged_label(law, evidence)
		laws[law].call(evidence)
	_law_c8_joiners(evidence)


func _law_c1_any_to_any(evidence: _Evidence) -> void:
	for seat in evidence.seats:
		if seat.relay_supported:
			_fail("C1", evidence, "%d supports server relay" % seat.original_id)
	var finals := evidence.final_labels()
	for reader in evidence.survivors():
		for sender in finals:
			var heard := reader.rounds_heard(_KIND_BYTES, sender)
			if sender != reader.final_id and heard != _POST_ROUNDS:
				var detail := "%d heard %d of %d rounds from %d"
				_fail(
					"C1",
					evidence,
					detail % [reader.final_id, heard, _POST_ROUNDS, sender],
				)


func _law_c2_agreement(evidence: _Evidence) -> void:
	var first := evidence.transfer()
	for seat in evidence.survivors():
		var id := seat.original_id
		if seat.relabels.size() != 1:
			var count := seat.relabels.size()
			_fail("C2", evidence, "%d installed %d transfers" % [id, count])
		elif seat.relabels[0] != first:
			var detail := "%d installed %s, another installed %s"
			_fail("C2", evidence, detail % [id, seat.relabels[0], first])
	if first.is_empty():
		return
	var successor_was: int = first[0]
	var former_now: int = first[1]
	if evidence.scenario == SCENARIO_LOST:
		if former_now != 0:
			_fail("C2", evidence, "a lost server came back as %d" % former_now)
		if successor_was == 1 or not evidence.ids_before.has(successor_was):
			_fail("C2", evidence, "%d was elected" % successor_was)
		return
	if successor_was != evidence.requested:
		var detail := "%d took over, %d was asked"
		_fail("C2", evidence, detail % [successor_was, evidence.requested])
	if former_now == 0:
		_fail("C2", evidence, "the old server was dropped on hand off")


func _law_c3_synchronous(evidence: _Evidence) -> void:
	var transfer := evidence.transfer()
	if transfer.is_empty():
		return
	var finals := evidence.final_labels()
	for seat in evidence.survivors():
		if seat.in_signal.is_empty():
			continue
		var inside: Dictionary = seat.in_signal[0]
		var id := seat.original_id
		var expected := _relabelled(id, transfer[0], transfer[1])
		if inside.unique_id != expected:
			var detail := "%d answered %d inside the signal, not %d"
			_fail("C3", evidence, detail % [id, inside.unique_id, expected])
		if inside.is_server != (expected == 1):
			var detail := "%d answered is_server %s inside the signal"
			_fail("C3", evidence, detail % [id, inside.is_server])
		if inside.packet_peer != 0 and not finals.has(inside.packet_peer):
			var detail := "%d named sender %d inside the signal"
			_fail("C3", evidence, detail % [id, inside.packet_peer])
	for read in evidence.reads_after_cut():
		if read.remote != read.claimed:
			var detail := "%d read %s from %d, sent as %d"
			_fail(
				"C3",
				evidence,
				detail % [read.reader, read.phase, read.remote, read.claimed],
			)


func _law_c4_cut(evidence: _Evidence) -> void:
	if evidence.crossings == 0:
		_fail("C4", evidence, "no packet was sent across the cut")
	for read in evidence.reads_after_cut():
		if read.sender_installs != read.reader_installs:
			var detail := (
					"%d read a %s packet %d sent after %d installs, having "
					+ "installed %d"
			)
			_fail(
				"C4",
				evidence,
				detail % [
					read.reader,
					read.phase,
					read.claimed,
					read.sender_installs,
					read.reader_installs,
				],
			)


func _law_c5_no_peer_edges(evidence: _Evidence) -> void:
	var transfer := evidence.transfer()
	if transfer.is_empty():
		return
	var relabelled_ids: Array[int] = [transfer[0], 1]
	if transfer[1] != 0:
		relabelled_ids.append(transfer[1])
	for seat in evidence.survivors():
		for edge in seat.edges:
			if relabelled_ids.has(absi(edge)):
				var kind := "connected" if edge > 0 else "disconnected"
				var detail := "%d saw peer %s %d"
				_fail(
					"C5",
					evidence,
					detail % [seat.original_id, kind, absi(edge)],
				)


func _law_c6_status(evidence: _Evidence) -> void:
	for seat in evidence.survivors():
		for status in seat.status_faults:
			var detail := "%d went to status %d" % [seat.original_id, status]
			_fail("C6", evidence, detail)


func _law_c7_fresh_ids(evidence: _Evidence) -> void:
	var transfer := evidence.transfer()
	if transfer.is_empty():
		return
	var successor_was: int = transfer[0]
	var former_now: int = transfer[1]
	if former_now != 0 and evidence.ids_before.has(former_now):
		var detail := "the old server became %d, already used" % former_now
		_fail("C7", evidence, detail)
	for seat in evidence.live_seats():
		if seat.final_id == successor_was:
			var detail := "%d still answers %d"
			_fail("C7", evidence, detail % [seat.original_id, successor_was])
	var joiner := evidence.joiner
	if joiner == null or joiner.refused:
		return
	var joiner_id := joiner.original_id
	if evidence.ids_before.has(joiner_id) or joiner_id == former_now:
		var detail := "the joiner was handed %d, already used" % joiner_id
		_fail("C7", evidence, detail)


func _law_c8_joiners(evidence: _Evidence) -> void:
	var joiner := evidence.joiner
	if joiner == null:
		return
	_judged_label("C8", evidence)
	if joiner.refused:
		return
	if not joiner.relabels.is_empty():
		_fail("C8", evidence, "the joiner installed %s" % [joiner.relabels])
	var finals := evidence.final_labels()
	for edge in joiner.edges:
		if not finals.has(absi(edge)):
			_fail("C8", evidence, "the joiner saw peer edge %d" % edge)
	if not joiner.edges.has(1):
		_fail("C8", evidence, "the joiner never connected to 1")
	for seat in evidence.survivors():
		if not seat.final_peers.has(joiner.final_id):
			var detail := "%d never admitted the joiner" % seat.final_id
			_fail("C8", evidence, detail)


func _law_inner(evidence: _Evidence) -> void:
	var counts: Dictionary[String, int] = { }
	for error in evidence.errors:
		counts[error] = counts.get(error, 0) + 1
	for error in counts:
		_fail("inner", evidence, "x%d %s" % [counts[error], error])
	var finals := evidence.final_labels()
	for seat in evidence.live_seats():
		var expected := finals.filter(
			func(label: int) -> bool: return label != seat.final_id
		)
		expected.sort()
		var peers := seat.final_peers.duplicate()
		peers.sort()
		if peers != expected:
			var detail := "%d ended with peers %s, not %s"
			_fail("inner", evidence, detail % [seat.final_id, peers, expected])
		for sender in expected:
			var heard := seat.rounds_heard(_KIND_RPC, sender)
			if heard != _POST_ROUNDS:
				var detail := "%d heard %d of %d RPC rounds from %d"
				_fail(
					"inner",
					evidence,
					detail % [seat.final_id, heard, _POST_ROUNDS, sender],
				)


static func _relabelled(label: int, successor_was: int, former_now: int) -> int:
	if label == successor_was:
		return 1
	if label == 1:
		return former_now
	return label


func _build_world(evidence: _Evidence) -> bool:
	var endpoints := driver.make_endpoints(_CLIENTS + 1)
	if endpoints.size() != _CLIENTS + 1:
		var detail := "the driver built %d endpoints, not %d"
		_fail("world", evidence, detail % [endpoints.size(), _CLIENTS + 1])
		return false
	if endpoints[0].get_unique_id() != 1:
		_fail("world", evidence, "the first endpoint is not the server")
		return false
	for endpoint in endpoints:
		if not endpoint.has_signal(&"server_relabelled"):
			_fail("world", evidence, "the peer declares no server_relabelled")
			return false
		evidence.seats.append(_mount(endpoint, false))
	for poll in _CONNECT_POLLS:
		_poll_seats(evidence, -1)
		if evidence.seats.all(
			func(seat: _Seat) -> bool:
				return seat.mp.get_peers().size() == _CLIENTS
		):
			return true
	_fail("world", evidence, "the endpoints never all connected")
	return false


func _cross(evidence: _Evidence, counter: _ErrorCounter) -> bool:
	var pinger: _Seat = evidence.seats[1]
	for label in pinger.mp.get_peers():
		pinger.alpha.rpc_id(label, &"ping", pinger.tag(_PHASE_ALPHA, 0))
	_poll_rounds(evidence, _POLLS_PER_ROUND)
	for round in _PRE_ROUNDS:
		_every_pair(evidence, _PHASE_PRE, round)
		_poll_rounds(evidence, _POLLS_PER_ROUND)
	var errors_before := counter.errors.size()
	if not _cut(evidence):
		return false
	for round in _MID_ROUNDS:
		if round == _JOIN_ROUND:
			_admit_joiner(evidence)
		_every_pair(evidence, _PHASE_MID, round)
		_poll_seats(evidence, round)
	_poll_rounds(evidence, _SETTLE_POLLS)
	for round in _POST_ROUNDS:
		_every_pair(evidence, _PHASE_POST, round)
		_poll_rounds(evidence, _POLLS_PER_ROUND)
	evidence.errors = counter.errors.slice(errors_before)
	for seat in evidence.seats:
		seat.close_record()
	return true


func _cut(evidence: _Evidence) -> bool:
	var server: _Seat = evidence.seats[0]
	for seat in evidence.seats:
		evidence.ids_before.append(seat.peer.get_unique_id())
	for seat in evidence.seats:
		seat.open_cut(evidence.ids_before)
	var order: Array[_Seat] = evidence.seats.slice(1)
	if evidence.scenario == SCENARIO_LOST:
		driver.lose_server()
		server.lost = true
		server.mp.multiplayer_peer = null
	else:
		var successor: _Seat = evidence.seats[2]
		evidence.requested = successor.original_id
		var error := driver.hand_off(server.peer, successor.original_id)
		if error == ERR_UNAVAILABLE:
			return false
		if error != OK:
			var detail := "hand_off answered %s" % error_string(error)
			_fail("world", evidence, detail)
			return false
		order = [successor, server, evidence.seats[1], evidence.seats[3]]
	for index in order.size():
		order[index].first_round = _INSTALL_ROUNDS[index]
	return true


func _admit_joiner(evidence: _Evidence) -> void:
	var peer := driver.join()
	if peer == null:
		return
	var joiner := _mount(peer, true)
	joiner.open_cut(evidence.ids_before)
	joiner.installs = 1
	joiner.first_round = _JOIN_ROUND
	joiner.refused = (
			peer.get_connection_status() != MultiplayerPeer.CONNECTION_CONNECTED
	)
	evidence.seats.append(joiner)
	evidence.joiner = joiner


func _mount(peer: MultiplayerPeer, joined: bool) -> _Seat:
	var seat := _Seat.new(peer, joined)
	seat.holder.name = "TransferSeat%d" % seat.original_id
	parent.add_child(seat.holder)
	parent.get_tree().set_multiplayer(seat.mp, seat.holder.get_path())
	seat.mp.multiplayer_peer = peer
	seat.listen()
	return seat


func _teardown(evidence: _Evidence) -> void:
	for seat in evidence.seats:
		seat.mp.multiplayer_peer = null
		if seat.holder.is_inside_tree():
			parent.get_tree().set_multiplayer(null, seat.holder.get_path())
		seat.holder.free()
		seat.mp.auth_callback = Callable()
	driver.close()


func _poll_rounds(evidence: _Evidence, polls: int) -> void:
	for poll in polls:
		_poll_seats(evidence, -1)


func _poll_seats(evidence: _Evidence, round: int) -> void:
	for seat in evidence.seats:
		if seat.is_live() and (round < 0 or seat.first_round <= round):
			seat.mp.poll()
			seat.note_status()


func _every_pair(evidence: _Evidence, phase: String, round: int) -> void:
	for seat in evidence.live_seats():
		for label in seat.mp.get_peers():
			var target := _seat_labelled(evidence, label)
			if target != null and target.installs != seat.installs:
				evidence.crossings += 1
			var payload := seat.tag(phase, round)
			seat.probe.rpc_id(label, &"ping", payload)
			seat.mp.send_bytes(payload.to_utf8_buffer(), label)


func _seat_labelled(evidence: _Evidence, label: int) -> _Seat:
	for seat in evidence.live_seats():
		if seat.peer.get_unique_id() == label:
			return seat
	return null


func _fail(law: String, evidence: _Evidence, detail: String) -> void:
	_judged_label(law, evidence)
	failures.append("%s x %s: %s" % [law, evidence.scenario, detail])


func _judged_label(law: String, evidence: _Evidence) -> void:
	var label := "%s x %s" % [law, evidence.scenario]
	if not judged.has(label):
		judged.append(label)


class _ErrorCounter:
	extends Logger

	var errors: Array[String] = []


	func _log_error(
			_function: String,
			_file: String,
			_line: int,
			code: String,
			rationale: String,
			_editor_notify: bool,
			_error_type: int,
			_script_backtraces: Array[ScriptBacktrace],
	) -> void:
		errors.append(code if rationale.is_empty() else rationale)


class _Evidence:
	extends RefCounted

	var scenario: StringName
	var seats: Array[_Seat] = []
	var ids_before: Array[int] = []
	var requested := 0
	var crossings := 0
	var errors: Array[String] = []
	var joiner: _Seat


	func _init(p_scenario: StringName) -> void:
		scenario = p_scenario


	func survivors() -> Array[_Seat]:
		return seats.filter(
			func(seat: _Seat) -> bool:
				return seat.is_live() and not seat.joined
		)


	func live_seats() -> Array[_Seat]:
		return seats.filter(func(seat: _Seat) -> bool: return seat.is_live())


	func final_labels() -> Array[int]:
		var labels: Array[int] = []
		for seat in live_seats():
			labels.append(seat.final_id)
		return labels


	func transfer() -> Array:
		for seat in survivors():
			if not seat.relabels.is_empty():
				return seat.relabels[0]
		return []


	func reads_after_cut() -> Array[Dictionary]:
		var after: Array[Dictionary] = []
		for seat in seats:
			for read in seat.reads:
				if read.phase != _PHASE_PRE and read.phase != _PHASE_ALPHA:
					after.append(read)
		return after


class _Seat:
	extends RefCounted

	var peer: MultiplayerPeer
	var mp := SceneMultiplayer.new()
	var holder := Node.new()
	var alpha := _Probe.new()
	var probe := _Probe.new()
	var original_id := 0
	var final_id := 0
	var final_peers: Array[int] = []
	var joined := false
	var lost := false
	var refused := false
	var first_round := 0
	var installs := 0
	var relabels: Array = []
	var in_signal: Array[Dictionary] = []
	var edges: Array[int] = []
	var status_faults: Array[int] = []
	var reads: Array[Dictionary] = []
	var relay_supported := false
	var _labels_before: Array[int] = []
	var _watching := false
	var _resetting := false


	func _init(p_peer: MultiplayerPeer, p_joined: bool) -> void:
		peer = p_peer
		joined = p_joined
		original_id = peer.get_unique_id()
		alpha.name = "Alpha"
		probe.name = "Probe"
		holder.add_child(alpha)
		holder.add_child(probe)
		alpha.heard = _hear.bind(_KIND_RPC)
		probe.heard = _hear.bind(_KIND_RPC)
		mp.peer_packet.connect(_hear_bytes)
		mp.server_relay = false
		mp.auth_callback = _authenticate
		mp.peer_authenticating.connect(_send_auth)


	func listen() -> void:
		relay_supported = peer.is_server_relay_supported()
		peer.peer_connected.connect(_note_edge.bind(1))
		peer.peer_disconnected.connect(_note_edge.bind(-1))
		peer.connect(&"server_relabelled", _on_server_relabelled)


	func is_live() -> bool:
		return not lost and not refused


	func open_cut(ids_before: Array[int]) -> void:
		_labels_before = ids_before
		_watching = true


	func close_record() -> void:
		if not is_live():
			return
		final_id = peer.get_unique_id()
		final_peers.assign(Array(mp.get_peers()))
		relay_supported = relay_supported or peer.is_server_relay_supported()


	func tag(phase: String, round: int) -> String:
		return "%s|%d|%d|%d" % [phase, round, peer.get_unique_id(), installs]


	func rounds_heard(kind: String, sender: int) -> int:
		var rounds: Dictionary[int, bool] = { }
		for read in reads:
			if read.kind == kind and read.phase == _PHASE_POST:
				if read.remote == sender:
					rounds[read.round] = true
		return rounds.size()


	func note_status() -> void:
		var status := peer.get_connection_status()
		if _watching and status != MultiplayerPeer.CONNECTION_CONNECTED:
			status_faults.append(status)


	func _hear(remote: int, payload: String, kind: String) -> void:
		var parts := payload.split("|")
		reads.append(
			{
				"kind": kind,
				"phase": parts[0],
				"round": int(parts[1]),
				"claimed": int(parts[2]),
				"sender_installs": int(parts[3]),
				"remote": remote,
				"reader": original_id,
				"reader_installs": installs,
			},
		)


	func _hear_bytes(remote: int, packet: PackedByteArray) -> void:
		_hear(remote, packet.get_string_from_utf8(), _KIND_BYTES)


	func _note_edge(label: int, sign: int) -> void:
		if _watching and not _resetting:
			edges.append(label * sign)


	func _authenticate(label: int, _data: PackedByteArray) -> void:
		mp.complete_auth(label)


	func _send_auth(label: int) -> void:
		mp.send_auth(label, PackedByteArray([7]))


	func _on_server_relabelled(successor_was: int, former_now: int) -> void:
		var packet_peer := 0
		if peer.get_available_packet_count() > 0:
			packet_peer = peer.get_packet_peer()
		in_signal.append(
			{
				"unique_id": peer.get_unique_id(),
				"is_server": mp.is_server(),
				"packet_peer": packet_peer,
			},
		)
		relabels.append([successor_was, former_now])
		note_status()
		installs += 1
		_reset_inner(successor_was, former_now)


	func _reset_inner(successor_was: int, former_now: int) -> void:
		var own_id := peer.get_unique_id()
		var auth := mp.auth_callback
		mp.auth_callback = Callable()
		_resetting = true
		for label in mp.get_peers():
			peer.peer_disconnected.emit(label)
		for label in _labels_before:
			var next := NetwTransferContract._relabelled(
				label,
				successor_was,
				former_now,
			)
			if next != 0 and next != own_id:
				peer.peer_connected.emit(next)
		_resetting = false
		mp.auth_callback = auth


class _Probe:
	extends Node

	var heard: Callable


	@rpc("any_peer", "reliable") func ping(payload: String) -> void:
		heard.call(multiplayer.get_remote_sender_id(), payload)
