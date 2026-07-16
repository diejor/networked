## Derivation tests for a script's state and input sets, the per-script
## [NetwSyncSet] built from the [NetwScriptModel.PropertyConfig] rows a script
## declares through [method Netw.configure_property].
##
## [method NetwSyncSet.from_property_configs] collects the fields carrying the
## kind's mark ([method NetwScriptModel.PropertyConfig.state],
## [method NetwScriptModel.PropertyConfig.input], or
## [method NetwScriptModel.PropertyConfig.broadcast]) in declaration order, stamps
## the kind's axis presets, routes each field onto its declared
## [member NetwSyncSet.Field.lane], and reconciles the set-level knobs a member
## wrote through so the last member to name a knob owns it.
##
## The suite also covers [method NetwSyncPipeline.encode_volatile_frame] and
## [method NetwSyncPipeline.apply_volatile_frame], the gather and apply that carry
## a derived set's volatile row over the [constant NetwFrameEnvelope.Channel.SYNC]
## frame, round-tripping a stamped state row through a live node, plus the
## plain-payload [method NetwSyncPipeline.gather_payload] and
## [method NetwSyncPipeline.apply_payload] (and their
## [method NetwSyncSetBinding.snapshot_payload] /
## [method NetwSyncSetBinding.apply_payload] handles) a prediction step records and
## reconciles against, plus the masked per-recipient volatile diff lane:
## [method NetwSyncSetBinding.masked_delta]'s gain-edge heal and ack-gated
## baseline advance, and [method NetwSyncSetBinding.apply_volatile]'s merge of a
## partial masked frame onto the receiver's own last-decoded row.
class_name TestScriptSyncSets
extends NetwTestSuite

const STATE := NetwSyncSet.Record.RECORD_STATE
const INPUT := NetwSyncSet.Record.RECORD_INPUT
const BROADCAST := NetwSyncSet.Record.RECORD_BROADCAST


func _prop() -> NetwScriptModel.PropertyConfig:
	var config := NetwScriptModel.PropertyConfig.new()
	config.is_property = true
	return config


func test_state_set_collects_marked_fields_in_declaration_order() -> void:
	var configs: Dictionary = {
		&"position": _prop().state(),
		&"health": _prop().retained(),  # unmarked for state
		&"rotation": _prop().state(),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	assert_array(set.keys()).is_equal([&"position", &"rotation"])
	assert_int(set.record).is_equal(STATE)
	assert_int(set.profile).is_equal(NetwSyncSet.Profile.STAMPED)
	assert_int(set.stamp).is_equal(NetwSyncSet.Stamp.STAMP_TICK_ACK)
	assert_int(set.cadence).is_equal(NetwSyncSet.Cadence.TICK)
	assert_int(set.trigger).is_equal(NetwSyncSet.Trigger.TRIGGER_ON_CHANGE)
	assert_int(set.audience).is_equal(NetwSyncSet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.AUTHORITY)
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)


func test_input_set_stamps_tick_server_only_controller() -> void:
	var configs: Dictionary = {
		&"move": _prop().input(),
		&"aim": _prop().input(),
	}
	var set := NetwSyncSet.from_property_configs(configs, INPUT)
	assert_array(set.keys()).is_equal([&"move", &"aim"])
	assert_int(set.record).is_equal(INPUT)
	assert_int(set.stamp).is_equal(NetwSyncSet.Stamp.STAMP_TICK)
	assert_int(set.audience).is_equal(NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)


func test_broadcast_set_derives_public_controller_tick_and_passes_masked() -> void:
	# A broadcast frames the bare tick (it never reconciles, so an ack slot would
	# be a lie), reaches every recipient, defaults to the controlling player, never
	# windows, and passes masked through as the whole point of the kind.
	var set := NetwSyncSet.from_property_configs({
		&"aim_dir": _prop().broadcast().masked(),
	}, BROADCAST)
	assert_array(set.keys()).is_equal([&"aim_dir"])
	assert_int(set.record).is_equal(BROADCAST)
	assert_int(set.stamp).is_equal(NetwSyncSet.Stamp.STAMP_TICK)
	assert_int(set.audience).is_equal(NetwSyncSet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)
	assert_int(set.window).is_equal(0)
	assert_bool(set.masked).is_true()
	assert_int(set.channel).is_equal(NetwFrameEnvelope.Channel.SYNC)

	# A member policy override still wins the way it does for state.
	var authed := NetwSyncSet.from_property_configs({
		&"pose": _prop().broadcast().authority(),
	}, BROADCAST)
	assert_int(authed.policy).is_equal(NetwScriptModel.Policy.AUTHORITY)


