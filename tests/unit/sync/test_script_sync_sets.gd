## Derivation tests for a script's state and input sets, the per-script
## [NetwPropertySet] built from the [NetwScriptModel.PropertyConfig] rows a script
## declares through [method Netw.configure_property].
##
## [method NetwPropertySet.from_property_configs] collects the fields carrying the
## kind's mark ([method NetwScriptModel.PropertyConfig.state],
## [method NetwScriptModel.PropertyConfig.input], or
## [method NetwScriptModel.PropertyConfig.broadcast]) in declaration order, stamps
## the kind's axis presets, routes each field onto its declared
## [member NetwPropertySet.Column.lane], and reconciles the set-level knobs a member
## wrote through so the last member to name a knob owns it.
##
## The suite also covers what a derived set writes onto a live node:
## [method NetwSyncPipeline.register_derived]'s binding of the declared sets,
## [method NetwPropertySetBinding.apply_volatile]'s snap and its write gate, and
## the plain-payload [method NetwSyncPipeline.gather_payload] and
## [method NetwSyncPipeline.apply_payload] (with their
## [method NetwPropertySetBinding.snapshot_payload] /
## [method NetwPropertySetBinding.apply_payload] handles) a prediction step records
## and reconciles against. The bytes those sets frame, and the per-peer masked
## books that fill them, are [code]tests/unit/wire/test_script_sync_wire.gd[/code].
class_name TestScriptSyncSets
extends NetwTestSuite

const STATE := NetwPropertySet.Record.RECORD_STATE
const INPUT := NetwPropertySet.Record.RECORD_INPUT
const BROADCAST := NetwPropertySet.Record.RECORD_BROADCAST


func _prop() -> NetwScriptModel.PropertyConfig:
	var config := NetwScriptModel.PropertyConfig.new()
	config.is_property = true
	return config


func _make_set(fields: Array) -> NetwPropertySet:
	# Builds a set directly from [key, lane, type?, quantizer?] rows.
	var set := NetwPropertySet.new()
	for pair: Array in fields:
		var column := NetwPropertySet.Column.new(
			pair[0],
			pair[3] if pair.size() > 3 else null,
			false,
			pair[2] if pair.size() > 2 else SchemaCore.ColumnType.VARIANT,
		)
		column.lane = pair[1]
		set.bind(column)
	return set


func test_state_set_collects_marked_fields_in_declaration_order() -> void:
	var configs: Dictionary = {
		&"position": _prop().state(),
		&"health": _prop().retained(), # unmarked for state
		&"rotation": _prop().state(),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	assert_array(set.keys()).is_equal([&"position", &"rotation"])
	assert_int(set.record).is_equal(STATE)
	assert_int(set.profile).is_equal(NetwPropertySet.Profile.STAMPED)
	assert_int(set.stamp).is_equal(NetwPropertySet.Stamp.STAMP_TICK_ACK)
	assert_int(set.cadence).is_equal(NetwPropertySet.Cadence.TICK)
	assert_int(set.trigger).is_equal(NetwPropertySet.Trigger.TRIGGER_ON_CHANGE)
	assert_int(set.audience).is_equal(NetwPropertySet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.AUTHORITY)
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)


func test_input_set_stamps_tick_server_only_controller() -> void:
	var configs: Dictionary = {
		&"move": _prop().input(),
		&"aim": _prop().input(),
	}
	var set := NetwPropertySet.from_property_configs(configs, INPUT)
	assert_array(set.keys()).is_equal([&"move", &"aim"])
	assert_int(set.record).is_equal(INPUT)
	assert_int(set.stamp).is_equal(NetwPropertySet.Stamp.STAMP_TICK)
	assert_int(set.audience).is_equal(NetwPropertySet.Audience.AUDIENCE_SERVER_ONLY)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)


func test_broadcast_set_derives_public_controller_tick_and_passes_masked() -> void:
	# A broadcast frames the bare tick (it never reconciles, so an ack slot would
	# be a lie), reaches every recipient, defaults to the controlling player, never
	# windows, and passes masked through as the whole point of the kind.
	var set := NetwPropertySet.from_property_configs(
		{
			&"aim_dir": _prop().broadcast().masked(),
		},
		BROADCAST,
	)
	assert_array(set.keys()).is_equal([&"aim_dir"])
	assert_int(set.record).is_equal(BROADCAST)
	assert_int(set.stamp).is_equal(NetwPropertySet.Stamp.STAMP_TICK)
	assert_int(set.audience).is_equal(NetwPropertySet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)
	assert_int(set.window).is_equal(0)
	assert_bool(set.masked).is_true()
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)

	# A member policy override still wins the way it does for state.
	var authed := NetwPropertySet.from_property_configs(
		{
			&"pose": _prop().broadcast().authority(),
		},
		BROADCAST,
	)
	assert_int(authed.policy).is_equal(NetwScriptModel.Policy.AUTHORITY)


