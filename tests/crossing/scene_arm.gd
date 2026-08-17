## The scene family's Bridge-B arm: drives the GDScript scene record plane
## through scripted registrations and requests and compares what it observes to
## the committed golden.
##
## This is the instrument, not a test. The scene column has no seam and no byte
## grammar, so the only thing that can license deleting its GDScript arm is a
## trace recorded from that arm BEFORE its replacement existed. Recording it
## afterwards would describe what the port produced rather than what it must
## reproduce, and no amount of later work recovers that.
##
## [br][br]
## Scenarios named [code]observers/[/code] and [code]request/[/code] drive
## [SceneCore] on a bare [NetwMultiplayer] with no scene tree, no node and no
## frame, because the record plane is exactly the half that does not need one:
## what a scene column remembers between frames is a routing table keyed by
## scene identity and one in-flight request. That is the half that crosses, and
## the native replay reproduces those rows directly.
## [br][br]
## Scenarios named [code]shell/[/code] drive the scene band on [NetwMultiplayer]
## itself, under a real [MultiplayerTree], because a scene container is a node
## and every membership and admission verb reads the node the entity stands on.
## They are the regression guard the port runs against rather than the crossing
## evidence, and they exist because the shell they observe is deleted before the
## carried suites over it have been re-authored.
## [br][br]
## Scenes are named by the entity [RID] they are keyed under rather than by
## whichever node is standing in for them, and handles are written as identity
## relations rather than as RIDs, since an RID is an allocation address.
## [codeblock]
## godot --headless --path . -s res://tests/crossing/scene_arm.gd
## godot --headless --path . -s res://tests/crossing/scene_arm.gd -- --record
## [/codeblock]
##
## PRIVATES ARE REACHED DELIBERATELY, and every one of them is behaviour rather
## than state. [code]_dispatch_observers[/code] is what makes a registration
## mean anything and has no public caller short of a live scene changing, and
## the request record is reached the same way the suite this arm outlives
## reaches it. Registration itself goes through the public
## [method SceneCore.observe] and [method SceneCore.unobserve].
##
## Exits 1 on the first differing row, naming the scenario and both sides.
extends SceneTree

const GOLDEN := "res://tests/native/goldens/scene_records.txt"

## Events a registration is keyed under. Written symbolically, so a renumbered
## [enum NetwMultiplayer.SceneEvent] fails the comparison instead of moving the
## golden quietly.
const EVENTS: Array[StringName] = [
	&"SCENE_EVENT_PARTICIPANT",
	&"SCENE_EVENT_PLAYER",
	&"SCENE_EVENT_ENTITY",
]

# Every scenario in the order the golden holds them.
const SCENARIOS: Array[StringName] = [
	&"observers/a_registration_is_keyed_by_scene_and_event",
	&"observers/dispatch_reaches_every_live_callback_in_order",
	&"observers/a_duplicate_registration_is_not_stored_twice",
	&"observers/unobserve_removes_one_and_leaves_the_rest",
	&"observers/an_invalid_scene_or_callback_registers_nothing",
	&"observers/a_dead_callback_is_pruned_at_dispatch",
	&"observers/an_empty_book_dispatches_nothing",
	&"request/ids_are_allocated_in_sequence",
	&"request/the_deadline_settles_the_request_it_names",
	&"request/a_stale_deadline_is_ignored",
	&"request/a_settled_request_leaves_no_pending_id",
	&"shell/a_wrapperless_record_parks_its_facet",
	&"shell/a_node_backed_scene_declares_and_labels",
	&"shell/membership_walks_the_parent_chain",
	&"shell/admission_needs_a_boundary_to_land_on",
]

const SHELL_PREFIX := "shell/"