func test_broadcast_forces_public_and_ignores_windowed() -> void:
	# A server-only volatile stream is an input set wearing a costume, so a
	# broadcast lints and stays public. A window heals against a redundant sample,
	# which the masked lane replaces with its confirmed baseline, so windowed lints
	# and is dropped.
	var set := NetwSyncSet.from_property_configs({
		&"aim": _prop().broadcast().audience(true).windowed(3),
	}, BROADCAST)
	assert_int(set.audience).is_equal(NetwSyncSet.Audience.AUDIENCE_PUBLIC)
	assert_int(set.window).is_equal(0)


func test_broadcast_mark_is_mutually_exclusive_with_recorded_marks() -> void:
	# A field marked broadcast and also state drops its broadcast mark: the
	# recorded kind is correctness-critical and wins, so no broadcast set derives
	# for it while the state set still collects it.
	var configs: Dictionary = {
		&"position": _prop().state().broadcast(),
	}
	assert_that(NetwSyncSet.from_property_configs(configs, BROADCAST)).is_null()
	var state_set := NetwSyncSet.from_property_configs(configs, STATE)
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


func test_no_marked_field_derives_nothing() -> void:
	var configs: Dictionary = {
		&"health": _prop().retained(),
		&"gold": _prop().persisted(),
	}
	assert_that(NetwSyncSet.from_property_configs(configs, STATE)).is_null()
	assert_that(NetwSyncSet.from_property_configs(configs, INPUT)).is_null()


func test_field_lane_drives_watch() -> void:
	var configs: Dictionary = {
		&"position": _prop().state(),           # volatile by default
		&"inventory": _prop().state().retained(),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	var by_key: Dictionary = { }
	for field in set.fields:
		by_key[field.key] = field
	assert_bool(by_key[&"position"].watch).is_false()
	assert_int(by_key[&"position"].lane).is_equal(NetwSyncSet.Lane.VOLATILE)
	assert_bool(by_key[&"inventory"].watch).is_true()
	assert_int(by_key[&"inventory"].lane).is_equal(NetwSyncSet.Lane.RETAINED)


func test_quantizers_are_parallel_to_keys() -> void:
	var codec := NetwQuantizeFixed.new()
	var configs: Dictionary = {
		&"position": _prop().state().quantize(codec),
		&"rotation": _prop().state(),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	var quantizers := set.quantizers()
	var keys := set.keys()
	assert_that(quantizers[keys.find(&"position")]).is_equal(codec)
	assert_that(quantizers[keys.find(&"rotation")]).is_null()


func test_windowed_writes_through_to_the_input_set() -> void:
	var configs: Dictionary = {
		&"move": _prop().input().windowed(3),
	}
	var set := NetwSyncSet.from_property_configs(configs, INPUT)
	assert_int(set.window).is_equal(3)


func test_member_policy_override_wins_over_the_kind_default() -> void:
	var configs: Dictionary = {
		&"position": _prop().state().controller(),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	assert_int(set.policy).is_equal(NetwScriptModel.Policy.CONTROLLER)


func test_state_audience_narrows_when_a_member_asks() -> void:
	var configs: Dictionary = {
		&"secret": _prop().state().audience(true),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	assert_int(set.audience).is_equal(NetwSyncSet.Audience.AUDIENCE_SERVER_ONLY)


func _make_set(fields: Array) -> NetwSyncSet:
	# Builds a set directly from [key, lane] pairs for schema-hash coverage.
	var set := NetwSyncSet.new()
	for pair: Array in fields:
		var field := NetwSyncSet.Field.new(pair[0])
		field.lane = pair[1]
		set.fields.append(field)
	return set


func test_schema_hash_is_deterministic_and_16_bit() -> void:
	var a := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE], [&"hp", NetwSyncSet.Lane.RETAINED]])
	var b := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE], [&"hp", NetwSyncSet.Lane.RETAINED]])
	assert_int(a.schema_hash()).is_equal(b.schema_hash())
	assert_int(a.schema_hash()).is_between(0, 0xFFFF)


