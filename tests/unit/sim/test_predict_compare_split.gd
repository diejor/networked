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


# The command is tested first because its evidence is the strongest: a hash
# disagreement means authority ran input the owner never authored, which
# explains the whole divergence without needing the environment or the step
# function to be at fault too.
func test_an_unequal_command_is_charged_to_the_command() -> void:
	assert_int(Engine_.attribute(false, true)).is_equal(Attribution.COMMAND)
	assert_int(Engine_.attribute(false, false)).is_equal(Attribution.COMMAND)


# An equal command leaves the environment as the next suspect, and a digest
# disagreement convicts it: the two peers ran the same input against world facts
# they had themselves declared and fingerprinted, and those did not match.
func test_an_unequal_environment_is_charged_to_the_environment() -> void:
	assert_int(Engine_.attribute(true, false)) \
			.is_equal(Attribution.ENVIRONMENT)


# The claim worth making. Equal commands and equal environments leave the step
# function itself, so this is a bug with an address rather than an unattributed
# divergence somebody would otherwise widen a tolerance around.
func test_equal_antecedents_leave_the_simulation() -> void:
	assert_int(Engine_.attribute(true, true)).is_equal(Attribution.SIMULATION)
