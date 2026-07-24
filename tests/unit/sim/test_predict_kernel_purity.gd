## Source lint that keeps the fenced prediction kernels decidable from their
## antecedents alone.
##
## A transition is reproducible only if the decision that produced it was a
## function of what the journal records: the command, the previous state, and the
## timing the pass carried. A kernel that reached for the clock, the entity, a
## binding, or a signal would make the recorded row an incomplete account of its
## own transition, and every determinism law above it would still pass while
## meaning nothing. This reads the engine source and fails on that regression.
##
## The kernels are static, so the language already denies them engine state; this
## guards the tokens the language cannot, and guards their staticness itself,
## since the cheapest way to break the fence is to drop one word.
class_name TestPredictKernelPurity
extends NetwTestSuite

const _ENGINE := "res://addons/networked/replication/netw_lag_compensation_interface.gd"

# Tokens no kernel may name: clock resolution, the entity and its nodes, the
# bindings that read and write the body, the recording surfaces, engine
# lifecycle, and signal wiring. A kernel receives its timing and its antecedents
# by argument and returns a plan the shell applies.
const _FORBIDDEN: Array[String] = [
	"_clock", "_frame_tick", "_tick_delta", "_adopt_timing",
	"Engine.", "Time.", "OS.",
	"get_node", "get_tree(", "is_instance_valid", "PhysicsServer",
	"_entity", "_handle", "_iface(",
	"_binding", "apply_payload", "snapshot_payload",
	"_journal", "_timeline", "_entry_history", "_authored_tape", "_owner_claims",
	"_capture(", "_restore(", "_run(",
	".emit(", ".connect(", ".disconnect(",
]

# The fenced region every kernel lives in, linted as one block so a helper added
# beside them inherits the same rule rather than slipping in unlinted.
const _KERNEL_REGION := "#region Kernels"

# Each kernel, addressed by the exact signature that makes it stateless. The
# `static` word is the fence the language enforces, so the lint pins the word.
const _KERNELS: Array[String] = [
	"\tstatic func predict_fold(",
	"\tstatic func consume_plan(",
	"\tstatic func evaluate(",
	"\tstatic func measure(",
	"\tstatic func transport(",
	"\tstatic func recover(",
	"\tstatic func project_payload(",
	"\tstatic func domain_of(",
	"\tstatic func attribute(",
	"\tstatic func differing_family(",
	"\tstatic func raw_state_fingerprint(",
	"\tstatic func fact_fingerprint(",
	"\tstatic func contact_count_bucket(",
	"\tstatic func converge_toward(",
	"\tstatic func window_after(",
	"\tstatic func environment_digest(",
]


func _source_lines() -> PackedStringArray:
	var f := FileAccess.open(_ENGINE, FileAccess.READ)
	assert_object(f).is_not_null()
	var text := f.get_as_text()
	f.close()
	return text.split("\n")


# Strips the line comment so a word like "clock" in a doc line never trips the
# lint. Kernel code carries no '#' inside a string, so a cut at the first '#' is
# safe.
func _strip_comment(line: String) -> String:
	var hash := line.find("#")
	return line if hash < 0 else line.substr(0, hash)


# The body of a region, from its opener to the matching closer. The kernel fence
# nests inside the engine fence, so the first closer encountered is its own.
func _region_body(lines: PackedStringArray, opener: String) -> String:
	var out := ""
	var inside := false
	for line in lines:
		if not inside:
			if line.begins_with(opener):
				inside = true
			continue
		if line.begins_with("#endregion"):
			break
		out += _strip_comment(line) + "\n"
	return out


# The whole body of a column-zero class, region markers included, so a law about
# the class as a whole is not cut short by a fence inside it.
func _class_body(lines: PackedStringArray, header: String) -> String:
	var out := ""
	var inside := false
	for line in lines:
		if not inside:
			if line.begins_with(header):
				inside = true
			continue
		if line.begins_with("class "):
			break
		out += _strip_comment(line) + "\n"
	return out


func _assert_pure(name: String, body: String) -> void:
	assert_str(body).override_failure_message(
		"purity lint could not locate '%s'" % name,
	).is_not_empty()
	for token in _FORBIDDEN:
		assert_bool(body.contains(token)) \
			.override_failure_message(
				"purity: kernel '%s' references forbidden '%s'" % [name, token],
			).is_false()


func test_kernel_region_names_no_engine_state() -> void:
	var lines := _source_lines()
	_assert_pure(_KERNEL_REGION, _region_body(lines, _KERNEL_REGION))


# Staticness is the fence the language itself enforces: a static function has no
# self, so it cannot reach a field even if a later edit tries. Dropping the word
# would silently reopen every door the token list closes.
func test_every_kernel_is_static() -> void:
	var lines := _source_lines()
	var source := "\n".join(lines)
	for signature in _KERNELS:
		assert_bool(source.contains(signature)) \
			.override_failure_message(
				"kernel '%s' is missing or no longer static" % signature.strip_edges(),
			).is_true()


# The engine reads no clock at all. Timing arrives as a PredictTiming captured
# once at the pump boundary, so two kernels in one pass cannot disagree about
# which tick they were running.
func test_engine_resolves_no_clock() -> void:
	var lines := _source_lines()
	var body := _class_body(lines, "class _PredictionEngine")
	assert_str(body).is_not_empty()
	for token in ["_clock", "_frame_tick("]:
		assert_bool(body.contains(token)) \
			.override_failure_message(
				"the engine resolves '%s' instead of receiving PredictTiming" % token,
			).is_false()


# Guards the guard: a passing lint must mean the kernels are clean, not that the
# extraction silently returned nothing or that the fence was renamed away.
func test_lint_is_not_vacuous() -> void:
	var planted := "\t\tvar t := _handle.ack_age_ticks + _clock.tick\n"
	var caught := 0
	for token in _FORBIDDEN:
		if planted.contains(token):
			caught += 1
	assert_int(caught) \
		.override_failure_message("lint failed to catch a planted engine-state read") \
		.is_greater(0)

	var lines := _source_lines()
	var region := _region_body(lines, _KERNEL_REGION)
	# Sentinels: each kernel's own decision vocabulary, so a fence that captured
	# the wrong block or an empty one cannot read as clean.
	assert_str(region).contains("ConsumeAction")
	assert_str(region).contains("DriveKind.REPEAT")
	assert_str(region).contains("divergence_by_field")
	assert_str(region).contains("max_restore_ticks")
	assert_str(region).contains("OUT_OF_DOMAIN")
	assert_str(_class_body(lines, "class _PredictionEngine")).contains("PredictTiming")