func test_schema_hash_is_sensitive_to_order_membership_and_lane() -> void:
	var base := _make_set([[&"a", NetwSyncSet.Lane.VOLATILE], [&"b", NetwSyncSet.Lane.VOLATILE]])
	var reordered := _make_set([[&"b", NetwSyncSet.Lane.VOLATILE], [&"a", NetwSyncSet.Lane.VOLATILE]])
	var extra := _make_set([
		[&"a", NetwSyncSet.Lane.VOLATILE],
		[&"b", NetwSyncSet.Lane.VOLATILE],
		[&"c", NetwSyncSet.Lane.VOLATILE],
	])
	var relaned := _make_set([[&"a", NetwSyncSet.Lane.VOLATILE], [&"b", NetwSyncSet.Lane.RETAINED]])
	assert_int(base.schema_hash()).is_not_equal(reordered.schema_hash())
	assert_int(base.schema_hash()).is_not_equal(extra.schema_hash())
	assert_int(base.schema_hash()).is_not_equal(relaned.schema_hash())


func test_last_member_owns_a_conflicting_knob() -> void:
	# Two members name the window differently. The derivation warns and the last
	# one owns it, the reconciliation the per-set pump relies on.
	var configs: Dictionary = {
		&"move": _prop().input().windowed(2),
		&"aim": _prop().input().windowed(5),
	}
	var set := NetwSyncSet.from_property_configs(configs, INPUT)
	assert_int(set.window).is_equal(5)


func test_volatile_frame_round_trips_a_stamped_state_row() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(3, -4)
	src.rotation = 1.5
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK_ACK

	var bytes := NetwSyncPipeline.encode_volatile_frame(src, set, 2, 40, 37)
	assert_int(bytes.size()).is_greater(0)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncPipeline.apply_volatile_frame(dst, set, bytes)
	assert_int(header["ordinal"]).is_equal(2)
	assert_int(header["tick"]).is_equal(40)
	assert_int(header["ack"]).is_equal(37)
	assert_that(dst.position).is_equal(Vector2(3, -4))
	assert_float(dst.rotation).is_equal_approx(1.5, 0.0001)


func test_volatile_frame_skips_the_retained_lane() -> void:
	# A retained field rides SYNC_DELTA, never the volatile frame, so a node that
	# lacks it still round-trips the volatile row cleanly.
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(7, 8)
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"inventory", NetwSyncSet.Lane.RETAINED],
	])
	var bytes := NetwSyncPipeline.encode_volatile_frame(src, set, 0, -1, -1)
	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	NetwSyncPipeline.apply_volatile_frame(dst, set, bytes)
	assert_that(dst.position).is_equal(Vector2(7, 8))


func test_volatile_frame_bails_on_a_missing_field() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var set := _make_set([[&"nonexistent", NetwSyncSet.Lane.VOLATILE]])
	assert_int(NetwSyncPipeline.encode_volatile_frame(src, set, 0, -1, -1).size()).is_equal(0)


func test_binding_encodes_a_windowed_input_ring() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK
	set.window = 3
	var binding := NetwSyncSetBinding.new(set, node)

	node.position = Vector2(1, 0)
	binding.encode_volatile(0, 10, -1)
	node.position = Vector2(2, 0)
	binding.encode_volatile(0, 11, -1)
	node.position = Vector2(3, 0)
	var bytes := binding.encode_volatile(0, 12, -1)

	var frame := NetwFrameEnvelope.decode_sync_frame(bytes, [null], [TYPE_VECTOR2])
	var samples: Array = frame["samples"]
	assert_int(frame["tick"]).is_equal(12)
	assert_int(samples.size()).is_equal(3)
	# Newest first: age 0 is the latest tick, the oldest is age 2.
	assert_int(samples[0][0]).is_equal(0)
	assert_that(samples[0][1][0]).is_equal(Vector2(3, 0))
	assert_int(samples[2][0]).is_equal(2)
	assert_that(samples[2][1][0]).is_equal(Vector2(1, 0))


func test_register_derived_binds_declared_state_and_input_sets() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	var node: Node = preload("res://tests/support/sync/derived_state_player.gd").new()
	add_child(node)
	auto_free(node)

	pipeline.register_derived(node)
	# The script marks one state set (position) and one input set (rotation).
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(2)

	# Idempotent: a second registration adds nothing.
	pipeline.register_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(2)

	pipeline.unregister_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(0)


