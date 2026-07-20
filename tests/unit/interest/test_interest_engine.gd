## Law suite for the pure [InterestEngine] verdict core.
class_name TestInterestEngine
extends NetwTestSuite

const ROOT := &"root"
const CHILD := &"child"
const LEAF := &"leaf"


func test_l1_determinism_returns_identical_rows() -> void:
	var first := _configured_engine()
	var second := _configured_engine()
	var first_delta := first.recompute()
	var second_delta := second.recompute()

	assert_that(first_delta.to_array()).is_equal(second_delta.to_array())
	first.commit(first_delta)
	second.commit(second_delta)
	assert_that(first.rows()).is_equal(second.rows())


func test_l2_confluence_ignores_mutation_order() -> void:
	var forward := _scripted_engine(false)
	var reverse := _scripted_engine(true)
	var forward_delta := forward.recompute()
	var reverse_delta := reverse.recompute()

	assert_that(forward_delta.to_array()).is_equal(reverse_delta.to_array())


func test_l2_seeded_final_states_match_permuted_replay() -> void:
	var failure := ""
	for seed in 64:
		var forward := _random_state_engine(seed, false)
		var reverse := _random_state_engine(seed, true)
		if forward.recompute().to_array() != reverse.recompute().to_array():
			failure = "seed %d diverged" % seed
			break
	assert_str(failure).is_empty()


