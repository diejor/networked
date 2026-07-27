## Laws for which comparison a transition is judged by, and for what a
## disagreement is charged to.
##
## A tolerance is what a peer spends when it cannot claim exactness. A
## transition whose antecedents were all declared equal has none to spend, so
## its fingerprints either match or the transition is broken, and widening an
## epsilon would only hide that. These laws pin the split between the two
## compares, and the attribution that makes a disagreement an address rather
## than a number. What earns a transition its label is
## [TestPredictDomainLabeling].
class_name TestPredictCompareSplit
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const ExactVerdict := Engine_.ExactVerdict
const Domain := NetwPredictJournal.Domain
const Attribution := NetwPredictJournal.Attribution
const WITNESS := NetwPredictJournal.EVIDENCE_WITNESS
const RAW := NetwPredictJournal.EVIDENCE_RAW

# Far apart enough that no tolerance below it would ever call them equal, so a
# law that expects agreement is proving the epsilon was not consulted.
const _WIDE_EPSILON := 1000.0


func _judge(
		domain: Domain,
		verdict: ExactVerdict,
		predicted: Dictionary,
		payload: Dictionary,
		epsilon: float,
) -> Dictionary:
	return Engine_.evaluate(
		domain,
		verdict,
		predicted,
		payload,
		epsilon,
		{ },
		{ },
		{ },
		{ },
	)


# --- the split ---


# The decisive law. The two states are a full four apart, which every tolerance
# in this file would forgive, and the fingerprints disagree. In domain that is a
# divergence, because the peers claimed they would reproduce the transition and
# did not, and no epsilon is entitled to overrule the claim they made.
func test_an_in_domain_mismatch_corrects_however_small_the_error() -> void:
	var verdict := _judge(
		Domain.IN_DOMAIN,
		ExactVerdict.UNEQUAL,
		{ &"x": 1.0 },
		{ &"x": 5.0 },
		_WIDE_EPSILON,
	)
	assert_bool(verdict[&"corrected"]).is_true()
	assert_bool(verdict[&"in_domain"]).is_true()


# The same claim read the other way. Fingerprints that agree are agreement,
# even where the reported magnitudes would have crossed the threshold, because
# the fingerprint covers the whole canonical closure and the epsilon covers only
# the properties this frame happened to carry.
func test_an_in_domain_match_never_corrects() -> void:
	var verdict := _judge(
		Domain.IN_DOMAIN,
		ExactVerdict.EQUAL,
		{ &"x": 1.0 },
		{ &"x": 5.0 },
		0.001,
	)
	assert_bool(verdict[&"corrected"]).is_false()


# Out of domain the fingerprint verdict is a report and never a trigger. The
# peers never claimed exactness here, so charging them for missing it would
# manufacture a correction out of a difference they declared.
func test_an_out_of_domain_mismatch_is_judged_by_tolerance() -> void:
	var verdict := _judge(
		Domain.OUT_OF_DOMAIN,
		ExactVerdict.UNEQUAL,
		{ &"x": 1.0 },
		{ &"x": 1.001 },
		0.1,
	)
	assert_bool(verdict[&"corrected"]).is_false()
	assert_bool(verdict[&"in_domain"]).is_false()


# Blind is not exact. A transition labeled in domain that nobody has compared
# fingerprints for yet cannot be corrected on a comparison that never ran, so it
# falls back to the tolerance compare until an acknowledgement arrives.
func test_an_unjudged_in_domain_transition_falls_back_to_tolerance() -> void:
	assert_bool(_judge(
		Domain.IN_DOMAIN,
		ExactVerdict.UNJUDGED,
		{ &"x": 1.0 },
		{ &"x": 5.0 },
		_WIDE_EPSILON,
	)[&"corrected"]).is_false()
	assert_bool(_judge(
		Domain.IN_DOMAIN,
		ExactVerdict.UNJUDGED,
		{ &"x": 1.0 },
		{ &"x": 5.0 },
		0.1,
	)[&"corrected"]).is_true()


# The magnitude survives the split as a report. It stopped deciding an in-domain
# transition, but a reader diagnosing one still needs to know how far apart the
# peers ended up.
func test_the_field_report_is_filled_on_both_branches() -> void:
	for domain: Domain in [Domain.IN_DOMAIN, Domain.OUT_OF_DOMAIN]:
		var sink: Dictionary = { }
		Engine_.evaluate(
			domain,
			ExactVerdict.UNEQUAL,
			{ &"x": 1.0 },
			{ &"x": 5.0 },
			0.1,
			{ },
			{ },
			{ },
			sink,
		)
		assert_float(sink[&"x"]).is_equal_approx(4.0, 0.0001)


# --- attribute ---


# Pre-state is the first causal boundary. A mismatch there means every later
# difference can be propagation, so command and environment cannot outrank it.
func test_an_unequal_pre_state_is_charged_first() -> void:
	assert_int(Engine_.attribute(
		false, false, false, false, false, false, WITNESS, WITNESS,
	)) \
			.is_equal(Attribution.PRE_STATE)