var _rows: Array[String] = []
var _api: NetwMultiplayer
var _scenes: SceneCore
var _tree_node: MultiplayerTree
var _owned: Array[Node] = []


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
		var shell := String(scenario).begins_with(SHELL_PREFIX)
		if shell:
			_open_shell()
		else:
			_open()
		match scenario:
			&"observers/a_registration_is_keyed_by_scene_and_event":
				_a_registration_is_keyed_by_scene_and_event(scenario)
			&"observers/dispatch_reaches_every_live_callback_in_order":
				_dispatch_reaches_every_live_callback_in_order(scenario)
			&"observers/a_duplicate_registration_is_not_stored_twice":
				_a_duplicate_registration_is_not_stored_twice(scenario)
			&"observers/unobserve_removes_one_and_leaves_the_rest":
				_unobserve_removes_one_and_leaves_the_rest(scenario)
			&"observers/an_invalid_scene_or_callback_registers_nothing":
				_an_invalid_scene_or_callback_registers_nothing(scenario)
			&"observers/a_dead_callback_is_pruned_at_dispatch":
				_a_dead_callback_is_pruned_at_dispatch(scenario)
			&"observers/an_empty_book_dispatches_nothing":
				_an_empty_book_dispatches_nothing(scenario)
			&"request/ids_are_allocated_in_sequence":
				_ids_are_allocated_in_sequence(scenario)
			&"request/the_deadline_settles_the_request_it_names":
				_the_deadline_settles_the_request_it_names(scenario)
			&"request/a_stale_deadline_is_ignored":
				_a_stale_deadline_is_ignored(scenario)
			&"request/a_settled_request_leaves_no_pending_id":
				_a_settled_request_leaves_no_pending_id(scenario)
			&"shell/a_wrapperless_record_parks_its_facet":
				_a_wrapperless_record_parks_its_facet(scenario)
			&"shell/a_node_backed_scene_declares_and_labels":
				_a_node_backed_scene_declares_and_labels(scenario)
			&"shell/membership_walks_the_parent_chain":
				_membership_walks_the_parent_chain(scenario)
			&"shell/admission_needs_a_boundary_to_land_on":
				_admission_needs_a_boundary_to_land_on(scenario)
			_:
				printerr("unknown scenario: %s" % scenario)
		if shell:
			_close_shell()
		else:
			_close()
	return _rows


#region The observer book

# A registration names one scene under one event, so the same scene under a
# different event and a different scene under the same event are separate rows.
func _a_registration_is_keyed_by_scene_and_event(scenario: StringName) -> void:
	var first := _scene()
	var second := _scene()
	var sink := _Sink.new()
	_owned.append(sink)

	_scenes.core.observe(first, NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER, sink.hit)

	for scene_index in 2:
		for event in EVENTS:
			var scene := first if scene_index == 0 else second
			sink.calls.clear()
			_dispatch(scene, event, true, scene_index)
			_row(
				scenario,
				"dispatch",
				{
					&"event": event,
					&"heard": sink.calls.size(),
					&"scene": "first" if scene_index == 0 else "second",
				},
			)


# Every live callback hears the edge, in registration order, with the arguments
# the dispatch carried.
func _dispatch_reaches_every_live_callback_in_order(scenario: StringName) -> void:
	var scene := _scene()
	var first := _Sink.new()
	var second := _Sink.new()
	_owned.append(first)
	_owned.append(second)
	var event := NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY

	_scenes.core.observe(scene, event, first.tagged.bind(&"first"))
	_scenes.core.observe(scene, event, second.tagged.bind(&"second"))
	_dispatch(scene, event, true, 42)
	_row(scenario, "order", { &"heard": _Sink.log_order })

	_Sink.log_order.clear()
	_dispatch(scene, event, false, 43)
	_row(scenario, "order", { &"heard": _Sink.log_order })
	_row(
		scenario,
		"args",
		{ &"first": first.calls, &"second": second.calls },
	)


# Registering the same callable twice stores it once, so an edge is heard once.
func _a_duplicate_registration_is_not_stored_twice(scenario: StringName) -> void:
	var scene := _scene()
	var sink := _Sink.new()
	_owned.append(sink)
	var event := NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER

	_scenes.core.observe(scene, event, sink.hit)
	_scenes.core.observe(scene, event, sink.hit)
	_dispatch(scene, event, true, 1)
	_row(scenario, "heard", { &"count": sink.calls.size() })