func test_broadcast_forces_public_and_ignores_windowed() -> void:
	# A server-only volatile stream is an input set wearing a costume, so a
	# broadcast lints and stays public. A window heals against a redundant sample,
	# which the masked lane replaces with its confirmed baseline, so windowed lints
	# and is dropped.
	var set := NetwPropertySet.from_property_configs(
		{
			&"aim": _prop().broadcast().audience(true).windowed(3),
		},
		BROADCAST,
	)
	assert_int(set.audience).is_equal(NetwPropertySet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.window).is_equal(0)


func test_broadcast_mark_is_mutually_exclusive_with_recorded_marks() -> void:
	# A field marked broadcast and also state drops its broadcast mark: the
	# recorded kind is correctness-critical and wins, so no broadcast set derives
	# for it while the state set still collects it.
	var configs: Dictionary = {
		&"position": _prop().state().broadcast(),
	}
	assert_that(NetwPropertySet.from_property_configs(configs, BROADCAST)).is_null()
	var state_set := NetwPropertySet.from_property_configs(configs, STATE)
	assert_array(state_set.keys()).is_equal([&"position"])


func test_register_derived_binds_a_broadcast_set_without_a_timeline() -> void:
	# A broadcast binding registers like state and input but calls no timeline
	# hook, the rewind boundary in code: a trusted display stream records nothing.
	var pipeline := NetwSyncPipeline.new(null)
	var node := BroadcastAimBody.new()
	add_child(node)
	auto_free(node)
	pipeline.register_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(1)
	assert_that(pipeline.derived_binding(node, BROADCAST)).is_not_null()
	assert_that(pipeline.derived_binding(node, STATE)).is_null()
	pipeline.dispose()


func test_derived_pump_skip_without_entity_is_counted() -> void:
	var mt := MultiplayerTree.new()
	add_child(mt)
	auto_free(mt)
	var node := BroadcastAimBody.new()
	mt.add_child(node)
	auto_free(node)
	var pipeline := NetwSyncPipeline.new(mt.api)
	pipeline.register_derived(node)

	pipeline.pump(1)

	assert_int(
		pipeline.counters()[&"sync_pump_skips_no_entity"],
	).is_equal(1)
	pipeline.dispose()


func test_no_marked_field_derives_nothing() -> void:
	var configs: Dictionary = {
		&"health": _prop().retained(),
		&"gold": _prop().persisted(),
	}
	assert_that(NetwPropertySet.from_property_configs(configs, STATE)).is_null()
	assert_that(NetwPropertySet.from_property_configs(configs, INPUT)).is_null()


