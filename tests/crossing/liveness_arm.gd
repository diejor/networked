## The liveness family's Bridge-B arm: drives the GDScript record plane through
## scripted lifecycles and compares what it observes to the committed golden.
##
## This is the instrument, not a test. Liveness has no seam and no byte grammar,
## so the only thing that can license deleting the GDScript arm is a trace
## recorded from that arm BEFORE its replacement existed. Recording the trace
## afterwards would describe what the port produced rather than what it must
## reproduce, and no amount of later work recovers that.
##
## [br][br]
## Rows are names and integers, so there is no tolerance question. Every state
## is written as its symbolic name rather than its ordinal, so a renumbering
## fails the comparison instead of moving the golden silently, and every handle
## is written as an identity relation rather than as an RID, because an RID is
## an allocation address and is not stable across runs.
## [codeblock]
## godot --headless --path . -s res://tests/crossing/liveness_arm.gd
## godot --headless --path . -s res://tests/crossing/liveness_arm.gd -- --record
## [/codeblock]
##
## Scenarios named [code]record/[/code] drive the record plane alone, which is
## what crosses, and the native replay reproduces them directly. Scenarios named
## [code]shell/[/code] drive the wrapper plane that stays GDScript, and they are
## the regression guard the port runs against rather than the crossing evidence.
##
## Exits 1 on the first differing row, naming the scenario and both sides.
extends SceneTree

const Recorder := preload("res://tests/support/netw_recorder.gd")

const GOLDEN := "res://tests/native/goldens/liveness_lifecycle.txt"

const STATE_NAMES: Array[String] = ["UNKNOWN", "LIVE", "LINGERING", "DEAD"]

# Every scenario in the order the golden holds them.
const SCENARIOS: Array[StringName] = [
	&"record/mint_bind_tombstone",
	&"record/bulk_claim_and_release",
	&"record/revival_is_a_new_epoch",
	&"record/pending_live_flush",
	&"record/pending_live_timeout",
	&"record/pending_live_two_deadlines",
	&"shell/adoption_row_then_wrapper",
	&"shell/adoption_wrapper_then_row",
]

var _rows: Array[String] = []
var _api: NetwMultiplayer
var _tree_node: MultiplayerTree
var _owned: Array[Node] = []


func _initialize() -> void:
	if "--record" in OS.get_cmdline_user_args():
		var text := "\n".join(_run_all())
		var file := FileAccess.open(GOLDEN, FileAccess.WRITE)
		assert(file != null, "cannot write golden: %s" % GOLDEN)
		file.store_string(_header() + text + "\n")
		print("ARM recorded %d rows to %s" % [_run_all().size(), GOLDEN])
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
		_open_session()
		match scenario:
			&"record/mint_bind_tombstone":
				_mint_bind_tombstone(scenario)
			&"record/bulk_claim_and_release":
				_bulk_claim_and_release(scenario)
			&"record/revival_is_a_new_epoch":
				_revival_is_a_new_epoch(scenario)
			&"record/pending_live_flush":
				_pending_live_flush(scenario)
			&"record/pending_live_timeout":
				_pending_live_timeout(scenario)
			&"record/pending_live_two_deadlines":
				_pending_live_two_deadlines(scenario)
			&"shell/adoption_row_then_wrapper":
				_adoption_row_then_wrapper(scenario)
			&"shell/adoption_wrapper_then_row":
				_adoption_wrapper_then_row(scenario)
			_:
				printerr("unknown scenario: %s" % scenario)
		_close_session()
	return _rows


#region Scenarios

# One handle, one route, one tombstone: the whole state machine in a straight
# line, with the unbound reads that must stay UNKNOWN rather than DEAD.
func _mint_bind_tombstone(scenario: StringName) -> void:
	var entity := _api.entity_create()
	var route := _api._native_core.liveness_reserve_route()
	_state(scenario, "minted", route, entity)

	_api.entity_bind_route(entity, route)
	_state(scenario, "bound", route, entity)
	_row(scenario, "route_of", { &"route": _api.entity_get_route(entity) })
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })

	_api._native_core.liveness_tombstone_routes_data(PackedInt64Array([route]))
	_state(scenario, "tombstoned", route, entity)
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })

	# A route nobody ever reserved is early, not dead, and that difference is
	# the whole reason the state machine has an UNKNOWN.
	_row(
		scenario,
		"unreserved",
		{ &"state": _state_name(_api.entity_get_state(
				_api.entity_from_route(route + 1000))) },
	)


