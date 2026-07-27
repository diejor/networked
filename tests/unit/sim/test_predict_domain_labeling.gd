## Laws for which transitions a peer is entitled to reproduce exactly.
##
## A prediction is only comparable by fingerprint when both peers ran against
## the same world. Declaring the island is how the engine learns which
## transitions those are, and the domain label is where that answer is recorded
## so a later compare can read it instead of re-deriving it. These laws pin the
## label. What the label then decides is
## [TestPredictCompareSplit].
class_name TestPredictDomainLabeling
extends NetwTestSuite

const Engine_ := NetwLagCompensationInterface._PredictionEngine
const Domain := NetwPredictJournal.Domain


# --- domain_of ---


# Every law below is about a DECLARED island, so the declaration argument is
# bound once here rather than repeated at every assertion. The laws that pin the
# undeclared baseline pass it explicitly.
func _declared(approximate: bool, label: int, window_until: int) -> Domain:
	return Engine_.domain_of(true, approximate, label, window_until)


func test_an_undisturbed_transition_claims_exactness() -> void:
	assert_int(_declared(false, 10, -1)).is_equal(Domain.IN_DOMAIN)


# Exactness is claimed, never assumed. An entity that never declared an island
# said nothing about whether its antecedents match its authority's, and silence
# is not a claim. No window and no quiet stretch earns exactness for it, because
# there is nothing to earn it against.
func test_an_undeclared_entity_is_never_in_domain() -> void:
	assert_int(Engine_.domain_of(false, false, 10, -1)) \
			.is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(Engine_.domain_of(false, false, 999, -1)) \
			.is_equal(Domain.OUT_OF_DOMAIN)


# A declaration of approximation is the game telling the engine the peers were
# never going to agree here, so no window and no timing can earn exactness back.
func test_an_approximate_island_is_never_in_domain() -> void:
	assert_int(_declared(true, 10, -1)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(true, 0, 999)).is_equal(Domain.OUT_OF_DOMAIN)


# The label is a property of WHEN the drive ran. A transition inside the window
# is out of domain and one past it is back in, so a window cannot retroactively
# condemn transitions that ran after the disturbance cleared.
func test_the_window_covers_exactly_the_transitions_inside_it() -> void:
	assert_int(_declared(false, 4, 6)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 5, 6)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 6, 6)).is_equal(Domain.IN_DOMAIN)
	assert_int(_declared(false, 7, 6)).is_equal(Domain.IN_DOMAIN)


# --- window_after ---


func test_a_contact_covers_its_own_transition_and_the_cooldown() -> void:
	# A contact at 10 with a 3 tick cooldown must leave 10, 11, 12 and 13 out of
	# domain: the contact frame itself is disturbed, not just the frames after.
	var until := Engine_.window_after(10, 3, -1)
	for label in [10, 11, 12, 13]:
		assert_int(_declared(false, label, until)) \
			.override_failure_message("label %d should be inside the window" % label) \
			.is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 14, until)).is_equal(Domain.IN_DOMAIN)


# Windows merge rather than restart. A second, nearer fact arriving while a
# window is open must never be able to cut the open one short, because the
# disturbance it reported does not undo the one already being covered.
func test_a_nearer_fact_cannot_shorten_an_open_window() -> void:
	var wide := Engine_.window_after(10, 20, -1)
	var narrowed := Engine_.window_after(11, 1, wide)
	assert_int(narrowed).is_equal(wide)


func test_a_later_fact_extends_an_open_window() -> void:
	var first := Engine_.window_after(10, 2, -1)
	var extended := Engine_.window_after(20, 2, first)
	assert_int(extended).is_greater(first)
	assert_int(_declared(false, 21, extended)).is_equal(Domain.OUT_OF_DOMAIN)


func test_a_negative_cooldown_still_covers_the_disturbed_transition() -> void:
	var until := Engine_.window_after(10, -5, -1)
	assert_int(_declared(false, 10, until)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 11, until)).is_equal(Domain.IN_DOMAIN)


# --- environment_digest ---


