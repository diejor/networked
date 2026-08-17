## The session family's Bridge-B arm: drives the GDScript session machine
## through scripted admission and lifecycle scenarios and compares what it
## observes to the committed golden.
##
## This is the instrument, not a test. The session has no seam and no byte
## grammar, so the only thing that can license deleting its GDScript arm is a
## trace recorded from that arm BEFORE its replacement existed. Recording it
## afterwards would describe what the port produced rather than what it must
## reproduce, and no amount of later work recovers that.
##
## [br][br]
## Every scenario drives a bare [NetwMultiplayer] with no [MultiplayerTree] and
## no host or join verb, because that is the contract the machine is for: a
## plain [code]multiplayer_peer = peer[/code] moves it on its own. States and
## roles are written symbolically, so a renumbered [enum SessionCore.State]
## fails the comparison instead of moving the golden quietly.
## [br][br]
## Scenarios named [code]machine/[/code], [code]edges/[/code],
## [code]flood/[/code] and [code]apptag/[/code] read [SessionCore] directly:
## that is the plane which crosses, and the native replay reproduces those rows.
## Scenarios named [code]shell/[/code] take the same readings through
## [NetwMultiplayer]'s own band instead, so they pin what the wrapper answers
## rather than what the machine holds. They are the regression guard the port
## runs against, and they exist because the shell is deleted before the carried
## suites over it have been re-authored.
## [codeblock]
## godot --headless --path . -s res://tests/crossing/session_arm.gd
## godot --headless --path . -s res://tests/crossing/session_arm.gd -- --record
## [/codeblock]
##
## TWO PRIVATES ARE REACHED DELIBERATELY, and both are behaviour rather than
## state. [code]_join_flooded[/code] answers the flood guard's whole decision
## and has no public caller short of a real client submitting a real frame;
## [code]_compute_app_tag[/code] is the build-compatibility gate the hello
## carries, and a port that folds it differently locks every peer out of a
## session that previously admitted them. Both are worth more in the golden
## than the purity of not reaching them.
##
## Exits 1 on the first differing row, naming the scenario and both sides.
extends SceneTree

const Recorder := preload("res://tests/support/netw_recorder.gd")

const GOLDEN := "res://tests/native/goldens/session_admission.txt"

## The session's own signals, in declaration order. A recorder on a signal that
## does not exist fails where it is constructed, which is what makes an empty
## trace here a failure rather than a quiet pass.
const WATCHED: Array[StringName] = [
	&"state_changed",
	&"session_entered",
	&"session_ended",
	&"participant_admitted",
	&"paused",
	&"unpaused",
	&"kicked",
]

## The peer id a client scenario draws. Any id but 1 resolves as a client, and
## naming it once keeps the trace from reading as though the value mattered.
const CLIENT_PEER := 7

## Strings the app tag is folded from. Chosen to cover the empty gate, an
## ordinary tag, a tag differing in one character, and one long enough that a
## 32-bit truncation is the only thing the mask can be doing. The spread also
## has to reach a fold whose top bit is set, because that is the one value a
## reimplementation can get wrong while every other row still matches.
const APP_TAGS: Array[String] = [
	"",
	"networked",
	"networkee",
	"networked-demo-build-2026-08-06-with-a-long-tail",
	"bomber",
	"quick_start",
	"éé",
	"netw/1.0.0",
	"netw/1.0.1",
]

# Every scenario in the order the golden holds them.
const SCENARIOS: Array[StringName] = [
	&"machine/offline_at_construction",
	&"machine/a_server_peer_is_online_at_the_edge",
	&"machine/the_listen_hint_splits_the_server_role",
	&"machine/a_client_peer_waits_in_connecting",
	&"machine/the_connected_signal_completes_the_client",
	&"machine/a_cancelled_connect_returns_offline",
	&"machine/a_failed_handshake_never_enters_online",
	&"machine/a_server_that_vanishes_ends_the_session",
	&"machine/a_second_crash_never_re_tears",
	&"machine/a_null_assignment_while_online_is_left_alone",
	&"machine/a_disposing_api_is_not_a_crash",
	&"edges/every_legal_edge_runs_its_hooks",
	&"flood/an_honest_peer_stays_under_the_window",
	&"flood/a_flood_trips_at_the_limit",
	&"flood/the_host_self_join_is_never_limited",
	&"flood/each_peer_draws_its_own_budget",
	&"apptag/the_fold_is_the_compatibility_gate",
	&"shell/the_api_mirrors_the_machine_it_holds",
	&"shell/the_service_registry_answers_by_type",
	&"shell/disposal_is_idempotent_and_leaves_no_peer",
]