func test_field_lane_drives_watch() -> void:
	var configs: Dictionary = {
		&"position": _prop().state(), # volatile by default
		&"inventory": _prop().state().retained(),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	var by_key: Dictionary = { }
	for field in set.columns:
		by_key[field.key] = field
	assert_bool(by_key[&"position"].watch).is_false()
	assert_int(by_key[&"position"].lane).is_equal(NetwPropertySet.Lane.VOLATILE)
	assert_bool(by_key[&"inventory"].watch).is_true()
	assert_int(by_key[&"inventory"].lane).is_equal(NetwPropertySet.Lane.RETAINED)


func test_quantizers_are_parallel_to_keys() -> void:
	var codec := NetwQuantizeFixed.new()
	var configs: Dictionary = {
		&"position": _prop().state().quantize(codec),
		&"rotation": _prop().state(),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	var quantizers := set.quantizers()
	var keys := set.keys()
	assert_that(quantizers[keys.find(&"position")]).is_equal(codec)
	assert_that(quantizers[keys.find(&"rotation")]).is_null()


func test_windowed_writes_through_to_the_input_set() -> void:
	var configs: Dictionary = {
		&"move": _prop().input().windowed(3),
	}
	var set := NetwPropertySet.from_property_configs(configs, INPUT)
	assert_int(set.window).is_equal(3)


func test_member_policy_override_wins_over_the_kind_default() -> void:
	var configs: Dictionary = {
		&"position": _prop().state().controller(),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)


func test_state_audience_narrows_when_a_member_asks() -> void:
	var configs: Dictionary = {
		&"secret": _prop().state().audience(true),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	assert_int(set.audience).is_equal(NetwPropertySet.Audience.AUDIENCE_SERVER_ONLY)


func test_last_member_owns_a_conflicting_knob() -> void:
	# Two members name the window differently. The derivation warns and the last
	# one owns it, the reconciliation the per-set pump relies on.
	var configs: Dictionary = {
		&"move": _prop().input().windowed(2),
		&"aim": _prop().input().windowed(5),
	}
	var set := NetwPropertySet.from_property_configs(configs, INPUT)
	assert_int(set.window).is_equal(5)


func test_register_derived_binds_declared_state_and_input_sets() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	var node: Node = preload("res://tests/support/sync/derived_state_player.gd").new()
	node.name = "PredictedWithoutComponent"
	add_child(node)
	auto_free(node)

	pipeline.register_derived(node)
	assert_str(
		NetwSyncPipeline._missing_prediction_component_message(
			"PredictedWithoutComponent",
		),
	).is_equal(
		"Prediction: PredictedWithoutComponent declares state() and input() "
		+ "fields but carries no PredictionComponent. Its controller will author "
		+ "commands without simulating the predicted state. Add the component or "
		+ "register prediction from code.",
	)
	# The script marks one state set (position) and one input set (rotation).
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(2)

	# Idempotent: a second registration adds nothing.
	pipeline.register_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(2)

	pipeline.unregister_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(0)
	pipeline.dispose()


func test_register_derived_ignores_a_scriptless_node() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	pipeline.register_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(0)
	pipeline.dispose()


func test_binding_applies_a_volatile_state_row() -> void:
	var set := NetwPropertySet.from_property_configs({ &"position": _prop().state() }, STATE)
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(5, -6)
	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)

	var bytes := NetwPropertySetBinding.new(set, src).encode_volatile(3, 20, 17)
	var header := NetwPropertySetBinding.new(set, dst).apply_volatile(bytes)
	assert_int(header["ordinal"]).is_equal(3)
	assert_int(header["ack"]).is_equal(17)
	assert_that(dst.position).is_equal(Vector2(5, -6))


func test_binding_applies_the_freshest_windowed_sample() -> void:
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK
	set.window = 3
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var src_binding := NetwPropertySetBinding.new(set, src)
	src.position = Vector2(1, 0)
	src_binding.encode_volatile(0, 10, -1)
	src.position = Vector2(2, 0)
	var bytes := src_binding.encode_volatile(0, 11, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwPropertySetBinding.new(set, dst).apply_volatile(bytes)
	# The freshest sample (age 0, tick 11) is the row displayed.
	assert_that(dst.position).is_equal(Vector2(2, 0))
	assert_int(header["tick"]).is_equal(11)


func test_binding_applies_a_retained_delta() -> void:
	var set := _make_set([[&"rotation", NetwPropertySet.Lane.RETAINED]])
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.rotation = 1.25
	var src_binding := NetwPropertySetBinding.new(set, src)
	src_binding.poll_retained()
	var bytes := src_binding.retained_delta(0, 2)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	assert_bool(NetwPropertySetBinding.new(set, dst).apply_retained_delta(bytes)).is_true()
	assert_float(dst.rotation).is_equal_approx(1.25, 0.0001)


func test_gather_payload_reads_every_set_field() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(2, 5)
	node.rotation = 0.75
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	var payload := NetwSyncPipeline.gather_payload(node, set)
	assert_int(payload.size()).is_equal(2)
	assert_that(payload[&"position"]).is_equal(Vector2(2, 5))
	assert_float(payload[&"rotation"]).is_equal_approx(0.75, 0.0001)


func test_gather_payload_skips_a_field_missing_on_the_node() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 1)
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"nonexistent", NetwPropertySet.Lane.VOLATILE],
		],
	)
	var payload := NetwSyncPipeline.gather_payload(node, set)
	assert_int(payload.size()).is_equal(1)
	assert_bool(payload.has(&"position")).is_true()
	assert_bool(payload.has(&"nonexistent")).is_false()


func test_apply_payload_writes_only_declared_fields() -> void:
	# A superset payload with a key the set does not name must not write a stray
	# property, the way a synchronizer restore ignores unregistered payload keys.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(0, 0)
	node.rotation = 0.0
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	NetwSyncPipeline.apply_payload(
		node,
		set,
		{
			&"position": Vector2(9, 9),
			&"rotation": 3.0, # not in the set, ignored
		},
	)
	assert_that(node.position).is_equal(Vector2(9, 9))
	assert_float(node.rotation).is_equal_approx(0.0, 0.0001)