# The bulk doors, which mint identities with no wrapper and no node. They are
# the record plane at its purest: a wave of rows in, a tombstone over part of
# it, and a live set that must stay sorted.
func _bulk_claim_and_release(scenario: StringName) -> void:
	var routes := _api.claim_routes(3)
	_row(scenario, "claimed", { &"routes": routes })
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })
	for value in routes:
		_row(
			scenario,
			"state",
			{
				&"route": int(value),
				&"state": _state_name(_api.entity_get_state(
						_api.entity_from_route(int(value)))),
			},
		)

	_api._native_core.liveness_tombstone_routes_data(PackedInt64Array([routes[1]]))
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })
	for value in routes:
		_row(
			scenario,
			"state",
			{
				&"route": int(value),
				&"state": _state_name(_api.entity_get_state(
						_api.entity_from_route(int(value)))),
			},
		)


# A route names one entity for the whole session, so a re-admission is the
# record it already had one life higher and a stranger asking for the same route
# is refused. Both facts have to hold at once, which is what makes this the
# scenario the port most easily gets half right.
func _revival_is_a_new_epoch(scenario: StringName) -> void:
	var first := _api.entity_create()
	var route := _api._native_core.liveness_reserve_route()
	_api.entity_bind_route(first, route)
	_api._native_core.liveness_tombstone_routes_data(PackedInt64Array([route]))
	_state(scenario, "first_dead", route, first)

	var stranger := _api.entity_create()
	_row(
		scenario,
		"rename",
		{ &"bound": _api.entity_bind_route(stranger, route) == OK },
	)

	_row(
		scenario,
		"revive",
		{ &"bound": _api.entity_bind_route(first, route) == OK },
	)
	_state(scenario, "second_live", route, first)
	_row(
		scenario,
		"identity",
		{
			&"epoch": _api.entity_get_epoch(first),
			&"route_holds_first": _api.entity_from_route(route) == first,
		},
	)


# A callback parked on a route nobody has bound yet, answered by the bulk door.
# The count before and after is what says the queue released the entry rather
# than merely running it.
func _pending_live_flush(scenario: StringName) -> void:
	var route := _api._native_core.liveness_reserve_route() + 1
	_api.when_live(route, func() -> void: _row(scenario, "cb", { &"route": route }))
	_row(scenario, "pending", { &"count": _api._native_core.liveness_pending_live_count() })

	var claimed := _api.claim_routes(1)
	_row(scenario, "claimed", { &"routes": claimed })
	_row(scenario, "pending", { &"count": _api._native_core.liveness_pending_live_count() })

	# A second binding of the same route must not answer the same caller twice.
	_api._native_core.liveness_bind_routes_data(PackedInt64Array([route]))
	_row(scenario, "pending", { &"count": _api._native_core.liveness_pending_live_count() })


# The expiry half. Nothing binds the route, so the entry ages out against the
# frame counter and the timeout callback runs in its place.
func _pending_live_timeout(scenario: StringName) -> void:
	var route := _api._native_core.liveness_reserve_route() + 5
	_row(scenario, "clock", { &"configured": _api._native_core.clock_handle.is_configured })

	_api.when_live(
		route,
		func() -> void: _row(scenario, "cb", { &"route": route }),
		3,
		func() -> void: _row(scenario, "timeout", { &"route": route }),
	)
	for step in 4:
		_api._liveness_poll()
		_row(
			scenario,
			"polled",
			{
				&"step": step,
				&"pending": _api._native_core.liveness_pending_live_count(),
			},
		)


# Two waits on one route with different deadlines. The shorter one expiring
# must not take the longer one with it, which is the sweep's one real edge.
func _pending_live_two_deadlines(scenario: StringName) -> void:
	var route := _api._native_core.liveness_reserve_route() + 5
	_api.when_live(
		route,
		func() -> void: _row(scenario, "cb", { &"deadline": 2 }),
		2,
		func() -> void: _row(scenario, "timeout", { &"deadline": 2 }),
	)
	_api.when_live(
		route,
		func() -> void: _row(scenario, "cb", { &"deadline": 4 }),
		4,
		func() -> void: _row(scenario, "timeout", { &"deadline": 4 }),
	)
	for step in 5:
		_api._liveness_poll()
		_row(
			scenario,
			"polled",
			{
				&"step": step,
				&"pending": _api._native_core.liveness_pending_live_count(),
			},
		)