func _unobserve_removes_one_and_leaves_the_rest(scenario: StringName) -> void:
	var scene := _scene()
	var kept := _Sink.new()
	var dropped := _Sink.new()
	_owned.append(kept)
	_owned.append(dropped)
	var event := NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER

	_scenes.core.observe(scene, event, kept.hit)
	_scenes.core.observe(scene, event, dropped.hit)
	_scenes.core.unobserve(scene, event, dropped.hit)
	_dispatch(scene, event, true, 1)
	_row(
		scenario,
		"heard",
		{ &"dropped": dropped.calls.size(), &"kept": kept.calls.size() },
	)

	# Unobserving something never registered is inert rather than an error.
	_scenes.core.unobserve(scene, event, dropped.hit)
	_dispatch(scene, event, true, 2)
	_row(
		scenario,
		"heard_again",
		{ &"dropped": dropped.calls.size(), &"kept": kept.calls.size() },
	)


# An invalid scene or an unset callable is refused at registration, so a
# dispatch cannot later find a row nobody meant to create.
func _an_invalid_scene_or_callback_registers_nothing(scenario: StringName) -> void:
	var scene := _scene()
	var sink := _Sink.new()
	_owned.append(sink)
	var event := NetwMultiplayer.SceneEvent.SCENE_EVENT_PLAYER

	_scenes.core.observe(RID(), event, sink.hit)
	_scenes.core.observe(scene, event, Callable())
	_dispatch(scene, event, true, 1)
	_row(scenario, "heard", { &"count": sink.calls.size() })


# A callable whose object is gone is dropped at the dispatch that finds it, and
# the callbacks beside it still hear the edge.
func _a_dead_callback_is_pruned_at_dispatch(scenario: StringName) -> void:
	var scene := _scene()
	var doomed := _Sink.new()
	var kept := _Sink.new()
	_owned.append(kept)
	var event := NetwMultiplayer.SceneEvent.SCENE_EVENT_ENTITY

	_scenes.core.observe(scene, event, doomed.hit)
	_scenes.core.observe(scene, event, kept.hit)
	doomed.free()

	_dispatch(scene, event, true, 1)
	_row(scenario, "after_death", { &"kept": kept.calls.size() })

	_dispatch(scene, event, true, 2)
	_row(scenario, "after_prune", { &"kept": kept.calls.size() })


func _an_empty_book_dispatches_nothing(scenario: StringName) -> void:
	var scene := _scene()
	for event in EVENTS:
		_dispatch(scene, NetwMultiplayer.SceneEvent[event], true, 1)
	_row(scenario, "survived", { &"events": EVENTS.size() })

#endregion

#region The request record

# Ids are drawn in sequence from one counter, so a reply can name the request it
# answers and a stale one can be told apart from the current one.
func _ids_are_allocated_in_sequence(scenario: StringName) -> void:
	var seen: Array[int] = []
	for step in 4:
		seen.append(_scenes._next_request_id)
		_scenes._next_request_id += 1
	_row(scenario, "allocated", { &"ids": seen })


func _the_deadline_settles_the_request_it_names(scenario: StringName) -> void:
	var promise := _pend(7)

	_scenes._on_request_deadline(7)
	_row(
		scenario,
		"settled",
		{ &"code": error_string(promise.code), &"settled": promise.is_settled },
	)


func _a_stale_deadline_is_ignored(scenario: StringName) -> void:
	var promise := _pend(7)

	_scenes._on_request_deadline(6)
	_row(scenario, "ignored", { &"settled": promise.is_settled })

	# The record is untouched, so the deadline that does name it still lands.
	_scenes._on_request_deadline(7)
	_row(scenario, "then_honoured", { &"settled": promise.is_settled })


# A settled request leaves no id behind, so a second deadline for the same id
# finds nothing rather than settling a request that already answered.
func _a_settled_request_leaves_no_pending_id(scenario: StringName) -> void:
	_pend(7)
	_scenes._on_request_deadline(7)

	_row(
		scenario,
		"cleared",
		{
			&"pending": _scenes._pending_request != null,
			&"pending_id": _scenes._pending_request_id,
		},
	)

#endregion

#region The shell's scene band

