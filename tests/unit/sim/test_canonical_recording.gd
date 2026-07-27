## Laws of the canonical domain: what
## [method NetwSyncSetBinding.canonicalize_payload] records, what
## [method NetwSyncSetBinding.canonical_bytes] hashes, and which fields a state
## fingerprint is taken over.
##
## A prediction and the authority state that acknowledges it are only comparable
## exactly when both were recorded in the form the wire can carry. These cases
## pin that the recorded value is the quantizer round trip, that the bytes are
## stable and order-faithful, and that a one-quantum difference is visible to
## [method NetwPredictJournal.fnv1a] rather than absorbed.
##
## The last group pins the scope. A comparison is entitled to judge the fields
## the recurrence reads and no others, so a
## [constant NetwSyncSet.PropertyClass.DERIVED] or
## [constant NetwSyncSet.PropertyClass.COSMETIC] field must not be able to
## decide whether two peers reproduced a transition.
class_name TestCanonicalRecording
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine

class CanonicalProbe:
	extends Node2D

	var pose: Vector2 = Vector2.ZERO
	var heading: float = 0.0
	var tag: int = 0


const STEP := 0.5


func _quantized_set() -> NetwSyncSet:
	var set := NetwSyncSet.new()
	var pose := NetwSyncSet.Field.new(&"pose")
	pose.quantizer = NetwQuantizeFixed.new().step(STEP).limits(-64.0, 64.0)
	var heading := NetwSyncSet.Field.new(&"heading")
	heading.quantizer = NetwQuantizeAngle.new().bits(8)
	set.fields.append(pose)
	set.fields.append(heading)
	set.fields.append(NetwSyncSet.Field.new(&"tag"))
	return set


func _probe() -> CanonicalProbe:
	var node := CanonicalProbe.new()
	add_child(node)
	auto_free(node)
	return node


func test_recorded_value_is_the_quantizer_round_trip() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(1.1, -2.3)
	node.heading = 1.0
	node.tag = 9

	var canonical := binding.canonicalize_payload(binding.snapshot_payload())

	var pose: Vector2 = canonical[&"pose"]
	assert_float(fmod(pose.x, STEP)).is_equal_approx(0.0, 1e-5)
	assert_float(fmod(pose.y, STEP)).is_equal_approx(0.0, 1e-5)
	assert_that(pose).is_not_equal(node.pose)
	assert_float(pose.distance_to(node.pose)).is_less(STEP)
	assert_float(canonical[&"heading"]).is_not_equal(node.heading)


func test_a_field_without_a_quantizer_passes_through_untouched() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.tag = -12345

	var canonical := binding.canonicalize_payload(binding.snapshot_payload())

	assert_int(canonical[&"tag"]).override_failure_message(
		"raw bits are already the canonical form of an unquantized field",
	).is_equal(-12345)


func test_canonicalization_is_idempotent() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(3.37, 8.91)
	node.heading = 2.7

	var once := binding.canonicalize_payload(binding.snapshot_payload())
	var twice := binding.canonicalize_payload(once)

	assert_dict(twice).override_failure_message(
		"a canonical payload must be its own canonical form",
	).is_equal(once)


func test_canonical_bytes_are_stable_across_two_encodes() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(4.2, -1.9)
	node.heading = 0.75
	node.tag = 3
	var payload := binding.snapshot_payload()

	var first := binding.canonical_bytes(payload)
	var second := binding.canonical_bytes(payload)

	assert_array(first).is_not_empty()
	assert_array(first).is_equal(second)
	assert_int(NetwPredictJournal.fnv1a(first)) \
			.is_equal(NetwPredictJournal.fnv1a(second))


func test_identical_payloads_fingerprint_equal_across_two_nodes() -> void:
	var set := _quantized_set()
	var source := _probe()
	var target := _probe()
	source.pose = Vector2(6.6, 2.2)
	source.heading = 1.9
	source.tag = 44

	var source_binding := NetwSyncSetBinding.new(set, source)
	var payload := source_binding.canonicalize_payload(
		source_binding.snapshot_payload(),
	)
	var target_binding := NetwSyncSetBinding.new(set, target)
	target_binding.apply_payload(payload)
	var restored := target_binding.canonicalize_payload(
		target_binding.snapshot_payload(),
	)

	assert_dict(restored).is_equal(payload)
	var restored_fp := NetwPredictJournal.fnv1a(
		target_binding.canonical_bytes(restored),
	)
	assert_int(restored_fp).override_failure_message(
		"a restore of a canonical payload must fingerprint equal to it",
	).is_equal(
		NetwPredictJournal.fnv1a(source_binding.canonical_bytes(payload)),
	)


func test_one_quantum_of_difference_changes_the_fingerprint() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(2.0, 2.0)
	var base := binding.canonicalize_payload(binding.snapshot_payload())
	node.pose = Vector2(2.0 + STEP, 2.0)
	var shifted := binding.canonicalize_payload(binding.snapshot_payload())

	assert_that(shifted[&"pose"]).is_not_equal(base[&"pose"])
	assert_int(NetwPredictJournal.fnv1a(binding.canonical_bytes(shifted))) \
			.override_failure_message(
				"a whole quantum of movement must not hash equal",
			).is_not_equal(
				NetwPredictJournal.fnv1a(binding.canonical_bytes(base)),
			)