# A row mints the identity and a wrapper arrives afterwards. The wrapper must
# adopt the record rather than mint a second one, and the wrapper plane's own
# signal fires exactly once for it.
func _adoption_row_then_wrapper(scenario: StringName) -> void:
	var recorder := _watch()
	var route := int(_api.claim_routes(1)[0])
	var minted := _api.entity_from_route(route)
	_row(scenario, "signals", { &"order": recorder.order() })

	var entity := _spawn_entity()
	_api._native_core.liveness_bind_route(route, entity)
	_row(
		scenario,
		"adopted",
		{
			&"same_record": entity.rid == minted,
			&"route": _api.entity_get_route(minted),
		},
	)
	_row(scenario, "signals", { &"order": recorder.order() })
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })


# The reverse ordering. The wrapper got there first, so the bulk door must reuse
# the record it already holds instead of minting past it.
func _adoption_wrapper_then_row(scenario: StringName) -> void:
	var recorder := _watch()
	var entity := _spawn_entity()
	var route := _api.entity_admit(_api.entity_of(entity.owner))
	var held := entity.rid
	_row(scenario, "signals", { &"order": recorder.order() })

	_api._native_core.liveness_bind_routes_data(PackedInt64Array([route]))
	_row(
		scenario,
		"reused",
		{
			&"same_record": entity.rid == held,
			&"route_holds_wrapper": _api.entity_from_route(route) == held,
		},
	)
	_row(scenario, "signals", { &"order": recorder.order() })
	_row(scenario, "live_routes", { &"routes": _api.live_routes() })

#endregion

#region Rig

func _open_session() -> void:
	_tree_node = MultiplayerTree.new()
	_tree_node.name = "LivenessArm"
	root.add_child(_tree_node)
	_api = _tree_node.api


func _close_session() -> void:
	for node in _owned:
		if is_instance_valid(node):
			node.queue_free()
	_owned.clear()
	_api = null
	root.remove_child(_tree_node)
	_tree_node.queue_free()
	_tree_node = null


func _watch() -> Recorder:
	return Recorder.new(
		_api._native_core,
		[&"entity_live", &"entity_lingering", &"entity_dead"],
	)


func _spawn_entity() -> NetwEntity:
	var node := Node2D.new()
	var entity := NetwEntity.ensure(node)
	_tree_node.add_child(node)
	_owned.append(node)
	return entity

#endregion

#region Rows

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


# The pair of readings every transition is judged by: the route's state and the
# handle's, which must agree except where a superseded record is the point.
func _state(
		scenario: StringName,
		label: String,
		route: int,
		entity: RID,
) -> void:
	_row(
		scenario,
		"state",
		{
			&"at": label,
			&"entity": _state_name(_api.entity_get_state(entity)),
			&"route": _state_name(_api.entity_get_state(_api.entity_from_route(route))),
		},
	)


func _state_name(state: int) -> String:
	return STATE_NAMES[state] if state >= 0 and state < 4 else "INVALID(%d)" % state


func _value(value: Variant) -> String:
	if value is bool:
		return "true" if value else "false"
	if value is PackedInt32Array or value is PackedInt64Array or value is Array:
		var parts: Array[String] = []
		for item in value:
			parts.append(str(item))
		return "[%s]" % ",".join(parts)
	return str(value)


func _header() -> String:
	return (
		"# The liveness family's Bridge-B trace, recorded from the GDScript\n"
		+ "# record plane before any of it was native. Rows are\n"
		+ "# scenario|kind|key=value, keys sorted, states symbolic.\n"
		+ "#\n"
		+ "# Regenerate with:\n"
		+ "#   godot --headless --path . -s res://tests/crossing/liveness_arm.gd \\\n"
		+ "#     -- --record\n"
		+ "# Regenerating against a candidate implementation destroys the\n"
		+ "# evidence this file exists to be.\n"
		+ "#\n"
		+ "# record/revival_is_a_new_epoch is the one scenario whose rows are\n"
		+ "# hand-written rather than recorded. Its recorded rows described a\n"
		+ "# revival minting a second record, which the record plane no longer\n"
		+ "# does, so the arm and the native replay were re-authored separately\n"
		+ "# against these rows instead of either being recorded from the other.\n"
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