func test_register_derived_ignores_a_scriptless_node() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	pipeline.register_derived(node)
	assert_int(pipeline.counters()[&"derived_sets_active"]).is_equal(0)


func test_binding_retained_delta_heals_then_masks_changes() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 0.5
	var set := _make_set([[&"rotation", NetwSyncSet.Lane.RETAINED]])
	var binding := NetwSyncSetBinding.new(set, node)

	# A peer with no baseline heals with the full retained row.
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_greater(0)

	# No change since the peer's baseline sends nothing.
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_equal(0)

	# A changed retained field sends a delta again.
	node.rotation = 1.5
	binding.poll_retained()
	assert_int(binding.retained_delta(0, 2).size()).is_greater(0)


func test_binding_retained_delta_quantizes_configured_fields() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 1.0
	var codec := NetwQuantizeFixed.new()
	var set := NetwSyncSet.new()
	var field := NetwSyncSet.Field.new(&"rotation", codec)
	field.lane = NetwSyncSet.Lane.RETAINED
	set.fields.append(field)
	var binding := NetwSyncSetBinding.new(set, node)

	binding.poll_retained()
	var bytes := binding.retained_delta(0, 2)

	# Decode [ordinal | mask | value] with the same codec the encoder selected.
	var r := NetwBitBuffer.Reader.new(bytes)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(0)
	assert_int(NetwCodec.get_safe_varint(r)).is_equal(1)
	var decoded := NetwScriptModel.read_values(r, [codec], [TYPE_FLOAT])
	assert_float(decoded[0]).is_equal_approx(1.0, 0.05)


func test_state_frame_carries_the_reconciliation_ack() -> void:
	# Wire lock: a state set frames STAMPED plus ACKED so the reconciliation ack
	# rides, and the no-input sentinel survives as -1 rather than a stray 0.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(2, 3)
	var set := NetwSyncSet.from_property_configs({ &"position": _prop().state() }, STATE)
	var binding := NetwSyncSetBinding.new(set, node)

	var frame := NetwFrameEnvelope.decode_sync_frame(
		binding.encode_volatile(0, 12, 9), [null], [TYPE_VECTOR2],
	)
	assert_int(frame["flags"]).is_equal(
		NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_ACKED,
	)
	assert_int(frame["tick"]).is_equal(12)
	assert_int(frame["ack"]).is_equal(9)

	var none := NetwFrameEnvelope.decode_sync_frame(
		binding.encode_volatile(0, 13, -1), [null], [TYPE_VECTOR2],
	)
	assert_int(none["ack"]).is_equal(-1)


func test_input_frame_carries_no_ack_slot() -> void:
	# Wire lock: an input set frames STAMPED | WINDOWED (the default input window
	# rides redundant samples), never ACKED, so an input row never reserves the
	# reconciliation-ack bytes a state row does.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.rotation = 0.25
	var set := NetwSyncSet.from_property_configs({ &"rotation": _prop().input() }, INPUT)
	var frame := NetwFrameEnvelope.decode_sync_frame(
		NetwSyncSetBinding.new(set, node).encode_volatile(0, 7, 4), [null], [TYPE_FLOAT],
	)
	assert_int(frame["flags"]).is_equal(
		NetwFrameEnvelope.SYNC_FLAG_STAMPED | NetwFrameEnvelope.SYNC_FLAG_WINDOWED,
	)
	assert_int(frame["ack"]).is_equal(-1)


func test_binding_applies_a_volatile_state_row() -> void:
	var set := NetwSyncSet.from_property_configs({ &"position": _prop().state() }, STATE)
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(5, -6)
	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)

	var bytes := NetwSyncSetBinding.new(set, src).encode_volatile(3, 20, 17)
	var header := NetwSyncSetBinding.new(set, dst).apply_volatile(bytes)
	assert_int(header["ordinal"]).is_equal(3)
	assert_int(header["ack"]).is_equal(17)
	assert_that(dst.position).is_equal(Vector2(5, -6))