# Two peers that declared the same island must digest it identically. Dictionary
# iteration order is not guaranteed to agree between them, so the digest folds
# its sensor names in text order rather than trusting the order it received them
# in.
func test_the_digest_does_not_depend_on_declaration_order() -> void:
	var one := Engine_.environment_digest(7, { &"b": 2.0, &"a": 1.0 })
	var other := Engine_.environment_digest(7, { &"a": 1.0, &"b": 2.0 })
	assert_int(one).is_equal(other)

	# Declaration order is the weaker half: the two peers are separate
	# processes, and sorting the StringName keys themselves would order them by
	# interning pointer, which no same-process assertion can distinguish from
	# text order. Fold longhand in text order and require the digest to match,
	# with enough sensors that a coincidence is not a plausible pass.
	var samples := {
		&"zulu": 1.0, &"alpha": 2.0, &"mike": 3.0, &"echo": 4.0,
		&"papa": 5.0, &"bravo": 6.0, &"tango": 7.0, &"kilo": 8.0,
	}
	var names := PackedStringArray()
	for key in samples:
		names.append(String(key))
	names.sort()
	var bytes := PackedByteArray()
	bytes.append_array(var_to_bytes(7))
	for name in names:
		var key := StringName(name)
		bytes.append_array(var_to_bytes(key))
		bytes.append_array(var_to_bytes(samples[key]))
	assert_int(Engine_.environment_digest(7, samples)) \
			.override_failure_message(
				"the environment digest must fold its sensors in text order",
			).is_equal(NetwPredictJournal.fnv1a(bytes))


func test_a_changed_world_version_changes_the_digest() -> void:
	assert_int(Engine_.environment_digest(7, { })) \
		.is_not_equal(Engine_.environment_digest(8, { }))


func test_a_changed_sensor_reading_changes_the_digest() -> void:
	assert_int(Engine_.environment_digest(7, { &"ground": 1.0 })) \
		.is_not_equal(Engine_.environment_digest(7, { &"ground": 1.5 }))


# A sensor name and its value are folded separately, so moving a reading from
# one sensor to another is a different environment rather than the same bytes.
func test_the_digest_distinguishes_which_sensor_read_what() -> void:
	assert_int(Engine_.environment_digest(0, { &"a": 1.0, &"b": 2.0 })) \
		.is_not_equal(Engine_.environment_digest(0, { &"a": 2.0, &"b": 1.0 }))


# --- what an undeclared island means ---


# The engine cannot ask a question about bodies nobody named, so an undeclared
# island has to resolve to "unknown", and unknown is not exactness. This is the
# case that would otherwise pass silently: an entity colliding with the world
# while claiming every transition is reproducible to the bit.
func test_an_undeclared_contact_is_not_treated_as_equivalent() -> void:
	# The window a contact opens is what carries that answer into the label, so
	# the law is stated where it is observable: a covered transition is out of
	# domain for the cooldown and in domain again after it.
	var until := Engine_.window_after(10, 2, -1)
	assert_int(_declared(false, 10, until)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 12, until)).is_equal(Domain.OUT_OF_DOMAIN)
	assert_int(_declared(false, 13, until)).is_equal(Domain.IN_DOMAIN)


# --- the journal column ---


# A substituted transition ran a command its owner never authored, so one of its
# antecedents is unequal by definition and it must never be held to exactness,
# whatever the window said when it opened.
func test_substitution_downgrades_the_row_it_marks() -> void:
	var journal := NetwPredictJournal.new(8)
	journal.open(0, 0, 0, 1234)
	assert_int(journal.row_at(0)[&"domain"]).is_equal(Domain.IN_DOMAIN)
	journal.mark_substituted(0)
	assert_int(journal.row_at(0)[&"domain"]).is_equal(Domain.OUT_OF_DOMAIN)


func test_a_row_records_the_environment_it_ran_against() -> void:
	var journal := NetwPredictJournal.new(8)
	journal.open(3, 3, 0, 1234)
	journal.mark_e_digest(3, 99)
	journal.mark_domain(3, Domain.OUT_OF_DOMAIN)
	var row := journal.row_at(3)
	assert_int(row[&"e_digest"]).is_equal(99)
	assert_int(row[&"domain"]).is_equal(Domain.OUT_OF_DOMAIN)


# A row that has fallen out of the ring is not resurrected by a late fact about
# it, since writing one would corrupt whichever transition now owns the slot.
func test_a_fact_about_an_evicted_row_is_dropped() -> void:
	var journal := NetwPredictJournal.new(2)
	journal.open(0, 0, 0, 1)
	journal.open(1, 1, 0, 2)
	journal.open(2, 2, 0, 3)
	journal.mark_domain(0, Domain.OUT_OF_DOMAIN)
	journal.mark_e_digest(0, 77)
	assert_dict(journal.row_at(0)).is_empty()
	assert_int(journal.row_at(2)[&"domain"]).is_equal(Domain.IN_DOMAIN)
	assert_int(journal.row_at(2)[&"e_digest"]).is_equal(0)
