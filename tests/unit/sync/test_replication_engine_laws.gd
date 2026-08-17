## Laws of the sealed sync and spawn engines whose subject is a node or a
## registration.
##
## Every case here asserts a materialization order, an admission, a node's
## fields, or the shape of a declaration, so the suite drives the engines
## through a live [MultiplayerTree]. The laws whose subject is the frame itself
## live in [code]tests/unit/wire/test_replication_wire_laws.gd[/code].
class_name TestReplicationEngineLaws
extends NetwTestSuite

var mt: MultiplayerTree
var api: NetwMultiplayer


## Build each law through the implementation selected for this run.
func before_test() -> void:
	mt = MultiplayerTree.new()
	mt.name = "ReplicationLawTree"
	add_child(mt)
	auto_free(mt)
	api = mt.api


## L5. Materialization order is parent-first on gain and child-first on loss.
func test_l5_ordering() -> void:
	var plan := NetwSpawnPlanner.reconcile(
		_nested_rows([2], [2], false, false),
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"despawn")).contains_exactly([2, 1])
	plan = NetwSpawnPlanner.reconcile(
		_nested_rows([], [], true, true),
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"spawn")).contains_exactly([1, 2])


## L5b. The book answers that order from ancestry, not from arm order, so a
## record reparented under a later-armed record still reaches the reconciler
## behind its parent.
func test_l5b_book_orders_by_ancestry() -> void:
	var book := NetwSpawnBook.new()
	for route in [1, 2, 3]:
		var record := NetwSpawnRecord.new()
		record.route = route
		book.issue(record)
	assert_array(book.ancestry_order()).contains_exactly([1, 2, 3])

	# Route 3 is armed last, and route 2 moves under it.
	book.spawned_of(2).parent_route = 3
	assert_array(book.ancestry_order()).contains_exactly([1, 3, 2])

	# A whole chain armed back to front still comes out root first.
	var reversed := NetwSpawnBook.new()
	for route in [1, 2, 3, 4]:
		var record := NetwSpawnRecord.new()
		record.route = route
		record.parent_route = route + 1 if route < 4 else 0
		reversed.issue(record)
	assert_array(reversed.ancestry_order()).contains_exactly([4, 3, 2, 1])


## L6. A recipient exists only when every supplied admission term agrees.
func test_l6_recipients() -> void:
	var rows := _nested_rows([], [], true, false)
	var plan := NetwSpawnPlanner.reconcile(
		rows,
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"spawn")).contains_exactly([1])


## L7. Flat author policy resolves authority and controller soundly.
func test_l7_author_gate_soundness() -> void:
	var root := make_test_entity(mt, "AuthorEntity", 0, false)
	var entity := api.entity_of(root)
	api.entity_bind_route(entity, 71)
	root.set_multiplayer_authority(4) # SMELL(authority-pin): policy fixture
	NetwEntity.of(root).controller = 5
	assert_bool(
		api.sync_policy_admits(
			NetwMultiplayer.WritePolicy.AUTHORITY,
			4,
			entity,
			0,
		),
	).is_true()
	assert_bool(
		api.sync_policy_admits(
			NetwMultiplayer.WritePolicy.CONTROLLER,
			5,
			entity,
			0,
		),
	).is_true()
	assert_bool(
		api.sync_policy_admits(
			NetwMultiplayer.WritePolicy.CONTROLLER,
			4,
			entity,
			0,
		),
	).is_false()


## L9. Reconciliation conserves gained, lost, and retained rows.
func test_l9_reconciliation_conservation() -> void:
	var rows := _single_row([2, 3], { 2: true, 3: false, 4: true })
	rows[0][&"leave"][3] = { &"despawn": false, &"custom": [] }
	var plan := NetwSpawnPlanner.reconcile(
		rows,
		PackedInt32Array([2, 3, 4]),
	)
	assert_int(_op_count(plan, &"spawn")).is_equal(1)
	assert_int(_op_count(plan, &"retain")).is_equal(1)
	assert_int(_op_count(plan, &"despawn")).is_equal(0)


## C1. A hidden parent clamps a locally desired child.
func test_c1_hidden_parent_clamps_child() -> void:
	var plan := NetwSpawnPlanner.reconcile(
		_nested_rows([], [], false, true),
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"spawn")).is_empty()