# A record with no node yet parks its facet rather than refusing it, which is
# what lets a scene be declared before anything stands in for it.
func _a_wrapperless_record_parks_its_facet(scenario: StringName) -> void:
	var record := _api.entity_create()
	_row(
		scenario,
		"parked",
		{
			&"wrote": _api.scene_declare(record),
			&"held": _api._pending_scene_facets.get(record, null),
			&"reads_declared": _api.scene_is_declared(record),
		},
	)

	_row(
		scenario,
		"unknown",
		{
			&"wrote": _api.scene_declare(RID()),
			&"held": _api._pending_scene_facets.has(RID()),
		},
	)


# The declaration and the stem are the two things the registry files a scene
# under, and undeclaring drops both: a stem outliving its declaration is a scene
# that can still be asked for by name.
func _a_node_backed_scene_declares_and_labels(scenario: StringName) -> void:
	var scene := _live_scene(&"arena")
	var label := NetwMultiplayer.SceneParam.SCENE_PARAM_LABEL
	_row(
		scenario,
		"declared",
		{
			&"is_declared": _api.scene_is_declared(scene),
			&"stem": _api.scene_get_param(scene, label),
		},
	)

	_row(
		scenario,
		"relabelled",
		{
			&"wrote": _api.scene_set_param(scene, label, &"renamed"),
			&"stem": _api.scene_get_param(scene, label),
		},
	)

	_row(
		scenario,
		"undeclared",
		{
			&"wrote": _api.scene_undeclare(scene),
			&"is_declared": _api.scene_is_declared(scene),
			&"stem": _api.scene_get_param(scene, label),
		},
	)

	_row(
		scenario,
		"unknown",
		{
			&"wrote": _api.scene_set_param(RID(), label, &"x"),
			&"stem": _api.scene_get_param(RID(), label),
		},
	)


# Membership is read off the container's subtree rather than off an enrolment
# book, so it is self-inclusive, it answers alike on every peer, and a reparent
# moves it with nothing re-enrolled.
func _membership_walks_the_parent_chain(scenario: StringName) -> void:
	var scene := _live_scene(&"arena")
	var member := _entity_under(_api.entity_get_node(scene))
	var loose := _entity_under(_tree_node)

	_row(
		scenario,
		"resolved",
		{
			&"scene_is_itself": _api.scene_of(scene) == scene,
			&"member_is_the_scene": _api.scene_of(member) == scene,
			&"loose_has_a_scene": _api.scene_of(loose).is_valid(),
			&"encloses": _api.scene_get_entities(scene).size(),
		},
	)

	_api.entity_get_node(member).reparent(_tree_node)
	_row(
		scenario,
		"reparented",
		{
			&"member_is_the_scene": _api.scene_of(member) == scene,
			&"encloses": _api.scene_get_entities(scene).size(),
		},
	)


# Admission is an interest-layer edge, so a scene nobody opened a boundary for
# has nothing to admit into and says so. The verb answers the admission itself
# rather than the attempt, which is why a refusal here is ERR_UNAVAILABLE and
# not a quiet OK over an edge that never landed.
func _admission_needs_a_boundary_to_land_on(scenario: StringName) -> void:
	var scene := _live_scene(&"arena")
	_row(
		scenario,
		"empty",
		{
			&"admits": _api.scene_admits(scene, 7),
			&"peers": _api.scene_get_peers(scene).size(),
			&"layer": _api.scene_get_layer(scene).is_valid(),
		},
	)

	_row(
		scenario,
		"refused",
		{
			&"seven": _api.scene_admit(scene, 7),
			&"zero": _api.scene_admit(scene, 0),
			&"unknown": _api.scene_admit(RID(), 7),
			&"admits_seven": _api.scene_admits(scene, 7),
		},
	)

#endregion

#region Fixtures

## A callback target that remembers what it heard, and in what order relative to
## every other sink. Order across sinks is what a routing table is actually
## about, and it is the thing a per-sink count cannot express.
class _Sink extends Node:
	static var log_order: Array[String] = []

	var calls: Array[String] = []

	func hit(present: bool, subject: Variant) -> void:
		calls.append("%s:%s" % [present, subject])

	func tagged(present: bool, subject: Variant, tag: StringName) -> void:
		calls.append("%s:%s" % [present, subject])
		log_order.append(String(tag))