func test_l3_repeated_mutations_and_flush_are_idempotent() -> void:
	var engine := _configured_engine()
	var first := engine.recompute()
	engine.commit(first)
	engine.set_membership(ROOT, [&"near"])
	engine.set_layer(
		&"near",
		_bits([0, 2]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	var second := engine.recompute()

	assert_that(second.is_empty()).is_true()
	engine.commit(second)
	assert_that(engine.recompute().is_empty()).is_true()


func test_l4_outsider_policy_is_monotone() -> void:
	var engine := _single_entity_engine(
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	engine.set_layer(
		&"test",
		_bits([0]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	_commit(engine)
	var smaller := engine.row_of(ROOT)
	engine.set_layer(
		&"test",
		_bits([0, 1]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	_commit(engine)
	var larger := engine.row_of(ROOT)

	assert_int(
		InterestBitSet.popcount(
			InterestBitSet.subtract(smaller, larger),
		),
	).is_equal(0)
	assert_that(InterestBitSet.test(larger, 1)).is_true()


func test_l4_insider_policy_is_antitone() -> void:
	var engine := _single_entity_engine(
		InterestEngine.Policy.HIDE_FROM_INSIDERS,
	)
	engine.set_layer(
		&"test",
		_bits([0]),
		InterestEngine.Policy.HIDE_FROM_INSIDERS,
	)
	_commit(engine)
	var larger := engine.row_of(ROOT)
	engine.set_layer(
		&"test",
		_bits([0, 1]),
		InterestEngine.Policy.HIDE_FROM_INSIDERS,
	)
	_commit(engine)
	var smaller := engine.row_of(ROOT)

	assert_int(
		InterestBitSet.popcount(
			InterestBitSet.subtract(smaller, larger),
		),
	).is_equal(0)
	assert_that(InterestBitSet.test(smaller, 1)).is_false()


func test_l5_ancestor_rows_always_clamp_descendants() -> void:
	var engine := _configured_engine()
	_commit(engine)
	var parent_row := engine.row_of(ROOT)
	var child_row := engine.row_of(CHILD)
	var leaf_row := engine.row_of(LEAF)

	assert_int(
		InterestBitSet.popcount(
			InterestBitSet.subtract(child_row, parent_row),
		),
	).is_equal(0)
	assert_int(
		InterestBitSet.popcount(
			InterestBitSet.subtract(leaf_row, child_row),
		),
	).is_equal(0)


func test_l6_entity_without_memberships_is_visible_by_intent() -> void:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1, 2]))
	engine.set_membership(ROOT, [])
	engine.set_order_key(ROOT, 0, 1)
	engine.set_intent(ROOT, _bits([0, 2]))
	_commit(engine)

	assert_that(engine.row_of(ROOT)).is_equal(_bits([0, 2]))


func test_l7_delta_is_exact_desired_xor_committed() -> void:
	var engine := _single_entity_engine(
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	engine.set_layer(
		&"test",
		_bits([0]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	_commit(engine)
	engine.set_layer(
		&"test",
		_bits([1, 2]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	var delta := engine.recompute()

	assert_that(delta.shows).contains_exactly([[ROOT, 1], [ROOT, 2]])
	assert_that(delta.hides).contains_exactly([[ROOT, 0]])
	var difference := InterestBitSet.symmetric_difference(
		delta.old_rows[0],
		delta.new_rows[0],
	)
	assert_int(InterestBitSet.popcount(difference)).is_equal(3)


func test_l8_orders_hides_deep_first_and_shows_shallow_first() -> void:
	var engine := _chain_engine()
	engine.set_membership(ROOT, [&"test"])
	engine.set_membership(CHILD, [&"test"])
	engine.set_membership(LEAF, [&"test"])
	engine.set_layer(
		&"test",
		_bits([0]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	_commit(engine)
	engine.set_layer(
		&"test",
		PackedInt64Array(),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	var hides := engine.recompute()

	assert_that(hides.hides).contains_exactly(
		[[LEAF, 0], [CHILD, 0], [ROOT, 0]],
	)
	engine.commit(hides)
	engine.set_layer(
		&"test",
		_bits([0]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	var shows := engine.recompute()
	assert_that(shows.shows).contains_exactly(
		[[ROOT, 0], [CHILD, 0], [LEAF, 0]],
	)


func test_small_model_matches_oracle_for_every_layer_state() -> void:
	var failure := ""
	for policy_bits in 4:
		for viewer_bits in 16:
			for membership_bits in 16:
				var engine := _small_model_engine(
					policy_bits,
					viewer_bits,
					membership_bits,
				)
				_commit(engine)
				failure = _small_model_failure(
					engine,
					policy_bits,
					viewer_bits,
					membership_bits,
				)
				if not failure.is_empty():
					break
			if not failure.is_empty():
				break
		if not failure.is_empty():
			break
	assert_str(failure).is_empty()


func test_p11_replay_produces_identical_delta_matrix_and_stats() -> void:
	var first := _scripted_engine(false)
	var second := _scripted_engine(false)
	var first_delta := first.recompute()
	var second_delta := second.recompute()
	first.commit(first_delta)
	second.commit(second_delta)

	assert_that(first_delta.to_array()).is_equal(second_delta.to_array())
	assert_that(first.rows()).is_equal(second.rows())
	assert_that(first.stats().to_array()).is_equal(second.stats().to_array())


func test_p12_registration_and_forest_order_do_not_change_output() -> void:
	var first := _permuted_engine([ROOT, CHILD, LEAF], [&"near", &"far"])
	var second := _permuted_engine([LEAF, ROOT, CHILD], [&"far", &"near"])
	var first_delta := first.recompute()
	var second_delta := second.recompute()

	assert_that(first_delta.to_array()).is_equal(second_delta.to_array())


func test_engine_source_obeys_purity_contract() -> void:
	var source := FileAccess.get_file_as_string(
		"res://addons/networked/session/interest/interest_engine.gd",
	)
	source = source.replace("InterestEngine.", "")
	var forbidden := [
		"get_tree",
		"get_node",
		"Engine.",
		"Time.",
		"SceneTree",
		"is_instance_valid",
		".owner",
		"NetwEntity.",
		"liveness",
		".emit(",
		"await",
		"Netw.dbg",
		"call_deferred",
	]
	var found: Array[String] = []
	for token in forbidden:
		if token in source:
			found.append(token)
	assert_array(found).is_empty()


func _configured_engine() -> InterestEngine:
	var engine := _chain_engine()
	engine.set_layer(
		&"near",
		_bits([0, 2]),
		InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
	)
	engine.set_layer(
		&"blind",
		_bits([2]),
		InterestEngine.Policy.HIDE_FROM_INSIDERS,
	)
	engine.set_membership(ROOT, [&"near"])
	engine.set_membership(CHILD, [&"blind"])
	engine.set_membership(LEAF, [])
	engine.set_intent(CHILD, _bits([0, 1]))
	return engine


func _scripted_engine(reverse: bool) -> InterestEngine:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1, 2]))
	var keys := [LEAF, CHILD, ROOT] if reverse else [ROOT, CHILD, LEAF]
	for key in keys:
		var depth := 0 if key == ROOT else 1 if key == CHILD else 2
		engine.set_order_key(key, depth, depth + 1)
	var layers := [&"blind", &"near"] if reverse else [&"near", &"blind"]
	for id in layers:
		if id == &"near":
			engine.set_layer(
				id,
				_bits([0, 2]),
				InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
			)
		else:
			engine.set_layer(
				id,
				_bits([2]),
				InterestEngine.Policy.HIDE_FROM_INSIDERS,
			)
	engine.set_parent(CHILD, ROOT)
	engine.set_parent(LEAF, CHILD)
	engine.set_membership(ROOT, [&"near"])
	engine.set_membership(CHILD, [&"blind"])
	engine.set_membership(LEAF, [])
	engine.set_intent(CHILD, _bits([0, 1]))
	return engine


func _single_entity_engine(policy: int) -> InterestEngine:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1, 2]))
	engine.set_layer(&"test", PackedInt64Array(), policy)
	engine.set_membership(ROOT, [&"test"])
	engine.set_order_key(ROOT, 0, 1)
	return engine


func _random_state_engine(seed: int, reverse: bool) -> InterestEngine:
	var rng := RandomNumberGenerator.new()
	rng.seed = seed
	var layer_rows := [_random_bits(rng), _random_bits(rng)]
	var policies := [rng.randi_range(0, 1), rng.randi_range(0, 1)]
	var memberships: Array = []
	var intents: Array = []
	for entity_index in 3:
		var member_ids: Array[StringName] = []
		for layer_index in 2:
			if rng.randi_range(0, 1) == 1:
				member_ids.append(_layer_id(layer_index))
		memberships.append(member_ids)
		intents.append(_random_bits(rng))
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1, 2]))
	var layer_order := [1, 0] if reverse else [0, 1]
	for layer_index in layer_order:
		engine.set_layer(
			_layer_id(layer_index),
			layer_rows[layer_index],
			policies[layer_index],
		)
	var entity_order := [2, 1, 0] if reverse else [0, 1, 2]
	var keys := [ROOT, CHILD, LEAF]
	for entity_index in entity_order:
		engine.set_order_key(
			keys[entity_index],
			entity_index,
			entity_index + 1,
		)
		engine.set_membership(
			keys[entity_index],
			memberships[entity_index],
		)
		engine.set_intent(keys[entity_index], intents[entity_index])
	engine.set_parent(CHILD, ROOT)
	engine.set_parent(LEAF, CHILD)
	return engine


func _random_bits(rng: RandomNumberGenerator) -> PackedInt64Array:
	var values: Array[int] = []
	for bit in 3:
		if rng.randi_range(0, 1) == 1:
			values.append(bit)
	return _bits(values)


func _chain_engine() -> InterestEngine:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1, 2]))
	for row in [[ROOT, 0, 1], [CHILD, 1, 2], [LEAF, 2, 3]]:
		engine.set_order_key(row[0], row[1], row[2])
	engine.set_parent(CHILD, ROOT)
	engine.set_parent(LEAF, CHILD)
	return engine


func _small_model_engine(
		policy_bits: int,
		viewer_bits: int,
		membership_bits: int,
) -> InterestEngine:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1]))
	engine.set_order_key(ROOT, 0, 1)
	engine.set_order_key(CHILD, 1, 2)
	engine.set_parent(CHILD, ROOT)
	for layer_index in 2:
		var viewers: Array[int] = []
		for peer_bit in 2:
			if (viewer_bits & (1 << (layer_index * 2 + peer_bit))) != 0:
				viewers.append(peer_bit)
		var policy := (policy_bits >> layer_index) & 1
		engine.set_layer(
			_layer_id(layer_index),
			_bits(viewers),
			policy,
		)
	for entity_index in 2:
		var memberships: Array[StringName] = []
		for layer_index in 2:
			var offset := entity_index * 2 + layer_index
			if (membership_bits & (1 << offset)) != 0:
				memberships.append(_layer_id(layer_index))
		engine.set_membership(_entity_key(entity_index), memberships)
	return engine