var _rows: Array[String] = []
var _api: NetwMultiplayer
var _session: SessionCore
var _recorder: RefCounted


func _initialize() -> void:
	if "--record" in OS.get_cmdline_user_args():
		var rows := _run_all()
		var file := FileAccess.open(GOLDEN, FileAccess.WRITE)
		assert(file != null, "cannot write golden: %s" % GOLDEN)
		file.store_string(_header() + "\n".join(rows) + "\n")
		print("ARM recorded %d rows to %s" % [rows.size(), GOLDEN])
		quit(0)
		return
	quit(_compare())


func _compare() -> int:
	var expected := _read_golden()
	var actual := _run_all()
	if expected.is_empty():
		printerr("ARM golden is empty: %s" % GOLDEN)
		return 1

	var limit := maxi(expected.size(), actual.size())
	for index in limit:
		var want := expected[index] if index < expected.size() else "<missing>"
		var got := actual[index] if index < actual.size() else "<missing>"
		if want == got:
			continue
		printerr(
			"DIFFER at row %d\n  golden %s\n  arm    %s" % [index, want, got]
		)
		printerr("ARM %d of %d rows match" % [index, expected.size()])
		return 1

	print("ARM %d rows match %s" % [actual.size(), GOLDEN])
	return 0


func _run_all() -> Array[String]:
	_rows = []
	for scenario in SCENARIOS:
		match scenario:
			&"machine/offline_at_construction":
				_offline_at_construction(scenario)
			&"machine/a_server_peer_is_online_at_the_edge":
				_a_server_peer_is_online_at_the_edge(scenario)
			&"machine/the_listen_hint_splits_the_server_role":
				_the_listen_hint_splits_the_server_role(scenario)
			&"machine/a_client_peer_waits_in_connecting":
				_a_client_peer_waits_in_connecting(scenario)
			&"machine/the_connected_signal_completes_the_client":
				_the_connected_signal_completes_the_client(scenario)
			&"machine/a_cancelled_connect_returns_offline":
				_a_cancelled_connect_returns_offline(scenario)
			&"machine/a_failed_handshake_never_enters_online":
				_a_failed_handshake_never_enters_online(scenario)
			&"machine/a_server_that_vanishes_ends_the_session":
				_a_server_that_vanishes_ends_the_session(scenario)
			&"machine/a_second_crash_never_re_tears":
				_a_second_crash_never_re_tears(scenario)
			&"machine/a_null_assignment_while_online_is_left_alone":
				_a_null_assignment_while_online_is_left_alone(scenario)
			&"machine/a_disposing_api_is_not_a_crash":
				_a_disposing_api_is_not_a_crash(scenario)
			&"edges/every_legal_edge_runs_its_hooks":
				_every_legal_edge_runs_its_hooks(scenario)
			&"flood/an_honest_peer_stays_under_the_window":
				_an_honest_peer_stays_under_the_window(scenario)
			&"flood/a_flood_trips_at_the_limit":
				_a_flood_trips_at_the_limit(scenario)
			&"flood/the_host_self_join_is_never_limited":
				_the_host_self_join_is_never_limited(scenario)
			&"flood/each_peer_draws_its_own_budget":
				_each_peer_draws_its_own_budget(scenario)
			&"apptag/the_fold_is_the_compatibility_gate":
				_the_fold_is_the_compatibility_gate(scenario)
			&"shell/the_api_mirrors_the_machine_it_holds":
				_the_api_mirrors_the_machine_it_holds(scenario)
			&"shell/the_service_registry_answers_by_type":
				_the_service_registry_answers_by_type(scenario)
			&"shell/disposal_is_idempotent_and_leaves_no_peer":
				_disposal_is_idempotent_and_leaves_no_peer(scenario)
			_:
				printerr("unknown scenario: %s" % scenario)
	return _rows


#region The shell's session band

# The api's state, role and the two booleans derived from them are reads of the
# machine rather than a second copy, so every edge the machine takes has to show
# through them without anything pushing it across.
func _the_api_mirrors_the_machine_it_holds(scenario: StringName) -> void:
	_open()
	_mirror(scenario, "constructed")

	_assign(_server_peer())
	_mirror(scenario, "server_assigned")

	_api.multiplayer_peer = null
	_mirror(scenario, "peer_cleared")
	_close()

	_open()
	_assign(_client_peer())
	_mirror(scenario, "client_assigned")
	_close()