func _open() -> void:
	_api = NetwMultiplayer.new(SceneMultiplayer.new())
	_scenes = SceneCore.new(_api)
	_Sink.log_order = []


func _close() -> void:
	for node in _owned:
		if is_instance_valid(node):
			node.free()
	_owned.clear()
	_scenes = null
	if is_instance_valid(_api):
		_api.embedding.dispose()
	_api = null


# A scene container is a node, and every membership and admission verb reads the
# node the entity stands on, so the wrapper plane needs a real tree.
func _open_shell() -> void:
	_tree_node = MultiplayerTree.new()
	_tree_node.name = "SceneArm"
	root.add_child(_tree_node)
	_api = _tree_node.api
	_scenes = _api._scenes


# Freed rather than queued, because this runs between scenarios and a bare
# SceneTree driven from _initialize never processes the frame a queue needs.
func _close_shell() -> void:
	for node in _owned:
		if is_instance_valid(node):
			node.free()
	_owned.clear()
	_scenes = null
	_api = null
	root.remove_child(_tree_node)
	_tree_node.free()
	_tree_node = null


# `NetwEntity.ensure` builds the record and `entity_of` introduces it to the api
# answers for, so the node enters the tree before it is named: a verb addressed
# at `entity.rid` beforehand reaches nothing and answers ERR_DOES_NOT_EXIST.
func _live_scene(label: StringName) -> RID:
	var node := Node2D.new()
	node.name = String(label)
	NetwEntity.ensure(node)
	_tree_node.add_child(node)
	_owned.append(node)
	var scene := _api.entity_of(node)
	_api.scene_declare(scene)
	_api.scene_set_param(
		scene, NetwMultiplayer.SceneParam.SCENE_PARAM_LABEL, label
	)
	return scene


func _entity_under(parent: Node) -> RID:
	var node := Node2D.new()
	NetwEntity.ensure(node)
	parent.add_child(node)
	_owned.append(node)
	return _api.entity_of(node)


# Puts one named id in flight and answers the promise it opened.
#
# THE ONE FIXTURE THE PORT FORCED, twice. The record plane used to expose a
# settable pending id, and then a settable pending promise, and a scenario could
# simply plant either. It no longer does, and it should not: a request a caller
# can claim without opening lets it answer one nobody made. Opening is the only
# route now, so the counter is placed and the request drawn from it, which
# reaches the same state through the verb that owns it. The rows this produces
# are the rows recorded before the port, and holding those fixed is the whole
# test. The rejection is absorbed here because an unhandled one warns, and an
# instrument that prints a warning on every run is one whose real failures are
# harder to read.
func _pend(request_id: int) -> NetwPromise:
	_scenes._next_request_id = request_id
	var promise := _scenes.core.request_open(false)
	promise.catch_error(func(_code: Error, _detail: Variant) -> void: pass)
	return promise


# A scene identity. Minted through the session's own handle allocator so it is
# a real RID rather than a fabricated one, and never written to the trace.
func _scene() -> RID:
	return _api.entity_create()


func _dispatch(
		scene: RID,
		event: Variant,
		present: bool,
		subject: Variant,
) -> void:
	_scenes._dispatch_observers(scene, _event_value(event), present, subject)


func _event_value(event: Variant) -> int:
	return NetwMultiplayer.SceneEvent[event] if event is StringName else int(event)

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


func _value(value: Variant) -> String:
	if value is bool:
		return "true" if value else "false"
	if value is Array:
		var parts: Array[String] = []
		for item in value:
			parts.append(str(item))
		return "[%s]" % ",".join(parts)
	return str(value)


func _header() -> String:
	return (
		"# The scene family's Bridge-B trace, recorded from the GDScript scene\n"
		+ "# record plane before any of it was native. Rows are\n"
		+ "# scenario|kind|key=value, keys sorted, events symbolic, scenes named\n"
		+ "# by relation rather than by RID.\n"
		+ "#\n"
		+ "# Regenerate with:\n"
		+ "#   godot --headless --path . -s res://tests/crossing/scene_arm.gd \\\n"
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