## C2. A visible nested pair gains parent before child.
func test_c2_nested_gain_is_parent_first() -> void:
	var plan := NetwSpawnPlanner.reconcile(
		_nested_rows([], [], true, true),
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"spawn")).contains_exactly([1, 2])


## C3. A parent despawn forces a retained child to despawn.
func test_c3_parent_despawn_forces_child() -> void:
	var rows := _nested_rows([2], [2], false, true)
	rows[1][&"leave"][2] = { &"despawn": false, &"custom": [] }
	var plan := NetwSpawnPlanner.reconcile(
		rows,
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"despawn")).contains_exactly([2, 1])


## C4. A retained parent permits its locally desired child to retain.
func test_c4_retained_parent_retains_child() -> void:
	var rows := _nested_rows([2], [2], false, true)
	rows[0][&"leave"][2] = { &"despawn": false, &"custom": [] }
	rows[1][&"leave"][2] = { &"despawn": false, &"custom": [] }
	var plan := NetwSpawnPlanner.reconcile(
		rows,
		PackedInt32Array([2]),
	)
	assert_int(_op_count(plan, &"retain")).is_equal(2)


## C5. A child can leave while its parent stays materialized.
func test_c5_child_leave_preserves_parent() -> void:
	var plan := NetwSpawnPlanner.reconcile(
		_nested_rows([2], [2], true, false),
		PackedInt32Array([2]),
	)
	assert_array(_op_routes(plan, &"despawn")).contains_exactly([2])


## The installed implementation owns the flat sync and spawn stages.
func test_installed_implementation_reaches_flat_stages() -> void:
	var root := Node2D.new()
	root.name = "FlatStageEntity"
	mt.add_child(root)
	auto_free(root)
	NetwEntity.ensure(root)
	var entity := api.entity_of(root)
	api.entity_bind_route(entity, 72)
	var schema := api.schema_create(&"FlatStageEntity")
	var position := api.schema_add_column(
		schema,
		&"position",
		NetwMultiplayer.ColumnType.COLUMN_VECTOR2,
	)
	api.schema_seal(schema)
	var set := api.property_set_create(schema, NetwMultiplayer.RecordKind.STATE)
	api.property_set_add_column(set, position)
	api.property_set_seal(set)
	assert_int(api.entity_add_property_set(entity, set, 0)).is_equal(OK)
	api._sync_encoder = func(_peer: int, _tick: int) -> PackedByteArray:
		return PackedByteArray([1, 2, 3])
	assert_array(api._sync_encode(2, 4)).is_equal(PackedByteArray([1, 2, 3]))
	api._sync_encoder = Callable()
	var gathered := api._run_gather_set(
		entity,
		0,
		func() -> Array: return [root.position],
	)
	assert_array(gathered).is_equal([Vector2.ZERO])
	var applied := api._run_apply_set(
		entity,
		0,
		[Vector2(3.0, 4.0)],
		func(values: Array) -> Error:
			root.position = values[0]
			return OK,
	)
	assert_int(applied).is_equal(OK)
	assert_vector(root.position).is_equal(Vector2(3.0, 4.0))
	assert_int(api._spawn_declare(entity, { &"recipe": 0 })).is_equal(OK)


func _single_row(recipients: Array, desired: Dictionary) -> Array[Dictionary]:
	return [
		{
			&"route": 1,
			&"parent_route": 0,
			&"recipients": recipients,
			&"local_desired": desired,
			&"leave": { },
		},
	]


func _nested_rows(
		parent_recipients: Array,
		child_recipients: Array,
		parent_desired: bool,
		child_desired: bool,
) -> Array[Dictionary]:
	return [
		{
			&"route": 1,
			&"parent_route": 0,
			&"recipients": parent_recipients,
			&"local_desired": { 2: parent_desired },
			&"leave": { },
		},
		{
			&"route": 2,
			&"parent_route": 1,
			&"recipients": child_recipients,
			&"local_desired": { 2: child_desired },
			&"leave": { },
		},
	]


func _op_routes(plan: Array, action: StringName) -> Array[int]:
	var routes: Array[int] = []
	for operation: Dictionary in plan:
		if operation[&"action"] == action:
			routes.append(int(operation[&"route"]))
	return routes


func _op_count(plan: Array, action: StringName) -> int:
	return _op_routes(plan, action).size()