# The registry is a discovery surface for the kit and game code, keyed by the
# script a node was registered under rather than by the node, so a lookup
# answers a type and unregistering a type nobody registered changes nothing.
func _the_service_registry_answers_by_type(scenario: StringName) -> void:
	_open()
	var type := GDScript.new()
	type.source_code = "extends Node\n"
	type.reload()
	var service := Node.new()
	service.set_script(type)

	_row(
		scenario,
		"empty",
		{ &"found": _api.get_service(type) != null },
	)

	_api.register_service(service, type)
	_row(
		scenario,
		"registered",
		{
			&"found": _api.get_service(type) == service,
			&"listed": _api.get_services(type).size(),
		},
	)

	_api.register_service(service, type)
	_row(
		scenario,
		"twice",
		{ &"listed": _api.get_services(type).size() },
	)

	_api.unregister_service(service, type)
	_row(
		scenario,
		"unregistered",
		{
			&"found": _api.get_service(type) != null,
			&"listed": _api.get_services(type).size(),
		},
	)

	_api.unregister_service(service, type)
	_row(
		scenario,
		"again",
		{ &"listed": _api.get_services(type).size() },
	)

	service.free()
	_close()


# Disposal is what an embedder calls, and it is called from teardown paths that
# cannot know whether another already ran. A second call has to be inert rather
# than a second teardown, and the api has to read offline afterwards.
func _disposal_is_idempotent_and_leaves_no_peer(scenario: StringName) -> void:
	_open()
	_assign(_server_peer())
	_mirror(scenario, "online")

	_api.embedding.dispose()
	_mirror(scenario, "disposed")
	_signals(scenario)

	_api.embedding.dispose()
	_mirror(scenario, "disposed_again")
	_signals(scenario)
	_close(false)

#endregion

#region The machine

# A session with nothing assigned answers with defaults rather than with a
# special inert mode, which is what lets a bare api be read at all.
func _offline_at_construction(scenario: StringName) -> void:
	_open()
	_state(scenario, "constructed")
	_signals(scenario)
	_close()


# A server peer is born live, so the edge alone carries it the whole way to
# ONLINE with no connection signal to wait for.
func _a_server_peer_is_online_at_the_edge(scenario: StringName) -> void:
	_open()
	_assign(_server_peer())
	_state(scenario, "assigned")
	_signals(scenario)
	_close()


# Peer identity says server; only the configured hint says which kind.
func _the_listen_hint_splits_the_server_role(scenario: StringName) -> void:
	for hint in [
		NetwMultiplayer.Role.LISTEN_SERVER,
		NetwMultiplayer.Role.DEDICATED_SERVER,
		NetwMultiplayer.Role.CLIENT,
	]:
		_open()
		var config := NetwSessionConfig.new()
		config.desired_role = hint
		_session.configure(config)
		_row(scenario, "hint", { &"desired": _role_name(_session.desired_role) })

		_assign(_server_peer())
		_state(scenario, "assigned")
		_close()