func test_binding_payload_snapshots_and_restores_between_nodes() -> void:
	var set := _make_set(
		[
			[&"position", NetwPropertySet.Lane.VOLATILE],
			[&"rotation", NetwPropertySet.Lane.VOLATILE],
		],
	)
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(-6, 2)
	src.rotation = 2.0
	var snapshot := NetwPropertySetBinding.new(set, src).snapshot_payload()

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	NetwPropertySetBinding.new(set, dst).apply_payload(snapshot)
	assert_that(dst.position).is_equal(Vector2(-6, 2))
	assert_float(dst.rotation).is_equal_approx(2.0, 0.0001)


func test_binding_apply_hook_fires_with_the_decoded_payload() -> void:
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK_ACK
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(4, 9)
	var bytes := NetwPropertySetBinding.new(set, src).encode_volatile(0, 12, 7)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	var seen: Array = []
	dst_binding.on_applied = func(h: Dictionary) -> void: seen.append(h)
	dst_binding.apply_volatile(bytes)
	assert_int(seen.size()).is_equal(1)
	var header: Dictionary = seen[0]
	assert_int(header["tick"]).is_equal(12)
	assert_int(header["ack"]).is_equal(7)
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(4, 9))
	# write_gate defaults true, so a plain display receive snaps the node.
	assert_that(dst.position).is_equal(Vector2(4, 9))


func test_binding_write_gate_suppresses_the_node_snap() -> void:
	# The predicting client decodes the authoritative row and hands it to its
	# reconciler without snapping the predicted body.
	var set := _make_set([[&"position", NetwPropertySet.Lane.VOLATILE]])
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK_ACK
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(5, 5)
	var bytes := NetwPropertySetBinding.new(set, src).encode_volatile(0, 3, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	dst.position = Vector2(-1, -1)
	var dst_binding := NetwPropertySetBinding.new(set, dst)
	dst_binding.write_gate = false
	var seen: Array = []
	dst_binding.on_applied = func(h: Dictionary) -> void: seen.append(h)
	var header := dst_binding.apply_volatile(bytes)
	assert_that(dst.position).is_equal(Vector2(-1, -1))
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(5, 5))
	assert_int(seen.size()).is_equal(1)


func test_binding_windowed_apply_returns_every_sample() -> void:
	# A windowed input frame carries a ring of redundant samples so the input
	# engine can record every tick and heal one the receiver missed.
	var set := _make_set([[&"rotation", NetwPropertySet.Lane.VOLATILE]])
	set.stamp = NetwPropertySet.Stamp.STAMP_TICK
	set.window = 3
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var src_binding := NetwPropertySetBinding.new(set, src)
	src.rotation = 0.1
	src_binding.encode_volatile(0, 10, -1)
	src.rotation = 0.2
	src_binding.encode_volatile(0, 11, -1)
	src.rotation = 0.3
	var bytes := src_binding.encode_volatile(0, 12, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwPropertySetBinding.new(set, dst).apply_volatile(bytes)
	var samples: Array = header["samples"]
	assert_int(samples.size()).is_equal(3)
	# Newest first: tick 12 at age 0, then 11, then 10.
	assert_int(samples[0]["tick"]).is_equal(12)
	assert_int(samples[2]["tick"]).is_equal(10)
	assert_float((samples[0]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(0.3, 0.0001)
	assert_float((samples[2]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(0.1, 0.0001)
	# The freshest sample snaps the display node.
	assert_float(dst.rotation).is_equal_approx(0.3, 0.0001)


func test_masked_sugar_writes_through_and_conflicts_with_windowed() -> void:
	var configs: Dictionary = {
		&"position": _prop().state().masked(),
	}
	var set := NetwPropertySet.from_property_configs(configs, STATE)
	assert_bool(set.masked).is_true()

	# masked() and windowed() are mutually exclusive; the derivation warns
	# and drops masked rather than crash or silently combine both.
	var conflicting: Dictionary = {
		&"move": _prop().input().masked().windowed(3),
	}
	var conflicting_set := NetwPropertySet.from_property_configs(conflicting, INPUT)
	assert_bool(conflicting_set.masked).is_false()
	assert_int(conflicting_set.window).is_equal(3)