func test_binding_applies_the_freshest_windowed_sample() -> void:
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK
	set.window = 3
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var src_binding := NetwSyncSetBinding.new(set, src)
	src.position = Vector2(1, 0)
	src_binding.encode_volatile(0, 10, -1)
	src.position = Vector2(2, 0)
	var bytes := src_binding.encode_volatile(0, 11, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncSetBinding.new(set, dst).apply_volatile(bytes)
	# The freshest sample (age 0, tick 11) is the row displayed.
	assert_that(dst.position).is_equal(Vector2(2, 0))
	assert_int(header["tick"]).is_equal(11)


func test_binding_applies_a_retained_delta() -> void:
	var set := _make_set([[&"rotation", NetwSyncSet.Lane.RETAINED]])
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.rotation = 1.25
	var src_binding := NetwSyncSetBinding.new(set, src)
	src_binding.poll_retained()
	var bytes := src_binding.retained_delta(0, 2)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	assert_bool(NetwSyncSetBinding.new(set, dst).apply_retained_delta(bytes)).is_true()
	assert_float(dst.rotation).is_equal_approx(1.25, 0.0001)


func test_gather_payload_reads_every_set_field() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(2, 5)
	node.rotation = 0.75
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	var payload := NetwSyncPipeline.gather_payload(node, set)
	assert_int(payload.size()).is_equal(2)
	assert_that(payload[&"position"]).is_equal(Vector2(2, 5))
	assert_float(payload[&"rotation"]).is_equal_approx(0.75, 0.0001)


func test_gather_payload_skips_a_field_missing_on_the_node() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 1)
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"nonexistent", NetwSyncSet.Lane.VOLATILE],
	])
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
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	NetwSyncPipeline.apply_payload(node, set, {
		&"position": Vector2(9, 9),
		&"rotation": 3.0,  # not in the set, ignored
	})
	assert_that(node.position).is_equal(Vector2(9, 9))
	assert_float(node.rotation).is_equal_approx(0.0, 0.0001)


func test_binding_payload_snapshots_and_restores_between_nodes() -> void:
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(-6, 2)
	src.rotation = 2.0
	var snapshot := NetwSyncSetBinding.new(set, src).snapshot_payload()

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	NetwSyncSetBinding.new(set, dst).apply_payload(snapshot)
	assert_that(dst.position).is_equal(Vector2(-6, 2))
	assert_float(dst.rotation).is_equal_approx(2.0, 0.0001)