# A client peer is still mid-handshake at assignment, so the machine holds.
func _a_client_peer_waits_in_connecting(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_state(scenario, "assigned")
	_signals(scenario)
	_close()


func _the_connected_signal_completes_the_client(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_state(scenario, "assigned")

	_api.connected_to_server.emit()
	_state(scenario, "connected")
	_signals(scenario)

	# The transport repeating itself must not re-enter a state already left.
	_api.connected_to_server.emit()
	_state(scenario, "repeated")
	_signals(scenario)
	_close()


# A cancelled connect is the null assignment rather than a bespoke abort verb.
func _a_cancelled_connect_returns_offline(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_state(scenario, "connecting")

	_session.on_peer_assigned(null)
	_state(scenario, "cancelled")
	_signals(scenario)

	# An OfflineMultiplayerPeer reads as the same cancellation as a null.
	_assign(_client_peer())
	_session.on_peer_assigned(OfflineMultiplayerPeer.new())
	_state(scenario, "offline_peer")
	_signals(scenario)
	_close()


# session_ended never fires on a connect that failed before ONLINE, which is
# the one thing the exit hook is placed where it is to guarantee.
func _a_failed_handshake_never_enters_online(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_api.connection_failed.emit()
	_state(scenario, "failed")
	_signals(scenario)
	_close()


func _a_server_that_vanishes_ends_the_session(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_api.connected_to_server.emit()
	_state(scenario, "online")
	_signals(scenario)

	# The crash reuses the graceful leave's teardown rather than a direct edge,
	# so the trace below is the whole reason DISCONNECTING is not skipped.
	_api.server_disconnected.emit()
	_state(scenario, "crashed")
	_signals(scenario)
	_close()


func _a_second_crash_never_re_tears(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_api.connected_to_server.emit()
	_api.server_disconnected.emit()
	_signals(scenario)

	_api.server_disconnected.emit()
	_state(scenario, "second_crash")
	_signals(scenario)
	_close()


# A tree deletion nulls the peer, and that must not end a live session.
func _a_null_assignment_while_online_is_left_alone(scenario: StringName) -> void:
	_open()
	_assign(_server_peer())
	_state(scenario, "online")
	_signals(scenario)

	_session.on_peer_assigned(null)
	_state(scenario, "nulled")
	_signals(scenario)
	_close()


# A disposing api closes its own peer, and the crash signal that rides that
# close is the api's own doing rather than the server vanishing.
func _a_disposing_api_is_not_a_crash(scenario: StringName) -> void:
	_open()
	_assign(_client_peer())
	_api.connected_to_server.emit()
	_state(scenario, "online")

	_api.embedding.dispose()
	_api.server_disconnected.emit()
	_state(scenario, "disposed")
	_signals(scenario)
	_close(false)


# Every edge the table admits, driven in the one order that reaches all of
# them, so the enter and exit hooks are observed paired rather than asserted.
func _every_legal_edge_runs_its_hooks(scenario: StringName) -> void:
	_open()
	for next in [
		SessionCore.State.CONNECTING,
		SessionCore.State.OFFLINE,
		SessionCore.State.CONNECTING,
		SessionCore.State.ONLINE,
		SessionCore.State.DISCONNECTING,
		SessionCore.State.OFFLINE,
	]:
		_session.transition(next)
		_state(scenario, "to_%s" % _state_name(next))
		_signals(scenario)

	# The same state twice is a no-op rather than a re-entry, which is what
	# keeps a repeated transport signal from firing a second session_entered.
	_session.transition(SessionCore.State.OFFLINE)
	_state(scenario, "repeated")
	_signals(scenario)
	_close()

#endregion

#region The flood guard

# An honest peer submits one join, twice at most through the resend path.
func _an_honest_peer_stays_under_the_window(scenario: StringName) -> void:
	var session := SessionCore.new()
	for attempt in 2:
		_row(
			scenario,
			"submit",
			{ &"attempt": attempt, &"flooded": session._join_flooded(CLIENT_PEER) },
		)


# The guard admits its whole budget and refuses the one past it. The trip index
# is the reading, because a port that changed the limit by one would otherwise
# still produce a trace that trips.
func _a_flood_trips_at_the_limit(scenario: StringName) -> void:
	var session := SessionCore.new()
	var tripped_at := -1
	for attempt in 20:
		if session._join_flooded(CLIENT_PEER):
			tripped_at = attempt
			break
	_row(scenario, "trip", { &"at": tripped_at })


# The host self-join at peer 1 carries authority and is never limited, so a
# listen server cannot flood itself out of its own session.
func _the_host_self_join_is_never_limited(scenario: StringName) -> void:
	var session := SessionCore.new()
	var flooded := false
	for attempt in 40:
		flooded = flooded or session._join_flooded(1)
	_row(scenario, "host", { &"flooded_once": flooded })


# The window is per peer, so one flooding peer cannot spend another's budget.
func _each_peer_draws_its_own_budget(scenario: StringName) -> void:
	var session := SessionCore.new()
	for attempt in 20:
		session._join_flooded(CLIENT_PEER)
	_row(
		scenario,
		"neighbour",
		{ &"flooded": session._join_flooded(CLIENT_PEER + 1) },
	)

#endregion

#region The app tag

# Empty means the gate is disabled, and everything else folds into the 32 bits
# the hello carries. Two tags differing in one character must not collide.
func _the_fold_is_the_compatibility_gate(scenario: StringName) -> void:
	var session := SessionCore.new()
	for tag in APP_TAGS:
		_row(
			scenario,
			"fold",
			{
				&"length": tag.length(),
				&"tag": session._compute_app_tag(StringName(tag)),
			},
		)

#endregion

#region Fixtures

func _open() -> void:
	_api = NetwMultiplayer.new(SceneMultiplayer.new())
	_session = _api._session
	_recorder = Recorder.new(_session, WATCHED)


# `disposed` is false where a scenario already disposed the api itself, since
# disposing twice is not what any of this is measuring.
func _close(disposed: bool = true) -> void:
	_recorder = null
	if disposed and is_instance_valid(_api):
		_api.embedding.dispose()
	_session = null
	_api = null


func _server_peer() -> LocalMultiplayerPeer:
	var peer := LocalMultiplayerPeer.new()
	peer.create_server()
	return peer


# A client peer wired to a server that will never answer, which is exactly the
# mid-handshake condition the CONNECTING state exists for.
func _client_peer() -> LocalMultiplayerPeer:
	var peer := LocalMultiplayerPeer.new()
	peer.create_client(CLIENT_PEER)
	return peer


func _assign(peer: MultiplayerPeer) -> void:
	_api.multiplayer_peer = peer

#endregion

#region Rows

# The machine's whole reading at one moment, symbolic so a renumbering fails
# the comparison rather than moving the golden.
# The same reading taken through the api rather than through the machine, which
# is the only difference these scenarios are about.
func _mirror(scenario: StringName, label: String) -> void:
	_row(
		scenario,
		"mirror",
		{
			&"at": label,
			&"is_host": _api.is_host,
			&"is_online": _api.is_online,
			&"role": _role_name(_api.role),
			&"state": _state_name(_api.state),
		},
	)


func _state(scenario: StringName, label: String) -> void:
	_row(
		scenario,
		"state",
		{
			&"at": label,
			&"role": _role_name(_session.role),
			&"state": _state_name(_session.state),
		},
	)


# The signal order since the last reading, which is what polled state cannot
# express and what every lifecycle law is actually about. state_changed's
# arguments ride the name, because a trace of edges that does not say which
# edge is a trace of nothing.
func _signals(scenario: StringName) -> void:
	var order: Array[String] = []
	var changes := 0
	for name: StringName in _recorder.order():
		if name != &"state_changed":
			order.append(String(name))
			continue
		var args: Array = _recorder.args(&"state_changed", changes)
		changes += 1
		order.append(
			"state_changed(%s->%s)" % [_state_name(args[0]), _state_name(args[1])]
		)
	_row(scenario, "signals", { &"order": order })
	_recorder.clear()


func _state_name(value: int) -> String:
	return SessionCore.State.keys()[value]


func _role_name(value: int) -> String:
	return SessionCore.Role.keys()[value]


# One observation. Keys are sorted as text so a dictionary literal's authoring
# order can never move a golden. Sorting StringNames directly would not do it:
# they compare by their interned address, which is allocation order rather than
# spelling, and it is stable enough to look correct and not stable enough to be.
func _row(scenario: StringName, kind: String, fields: Dictionary) -> void:
	var keys: Array[String] = []
	for key in fields:
		keys.append(String(key))
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s=%s" % [key, _value(fields[StringName(key)])])
	_rows.append("%s|%s|%s" % [scenario, kind, " ".join(parts)])


func _value(value: Variant) -> String:
	if value is bool:
		return "true" if value else "false"
	if value is Array or value is PackedStringArray:
		var parts: Array[String] = []
		for item in value:
			parts.append(str(item))
		return "[%s]" % ",".join(parts)
	return str(value)


func _header() -> String:
	return (
		"# The session family's Bridge-B trace, recorded from the GDScript\n"
		+ "# session machine before any of it was native. Rows are\n"
		+ "# scenario|kind|key=value, keys sorted, states and roles symbolic.\n"
		+ "#\n"
		+ "# Regenerate with:\n"
		+ "#   godot --headless --path . -s res://tests/crossing/session_arm.gd \\\n"
		+ "#     -- --record\n"
		+ "# Regenerating against a candidate implementation destroys the\n"
		+ "# evidence this file exists to be.\n"
	)


func _read_golden() -> Array[String]:
	var file := FileAccess.open(GOLDEN, FileAccess.READ)
	assert(file != null, "missing golden: %s" % GOLDEN)
	var lines: Array[String] = []
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line.is_empty() or line.begins_with("#"):
			continue
		lines.append(line)
	return lines

#endregion
