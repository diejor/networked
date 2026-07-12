## Lint: the record stays wire-free, no native name is shadowed, and every test
## that forces production state carries a SMELL tag.
##
## These three greps pin invariants prose cannot enforce. The record half of the
## entity pair never touches the wire, so codec, buffer, and frame types stay out
## of [NetwEntity]. A GDScript func may not shadow a non-virtual native name, the
## static-dispatch hazard behind [method MultiplayerAPI.is_server]. And a test
## that reaches past the public flow to force production state tags itself, so a
## new monkey-patch cannot land silently and the tag sweep only ratchets.
class_name TestNetwDisciplineLint
extends NetwTestSuite

const ADDON_ROOT := "res://addons/networked"
const TESTS_ROOT := "res://tests"
const RECORD_PATH := "res://addons/networked/context/session/netw_entity.gd"

# Wire types the record must never name in code. The record reaches the wire only
# through an api call or a signal the machinery observes.
const WIRE_TYPES: Array[String] = [
	"NetwCodec",
	"NetwBitBuffer",
	"NetwFrameEnvelope",
]

# Non-virtual native methods (chiefly [MultiplayerAPI]) that the addon must reach
# through native dispatch, never redefine as a bare GDScript func.
const FORBIDDEN_NAMES: Array[String] = [
	"is_server",
	"get_unique_id",
	"has_multiplayer_peer",
]

# Forcing patterns a test may only use with a SMELL tag. Production applies node
# authority through [method NetwEntity.arm], so a bare set_multiplayer_authority
# in a test is always a pin standing in for a spawn the scenario never runs.
const FORCING_PATTERNS: Array[String] = [
	"set_multiplayer_authority(",
]


func test_entity_record_is_wire_free() -> void:
	var offenders: Array[String] = []
	var lines := FileAccess.get_file_as_string(RECORD_PATH).split("\n")
	for i in lines.size():
		var line: String = lines[i]
		# Comment and doc lines may link a wire type by concept, only code may not.
		if line.strip_edges().begins_with("#"):
			continue
		for wire_type in WIRE_TYPES:
			if line.contains(wire_type):
				offenders.append("netw_entity.gd:%d references %s" % [i + 1, wire_type])
	assert_that(offenders).is_empty()


func test_no_addon_script_shadows_a_native_name() -> void:
	var offenders: Array[String] = []
	for path in _gd_files(ADDON_ROOT):
		var text := FileAccess.get_file_as_string(path)
		for name in FORBIDDEN_NAMES:
			if text.contains("func %s(" % name):
				offenders.append("%s defines func %s()" % [path, name])
	assert_that(offenders).is_empty()


func test_forcing_test_lines_carry_a_smell_tag() -> void:
	var offenders: Array[String] = []
	var self_path := (get_script() as Script).resource_path
	for path in _gd_files(TESTS_ROOT):
		# This suite names the patterns as string literals, so it matches itself.
		if path == self_path:
			continue
		var lines := FileAccess.get_file_as_string(path).split("\n")
		for i in lines.size():
			for pattern in FORCING_PATTERNS:
				if lines[i].contains(pattern) and not _has_smell_tag(lines, i):
					offenders.append("%s:%d untagged '%s'" % [path, i + 1, pattern])
	assert_that(offenders).is_empty()


# Whether line [param idx] carries a SMELL tag, on the line itself or anywhere in
# the contiguous comment block directly above it.
func _has_smell_tag(lines: PackedStringArray, idx: int) -> bool:
	if lines[idx].contains("SMELL("):
		return true
	var j := idx - 1
	while j >= 0 and lines[j].strip_edges().begins_with("#"):
		if lines[j].contains("SMELL("):
			return true
		j -= 1
	return false


func _gd_files(root: String) -> Array[String]:
	var out: Array[String] = []
	var dir := DirAccess.open(root)
	if dir == null:
		return out
	dir.list_dir_begin()
	var entry := dir.get_next()
	while entry != "":
		var full := root.path_join(entry)
		if dir.current_is_dir():
			out.append_array(_gd_files(full))
		elif entry.ends_with(".gd"):
			out.append(full)
		entry = dir.get_next()
	dir.list_dir_end()
	return out