func _small_model_failure(
		engine: InterestEngine,
		policy_bits: int,
		viewer_bits: int,
		membership_bits: int,
) -> String:
	for entity_index in 2:
		for peer_bit in 2:
			var expected := _oracle_grant(
				entity_index,
				peer_bit,
				policy_bits,
				viewer_bits,
				membership_bits,
			)
			if entity_index == 1:
				expected = expected and engine.test(ROOT, peer_bit)
			if engine.test(_entity_key(entity_index), peer_bit) != expected:
				return "state %d/%d/%d entity %d peer %d" % [
					policy_bits,
					viewer_bits,
					membership_bits,
					entity_index,
					peer_bit,
				]
	return ""


func _oracle_grant(
		entity_index: int,
		peer_bit: int,
		policy_bits: int,
		viewer_bits: int,
		membership_bits: int,
) -> bool:
	var has_membership := false
	var admitted := false
	for layer_index in 2:
		var member_offset := entity_index * 2 + layer_index
		if (membership_bits & (1 << member_offset)) == 0:
			continue
		has_membership = true
		var viewer_offset := layer_index * 2 + peer_bit
		var viewer := (viewer_bits & (1 << viewer_offset)) != 0
		var policy := (policy_bits >> layer_index) & 1
		admitted = admitted or (
				viewer
				if policy == InterestEngine.Policy.HIDE_FROM_OUTSIDERS
				else not viewer
		)
	return admitted if has_membership else true


func _permuted_engine(keys: Array, layers: Array) -> InterestEngine:
	var engine := InterestEngine.new()
	engine.set_live_peers(_bits([0, 1]))
	for id in layers:
		var viewers := _bits([0]) if id == &"near" else _bits([1])
		engine.set_layer(
			id,
			viewers,
			InterestEngine.Policy.HIDE_FROM_OUTSIDERS,
		)
	for key in keys:
		var depth := 0 if key == ROOT else 1 if key == CHILD else 2
		engine.set_order_key(key, depth, depth + 1)
	engine.set_parent(CHILD, ROOT)
	engine.set_parent(LEAF, CHILD)
	engine.set_membership(ROOT, [&"near"])
	engine.set_membership(CHILD, [&"near", &"far"])
	engine.set_membership(LEAF, [&"far"])
	return engine


func _commit(engine: InterestEngine) -> void:
	engine.commit(engine.recompute())


func _bits(values: Array[int]) -> PackedInt64Array:
	var out := PackedInt64Array()
	for bit in values:
		out = InterestBitSet.with_bit(out, bit)
	return out


func _layer_id(index: int) -> StringName:
	return &"a" if index == 0 else &"b"


func _entity_key(index: int) -> StringName:
	return ROOT if index == 0 else CHILD