func test_binding_apply_hook_fires_with_the_decoded_payload() -> void:
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK_ACK
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(4, 9)
	var bytes := NetwSyncSetBinding.new(set, src).encode_volatile(0, 12, 7)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var dst_binding := NetwSyncSetBinding.new(set, dst)
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
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK_ACK
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(5, 5)
	var bytes := NetwSyncSetBinding.new(set, src).encode_volatile(0, 3, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	dst.position = Vector2(-1, -1)
	var dst_binding := NetwSyncSetBinding.new(set, dst)
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
	var set := _make_set([[&"rotation", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK
	set.window = 3
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	var src_binding := NetwSyncSetBinding.new(set, src)
	src.rotation = 0.1
	src_binding.encode_volatile(0, 10, -1)
	src.rotation = 0.2
	src_binding.encode_volatile(0, 11, -1)
	src.rotation = 0.3
	var bytes := src_binding.encode_volatile(0, 12, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncSetBinding.new(set, dst).apply_volatile(bytes)
	var samples: Array = header["samples"]
	assert_int(samples.size()).is_equal(3)
	# Newest first: tick 12 at age 0, then 11, then 10.
	assert_int(samples[0]["tick"]).is_equal(12)
	assert_int(samples[2]["tick"]).is_equal(10)
	assert_float((samples[0]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(0.3, 0.0001)
	assert_float((samples[2]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(0.1, 0.0001)
	# The freshest sample snaps the display node.
	assert_float(dst.rotation).is_equal_approx(0.3, 0.0001)


func test_binding_windowed_input_sources_the_predicted_timeline() -> void:
	# A prediction engine drives the window from the predicted timeline, so the
	# client resends the same samples it recorded rather than its gathered ring.
	var set := _make_set([[&"rotation", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK
	set.window = 3
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var timeline := NetwTimeline.new()
	timeline.record_input(8, {&"rotation": 0.8})
	timeline.record_input(9, {&"rotation": 0.9})
	timeline.record_input(10, {&"rotation": 1.0})
	var binding := NetwSyncSetBinding.new(set, node)
	binding.window_timeline = timeline
	var bytes := binding.encode_volatile(0, 10, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncSetBinding.new(set, dst).apply_volatile(bytes)
	var samples: Array = header["samples"]
	assert_int(samples.size()).is_equal(3)
	assert_int(samples[0]["tick"]).is_equal(10)
	assert_float((samples[0]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(1.0, 0.0001)
	assert_float((samples[2]["payload"] as Dictionary)[&"rotation"]).is_equal_approx(0.8, 0.0001)


func test_masked_sugar_writes_through_and_conflicts_with_windowed() -> void:
	var configs: Dictionary = {
		&"position": _prop().state().masked(),
	}
	var set := NetwSyncSet.from_property_configs(configs, STATE)
	assert_bool(set.masked).is_true()

	# masked() and windowed() are mutually exclusive; the derivation warns
	# and drops masked rather than crash or silently combine both.
	var conflicting: Dictionary = {
		&"move": _prop().input().masked().windowed(3),
	}
	var conflicting_set := NetwSyncSet.from_property_configs(conflicting, INPUT)
	assert_bool(conflicting_set.masked).is_false()
	assert_int(conflicting_set.window).is_equal(3)


func test_masked_delta_heals_an_unconfirmed_peer_with_the_full_row() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)

	# Peer 2 has never confirmed a baseline, so every field masks in.
	var first := binding.masked_delta(0, 2, 10, -1)
	assert_int((first["bytes"] as PackedByteArray).size()).is_greater(0)

	var frame := NetwFrameEnvelope.decode_sync_frame(first["bytes"], [null], [TYPE_VECTOR2])
	assert_int(frame["flags"] & NetwFrameEnvelope.SYNC_FLAG_MASKED).is_greater(0)
	assert_int(frame["mask"]).is_equal(1)


func test_masked_delta_sends_nothing_once_a_peer_is_confirmed_and_unchanged() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)

	# Same value, peer now confirmed: nothing to send.
	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_equal(0)


func test_masked_delta_never_advances_a_baseline_until_the_send_is_acked() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	# No ack yet: the peer's confirmed baseline is still absent, so the very
	# same value masks in again rather than silently going quiet, the
	# never-diff-against-an-unconfirmed-row guarantee.
	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_greater(0)

	binding.advance_masked_ack(2, 500)
	var third := binding.masked_delta(0, 2, 12, -1)
	assert_int((third["bytes"] as PackedByteArray).size()).is_equal(0)


func test_masked_delta_masks_only_the_changed_field() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	node.rotation = 0.5
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)

	node.rotation = 1.5
	var second := binding.masked_delta(0, 2, 11, -1)
	var frame := NetwFrameEnvelope.decode_sync_frame(
		second["bytes"], [null, null], [TYPE_VECTOR2, TYPE_FLOAT],
	)
	# Bit 1 (rotation) alone, position untouched.
	assert_int(frame["mask"]).is_equal(2)


func test_masked_apply_merges_a_partial_frame_onto_the_last_decoded_row() -> void:
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(1, 2)
	src.rotation = 0.5
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	set.masked = true
	var src_binding := NetwSyncSetBinding.new(set, src)
	var full := src_binding.masked_delta(0, 9, 10, -1)  # gain edge, full row

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var dst_binding := NetwSyncSetBinding.new(set, dst)
	dst_binding.apply_volatile(full["bytes"])
	assert_that(dst.position).is_equal(Vector2(1, 2))
	assert_float(dst.rotation).is_equal_approx(0.5, 0.0001)

	src_binding.commit_masked_pending(9, 500, full["row"])
	src_binding.advance_masked_ack(9, 500)
	src.rotation = 1.5
	var partial := src_binding.masked_delta(0, 9, 11, -1)

	var header := dst_binding.apply_volatile(partial["bytes"])
	# Only rotation was masked in, but position must survive the merge.
	assert_that(dst.position).is_equal(Vector2(1, 2))
	assert_float(dst.rotation).is_equal_approx(1.5, 0.0001)
	assert_float((header["payload"] as Dictionary)[&"rotation"]).is_equal_approx(1.5, 0.0001)
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(1, 2))


func test_masked_apply_merges_against_last_row_when_write_gate_is_off() -> void:
	# A reconciling client (write_gate false) must merge a masked frame against
	# its own last-decoded authoritative row, not the live node, which may hold
	# a diverging prediction.
	var src := Node2D.new()
	add_child(src)
	auto_free(src)
	src.position = Vector2(1, 2)
	src.rotation = 0.5
	var set := _make_set([
		[&"position", NetwSyncSet.Lane.VOLATILE],
		[&"rotation", NetwSyncSet.Lane.VOLATILE],
	])
	set.masked = true
	var src_binding := NetwSyncSetBinding.new(set, src)
	var full := src_binding.masked_delta(0, 9, 10, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	dst.position = Vector2(99, 99)  # a diverging prediction
	dst.rotation = 9.0
	var dst_binding := NetwSyncSetBinding.new(set, dst)
	dst_binding.write_gate = false
	dst_binding.apply_volatile(full["bytes"])
	# write_gate false: the node never snaps.
	assert_that(dst.position).is_equal(Vector2(99, 99))

	src_binding.commit_masked_pending(9, 500, full["row"])
	src_binding.advance_masked_ack(9, 500)
	src.rotation = 1.5
	var partial := src_binding.masked_delta(0, 9, 11, -1)
	var header := dst_binding.apply_volatile(partial["bytes"])
	# The merged row reflects the authoritative position from the first frame,
	# not the diverging prediction the (unwritten) node still holds.
	assert_that((header["payload"] as Dictionary)[&"position"]).is_equal(Vector2(1, 2))
	assert_float((header["payload"] as Dictionary)[&"rotation"]).is_equal_approx(1.5, 0.0001)


func test_advance_masked_ack_no_ops_on_a_peer_with_no_in_flight_rows() -> void:
	# note_peer_ack fans out to every derived binding regardless of whether that
	# binding's set is masked or ever sent this peer anything, so an untyped
	# Dictionary.get(peer) with no default (returning Nil into a Dictionary-typed
	# local) must never be reached here.
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)
	binding.advance_masked_ack(2, 500)  # must not error


func test_commit_pending_masked_no_ops_when_nothing_was_staged() -> void:
	var pipeline := NetwSyncPipeline.new(null)
	pipeline.commit_pending_masked(2, 500)  # must not error


func test_retain_masked_baselines_drops_a_peer_no_longer_a_recipient() -> void:
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	node.position = Vector2(1, 2)
	var set := _make_set([[&"position", NetwSyncSet.Lane.VOLATILE]])
	set.masked = true
	var binding := NetwSyncSetBinding.new(set, node)

	var first := binding.masked_delta(0, 2, 10, -1)
	binding.commit_masked_pending(2, 500, first["row"])
	binding.advance_masked_ack(2, 500)
	# Peer 2 no longer admitted: its baseline must drop, so a later re-admission
	# heals with a full row instead of diffing against the stale one.
	binding.retain_masked_baselines([])

	var second := binding.masked_delta(0, 2, 11, -1)
	assert_int((second["bytes"] as PackedByteArray).size()).is_greater(0)
	var frame := NetwFrameEnvelope.decode_sync_frame(second["bytes"], [null], [TYPE_VECTOR2])
	assert_int(frame["mask"]).is_equal(1)


func test_binding_window_floor_trims_acknowledged_samples() -> void:
	var set := _make_set([[&"rotation", NetwSyncSet.Lane.VOLATILE]])
	set.stamp = NetwSyncSet.Stamp.STAMP_TICK
	set.window = 5
	var node := Node2D.new()
	add_child(node)
	auto_free(node)
	var timeline := NetwTimeline.new()
	for t: int in [7, 8, 9, 10]:
		timeline.record_input(t, {&"rotation": float(t)})
	var binding := NetwSyncSetBinding.new(set, node)
	binding.window_timeline = timeline
	binding.window_floor = 8  # the server already consumed through tick 8
	var bytes := binding.encode_volatile(0, 10, -1)

	var dst := Node2D.new()
	add_child(dst)
	auto_free(dst)
	var header := NetwSyncSetBinding.new(set, dst).apply_volatile(bytes)
	var samples: Array = header["samples"]
	# Only ticks 9 and 10 survive the floor.
	assert_int(samples.size()).is_equal(2)
	assert_int(samples[0]["tick"]).is_equal(10)
	assert_int(samples[1]["tick"]).is_equal(9)
