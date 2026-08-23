## Source lint that keeps the display pump kernels pure.
##
## The display calculus can drive the real engine node-free only because the
## netw::display pump kernels read time by value and touch nothing outside
## the runtime and timing handed to them. That purity is a standing
## invariant: an edit that reaches for the clock core, walks the tree, reads
## the replication registry, or rescans roles per frame would silently
## re-break the calculus and the native fork-join. This test reads
## extension/src/display_pump.cpp and fails on that regression.
##
## The one sanctioned impurity is pump_chase reading the live predicted body
## through get_source_obj(). The lint allows that read in the chase kernel
## alone. pump_runtime and absorb_recovery reach through PumpHooks into
## surviving GDScript for role resolution and the chase clamp, so they stay
## outside the pure kernel set.
class_name TestNetwInterpEnginePurity
extends NetwTestSuite

const _ENGINE := "res://extension/src/display_pump.cpp"

# Tokens that must never appear in a pure kernel: clock resolution, tree or
# node access, engine globals, the replication registry, per-frame role and
# authority scans, hook reaches, and signal wiring. A kernel receives its
# timing by value and its runtime by argument, and it emits only through the
# channel writer and the stats record.
const _FORBIDDEN: Array[String] = [
	"ClockEngine",
	"clock_engine(",
	"->tick(",
	"after_tick",
	"get_tree(",
	"get_node",
	"get_parent(",
	"add_child(",
	"find_children",
	"NetwEntity::of(",
	"Engine::",
	"Time::",
	"OS::",
	"NetwScriptModel",
	"replication_config",
	"synchronizers(",
	"._replication",
	"is_multiplayer_authority",
	"PumpHooks",
	"hooks",
	"emit_signal(",
	"->connect(",
	".connect(",
	"->disconnect(",
	".disconnect(",
]

# The pure kernels, addressed by their column-zero signature. Each runs
# every frame and must stay free of every forbidden token above.
const _PURE_KERNELS: Array[String] = [
	"void pump_history(",
	"void dilate_playhead(",
	"double chase_smooth_time(",
	"double glide(",
	"bool take_trace_frame(",
]

func _source_lines() -> PackedStringArray:
	var f := FileAccess.open(_ENGINE, FileAccess.READ)
	assert_object(f).is_not_null()
	var text := f.get_as_text()
	f.close()
	return text.split("\n")


# The body of a column-zero function, from its signature past the (possibly
# wrapped) parameter list and the opening brace, to the closing brace that
# sits alone at column zero.
func _func_body(lines: PackedStringArray, signature: String) -> String:
	var out := ""
	var inside := false
	for line in lines:
		if not inside:
			if line.begins_with(signature):
				inside = true
			continue
		if line == "}":
			break
		out += line + "\n"
	return out


func _assert_pure(region_name: String, body: String, forbidden: Array[String]) -> void:
	assert_str(body).override_failure_message(
		"purity lint could not locate region '%s'" % region_name,
	).is_not_empty()
	for token in forbidden:
		assert_bool(body.contains(token)) \
				.override_failure_message(
					"purity: kernel '%s' references forbidden '%s'" % [
						region_name,
						token,
					],
				).is_false()


func test_pure_kernels_touch_no_engine_state() -> void:
	var lines := _source_lines()
	for signature in _PURE_KERNELS:
		_assert_pure(signature, _func_body(lines, signature), _FORBIDDEN)


# pump_chase is the one sanctioned reader of the live body, a plain
# [code]source_obj->get(source_prop)[/code] that the forbidden set does not
# name. It still may not touch the clock, tree, registry, roles, hooks, or
# signals, so it holds the same lint.
func test_chase_kernel_reads_only_the_live_body() -> void:
	var lines := _source_lines()
	_assert_pure("void pump_chase(", _func_body(lines, "void pump_chase("), _FORBIDDEN)


# Guards the guard: a passing lint must mean the kernel is clean, not that
# the extraction silently returned nothing. This proves a planted violation
# is caught and that each real kernel region extracts recognizable content.
func test_lint_is_not_vacuous() -> void:
	var planted := "    Engine::get_singleton();\n"
	var caught := false
	for token in _FORBIDDEN:
		if planted.contains(token):
			caught = true
			break
	assert_bool(caught) \
			.override_failure_message("lint failed to catch a planted Engine::get_singleton()") \
			.is_true()

	var lines := _source_lines()
	assert_str(_func_body(lines, "void pump_history(")).contains("display_lag")
	assert_str(_func_body(lines, "void dilate_playhead(")).contains("starving")