# Equal pre-state leaves the command as the next boundary.
func test_an_unequal_command_is_charged_to_the_command() -> void:
	assert_int(Engine_.attribute(
		true, false, true, true, true, true, WITNESS, WITNESS,
	)).is_equal(Attribution.COMMAND)


# An equal command leaves the environment as the next suspect, and a digest
# disagreement convicts it: the two peers ran the same input against world facts
# they had themselves declared and fingerprinted, and those did not match.
func test_an_unequal_environment_is_charged_to_the_environment() -> void:
	assert_int(Engine_.attribute(
		true, true, false, true, true, true, WITNESS, WITNESS,
	)) \
			.is_equal(Attribution.ENVIRONMENT)


# Execution topology is the first B2 boundary.
func test_unequal_topology_is_charged_before_solve_evidence() -> void:
	assert_int(Engine_.attribute(
		true, true, true, false, false, false, WITNESS | RAW, WITNESS | RAW,
	)).is_equal(Attribution.TOPOLOGY)


func test_raw_bits_are_tested_only_when_both_peers_enable_them() -> void:
	assert_int(Engine_.attribute(
		true, true, true, true, false, true, WITNESS | RAW, WITNESS | RAW,
	)).is_equal(Attribution.EXECUTION)
	assert_int(Engine_.attribute(
		true, true, true, true, false, true, WITNESS | RAW, WITNESS,
	)).is_equal(Attribution.CLOSURE)


func test_unequal_realized_witness_is_charged_to_contact() -> void:
	assert_int(Engine_.attribute(
		true, true, true, true, true, false, WITNESS, WITNESS,
	)).is_equal(Attribution.CONTACT)


func test_equal_observed_boundaries_leave_the_closure() -> void:
	assert_int(Engine_.attribute(
		true, true, true, true, true, true, WITNESS, WITNESS,
	)) \
			.is_equal(Attribution.CLOSURE)


func test_missing_required_evidence_is_unknown() -> void:
	assert_int(Engine_.attribute(
		true, true, true, true, true, true, 0, 0,
	)) \
			.is_equal(Attribution.UNKNOWN)
	assert_int(Engine_.attribute(
		true, true, true, true, true, true, WITNESS, WITNESS, false,
	)) \
			.is_equal(Attribution.UNKNOWN)


func test_raw_and_fact_fingerprints_are_order_independent() -> void:
	assert_int(Engine_.fact_fingerprint({ &"a": 1, &"b": 2 })).is_equal(
		Engine_.fact_fingerprint({ &"b": 2, &"a": 1 }),
	)
	assert_int(Engine_.raw_state_fingerprint({ &"x": 1.0 })) \
			.is_not_equal(Engine_.raw_state_fingerprint({ &"x": 1.0000001 }))
	assert_int(Engine_.contact_count_bucket(99)).is_equal(4)

	# Insertion order above is the weaker half. These fold in one fixed order
	# and are compared across two processes, so the order has to come from the
	# names' text: sorting the StringName keys themselves compares interning
	# pointers, which is a per-process accident that no same-process assertion
	# can see. Enough keys that a pointer order coinciding with text order is
	# not a plausible pass.
	var facts := {
		&"zulu": 1, &"alpha": 2, &"mike": 3, &"echo": 4,
		&"papa": 5, &"bravo": 6, &"tango": 7, &"kilo": 8,
	}
	assert_int(Engine_.fact_fingerprint(facts)).override_failure_message(
		"fact fingerprints must fold their keys in text order",
	).is_equal(_fold_in_text_order(facts))
	var raw := { &"zed": 1.0, &"abe": 2.0, &"mid": 3.0, &"eve": 4.0 }
	assert_int(Engine_.raw_state_fingerprint(raw)).override_failure_message(
		"raw state fingerprints must fold their keys in text order",
	).is_equal(_fold_in_text_order(raw))


# The fold the two fingerprints above owe a peer, written out longhand so the
# law does not lean on the implementation it is checking.
func _fold_in_text_order(source: Dictionary) -> int:
	var names := PackedStringArray()
	for key in source:
		names.append(String(key))
	names.sort()
	var bytes := PackedByteArray()
	for name in names:
		var key := StringName(name)
		bytes.append_array(var_to_bytes(key))
		bytes.append_array(var_to_bytes(source[key]))
	return NetwPredictJournal.fnv1a(bytes)


func test_state_family_search_returns_the_first_difference() -> void:
	assert_int(
		Engine_.differing_family(
			PackedInt32Array([1, 2, 3]),
			PackedInt32Array([1, 9, 8]),
		),
	).is_equal(NetwPredictJournal.StateFamily.MOMENTUM)
	assert_int(
		Engine_.differing_family(
			PackedInt32Array([1, 2]),
			PackedInt32Array([1, 9, 8]),
		),
	).is_equal(NetwPredictJournal.StateFamily.NONE)