func test_a_sub_quantum_difference_collapses_to_one_fingerprint() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(2.0, 2.0)
	var base := binding.canonical_bytes(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)
	node.pose = Vector2(2.0 + STEP * 0.1, 2.0)
	var nudged := binding.canonical_bytes(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_int(NetwPredictJournal.fnv1a(nudged)).override_failure_message(
		"two values the wire cannot distinguish must fingerprint equal",
	).is_equal(NetwPredictJournal.fnv1a(base))


func test_field_order_is_the_set_order_not_the_payload_order() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_quantized_set(), node)
	node.pose = Vector2(1.0, 1.0)
	node.heading = 0.5
	node.tag = 7
	var payload := binding.canonicalize_payload(binding.snapshot_payload())

	var reordered: Dictionary = { }
	reordered[&"tag"] = payload[&"tag"]
	reordered[&"heading"] = payload[&"heading"]
	reordered[&"pose"] = payload[&"pose"]

	assert_array(binding.canonical_bytes(reordered)).override_failure_message(
		"the same values must hash the same however the payload was built",
	).is_equal(binding.canonical_bytes(payload))


# --- the scope of a state fingerprint ---


# A set whose pose is causal, whose heading is derived, and whose tag is
# cosmetic. The two non-causal fields carry no quantizer, which is exactly the
# shape that makes their raw bits reach a fingerprint.
func _mixed_class_set() -> NetwSyncSet:
	var set := NetwSyncSet.new()
	var pose := NetwSyncSet.Field.new(&"pose")
	pose.quantizer = NetwQuantizeFixed.new().step(STEP).limits(-64.0, 64.0)
	pose.property_class = NetwSyncSet.PropertyClass.CAUSAL
	var heading := NetwSyncSet.Field.new(&"heading")
	heading.property_class = NetwSyncSet.PropertyClass.DERIVED
	var tag := NetwSyncSet.Field.new(&"tag")
	tag.property_class = NetwSyncSet.PropertyClass.COSMETIC
	set.fields.append(pose)
	set.fields.append(heading)
	set.fields.append(tag)
	return set


# An engine wired to nothing but a binding and the two field maps a fingerprint
# reads. Nothing else in the kernel participates in the scope decision, so a
# whole loopback would only make the law harder to read.
func _fingerprinting_engine(binding: NetwSyncSetBinding) -> Engine_:
	var engine := Engine_.new()
	engine._state_binding = binding
	engine._causal_fields = { &"pose": true }
	engine._state_family_of = {
		&"pose": Engine_.STATE_FAMILY_POSE,
		&"heading": Engine_.STATE_FAMILY_CONTROLLER,
		&"tag": Engine_.STATE_FAMILY_CONTROLLER,
	}
	return engine


# The decisive law. A derived float is recomputed by the body from causal ones,
# so two peers reach it from raw values that differ below the causal grid and it
# can never agree by luck. If it reaches the fingerprint, bit-equality is
# unreachable however well the causal fields are sized.
func test_a_derived_field_cannot_decide_the_state_fingerprint() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_mixed_class_set(), node)
	var engine := _fingerprinting_engine(binding)
	node.pose = Vector2(2.0, 2.0)
	node.heading = 1.0
	var base := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	node.heading = -2.75
	var moved := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_int(moved).override_failure_message(
		"a field the recurrence does not read must not decide whether two "
		+ "peers reproduced the transition",
	).is_equal(base)


# The same law for the class that reaches display only. Comparing one would
# correct a simulation over a value no simulation reads.
func test_a_cosmetic_field_cannot_decide_the_state_fingerprint() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_mixed_class_set(), node)
	var engine := _fingerprinting_engine(binding)
	node.pose = Vector2(1.0, -1.0)
	node.tag = 3
	var base := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	node.tag = -99999
	var moved := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_int(moved).is_equal(base)


# The scope narrows what is compared, never what is detected. A causal field is
# the thing a fingerprint exists to judge.
func test_a_causal_field_still_decides_the_state_fingerprint() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_mixed_class_set(), node)
	var engine := _fingerprinting_engine(binding)
	node.pose = Vector2(2.0, 2.0)
	var base := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	node.pose = Vector2(2.0 + STEP, 2.0)
	var moved := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_int(moved).override_failure_message(
		"a whole quantum of causal movement must still be visible",
	).is_not_equal(base)


# The family columns answer WHICH part of the state forked, and they are read
# after the whole-state verdict says one did. A family that could be moved by a
# field the recurrence does not read would name the wrong part.
func test_family_fingerprints_ignore_the_non_causal_fields() -> void:
	var node := _probe()
	var binding := NetwSyncSetBinding.new(_mixed_class_set(), node)
	var engine := _fingerprinting_engine(binding)
	node.pose = Vector2(4.0, 4.0)
	node.heading = 0.25
	node.tag = 1
	var base := engine._state_family_fingerprints(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	node.heading = 3.0
	node.tag = 77
	var moved := engine._state_family_fingerprints(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_array(moved).override_failure_message(
		"a differing family must name a part of the state the next "
		+ "transition reads",
	).is_equal(base)


# A set that declares no causal field at all has nothing the recurrence reads,
# and an empty scope would make every transition agree vacuously. That is the
# instrument-reads-healthy failure, so the scope falls back to the whole payload
# and keeps the answer it always gave.
func test_a_set_with_no_causal_field_compares_the_whole_payload() -> void:
	var node := _probe()
	var set := _mixed_class_set()
	set.fields[0].property_class = NetwSyncSet.PropertyClass.DERIVED
	var binding := NetwSyncSetBinding.new(set, node)
	var engine := _fingerprinting_engine(binding)
	engine._causal_fields = { }
	node.pose = Vector2(2.0, 2.0)
	var base := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	node.pose = Vector2(2.0 + STEP, 2.0)
	var moved := engine._state_fingerprint(
		binding.canonicalize_payload(binding.snapshot_payload()),
	)

	assert_int(moved).override_failure_message(
		"an empty scope must not silently agree about everything",
	).is_not_equal(base)
