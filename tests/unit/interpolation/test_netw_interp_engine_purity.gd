## Source lint that keeps the sealed interpolation kernel pure.
##
## The display calculus can drive the real engine node-free only because the pump
## kernels read time by value and touch nothing outside the runtime handed to
## them. That purity is a standing invariant, not a one-time cleanup: a later edit
## that reaches for the clock, walks the tree, reads the config registry, or
## rescans roles per frame would silently re-break the calculus and the native
## fork-join. This test reads the engine source and fails on that regression. It
## is the operational guard behind P10.
##
## The one sanctioned impurity is CHASE reading the live predicted body, which is
## why its runtime is pinned to the SHELL thread class. The lint allows that read
## in the chase kernel alone.
class_name TestNetwInterpEnginePurity
extends NetwTestSuite

const _ENGINE := "res://addons/networked/sync/netw_interpolation_interface.gd"

# Tokens that must never appear in a pure kernel: clock resolution, tree or node
# access, engine globals, the config registry, per-frame role and authority
# scans, the runtime collection, and signal wiring. A kernel receives its timing
# by value and its runtime by argument, and it emits through the writer and the
# stats record.
const _FORBIDDEN: Array[String] = [
	"_get_clock", "_resolve_clock", "_capture_timing", "after_tick", "api.clock",
	"get_tree(", "get_node", "find_children", "get_parent(", "add_child",
	"NetwEntity.of",
	"Engine.", "Time.", "OS.",
	"NetwScriptModel", "get_node_property_interpolator", "replication_config",
	"synchronizers(", ".replication",
	"_resolve_role", "_resolve_display_role", "_authors_display_streams",
	"is_multiplayer_authority", "_pump_for(",
	"_runtimes",
	".emit(", ".connect(", ".disconnect(",
]

# The pure kernels, addressed by their column-zero signature. Each runs every
# frame and must stay free of every forbidden token above.
const _PURE_KERNELS: Array[String] = [
	"func _pump_history(",
	"func _dilate_playhead(",
	"func _calculate_min_lag(",
	"func _predicted_effective_smooth_time(",
]

# The inner display classes, linted as whole blocks.
const _PURE_CLASSES: Array[String] = [
	"class _History:",
	"class _Playhead:",
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


# The body of a column-zero func, from its signature to the next top-level func,
# class, or region marker, comment-stripped.
func _func_body(lines: PackedStringArray, signature: String) -> String:
	var out := ""
	var inside := false
	for line in lines:
		if not inside:
			if line.begins_with(signature):
				inside = true
			continue
		if line.begins_with("func ") or line.begins_with("class ") \
				or line.begins_with("#region") or line.begins_with("#endregion"):
			break
		out += _strip_comment(line) + "\n"
	return out


# The body of an inner class block, from its header to the next column-zero class
# or region marker, comment-stripped.
func _class_body(lines: PackedStringArray, header: String) -> String:
	var out := ""
	var inside := false
	for line in lines:
		if not inside:
			if line.begins_with(header):
				inside = true
			continue
		if line.begins_with("class ") or line.begins_with("#endregion") \
				or line.begins_with("#region"):
			break
		out += _strip_comment(line) + "\n"
	return out


func _assert_pure(region_name: String, body: String, forbidden: Array[String]) -> void:
	assert_str(body).override_failure_message(
		"purity lint could not locate region '%s'" % region_name,
	).is_not_empty()
	for token in forbidden:
		assert_bool(body.contains(token)) \
			.override_failure_message(
				"purity: kernel '%s' references forbidden '%s'" % [
					region_name, token,
				],
			).is_false()


func test_pure_kernels_touch_no_engine_state() -> void:
	var lines := _source_lines()
	for signature in _PURE_KERNELS:
		_assert_pure(signature, _func_body(lines, signature), _FORBIDDEN)
	for header in _PURE_CLASSES:
		_assert_pure(header, _class_body(lines, header), _FORBIDDEN)


# The CHASE kernel is the one sanctioned reader of the live body, a plain
# [code]source_obj.get(source_prop)[/code] that the forbidden set does not name,
# which is why its runtime is pinned to the SHELL thread class. It still may not
# touch the clock, tree, registry, roles, or signals, so it holds the same lint.
func test_chase_kernel_reads_only_the_live_body() -> void:
	var lines := _source_lines()
	_assert_pure("func _pump_chase(", _func_body(lines, "func _pump_chase("), _FORBIDDEN)


# Guards the guard: a passing lint must mean the kernel is clean, not that the
# extraction silently returned nothing. This proves a planted violation is caught
# and that each real kernel region extracts recognizable content.
func test_lint_is_not_vacuous() -> void:
	var planted := "\tvar c := _get_clock()\n"
	var caught := false
	for token in _FORBIDDEN:
		if planted.contains(token):
			caught = true
			break
	assert_bool(caught) \
		.override_failure_message("lint failed to catch a planted _get_clock()") \
		.is_true()

	var lines := _source_lines()
	assert_str(_func_body(lines, "func _pump_history(")).contains("display_lag")
	assert_str(_func_body(lines, "func _dilate_playhead(")).contains("starvation")
	assert_str(_class_body(lines, "class _History:")).contains("smooth_toward")
