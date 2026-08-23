## Lint: the record stays wire-free, no native name is shadowed, every test that
## forces production state carries a SMELL tag, core interfaces never reach
## through the service registry for their collaborators, and the connect kit and
## the machine tier stay on their own sides of the boundary.
##
## These greps pin invariants prose cannot enforce. The record half of the entity
## pair never touches the wire, so codec, buffer, and frame types stay out of
## [NetwEntity]. A GDScript func may not shadow a non-virtual native name, the
## static-dispatch hazard behind [method MultiplayerAPI.is_server]. A test that
## reaches past the public flow to force production state tags itself, so a new
## monkey-patch cannot land silently and the tag sweep only ratchets. And the
## service registry stays a discovery surface for the kit and game code, never
## ambient context for the core, so a core interface that wants a collaborator
## takes it through typed config registration, never a
## [method NetwMultiplayer.get_service] lookup. The last pair is the tier
## boundary: the machine tier names no class the connect kit declares, and the
## kit reaches no private member of a core object, so the seam between them is
## public in both directions.
class_name TestNetwDisciplineLint
extends NetwTestSuite

const ADDON_ROOT := "res://addons/networked"
const TESTS_ROOT := "res://tests"
const RECORD_PATH := "res://addons/networked/context/session/netw_entity.gd"

# The core interfaces live here. The service registry itself lives on
# NetwMultiplayer, which defines the query and surfaces it as typed convenience
# accessors, so that one file is the sole legitimate query site.
const REPLICATION_ROOT := "res://addons/networked/replication"
const REGISTRY_HOME := "res://addons/networked/replication/netw_multiplayer.gd"

# The connect kit: transports, views, attempts, the browser, and the connector.
# Ring 2 forever, so nothing under REPLICATION_ROOT may name a class declared
# here. There is no allowlist, value types included: after the establishment
# move the machine tier needs none of them.
const CONNECT_ROOT := "res://addons/networked/connect"

# Private members of a core object. The kit reads the session through
# [NetwSessionHandle] and the api's public surface, so any of these on a line in
# connect/ is a reach past the door that exists.
const CORE_PRIVATE_REACHES: Array[String] = [
	"._session",
	"._replication",
	"._interest",
	"._liveness",
	"._roster",
	"._clock",
	"._embedding",
	"._persistence",
	"._display.",
	"._rpc_core",
]

# Service-registry query call sites a core interface must not contain. Registering
# a service is fine, only reaching back through the registry to find one is the
# ambient-context pattern this bans.
const REGISTRY_QUERY_PATTERNS: Array[String] = [
	"get_service(",
	"get_services(",
]

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


func test_core_interfaces_never_query_the_service_registry() -> void:
	var offenders: Array[String] = []
	for path in _gd_files(REPLICATION_ROOT):
		if path == REGISTRY_HOME:
			continue
		var lines := FileAccess.get_file_as_string(path).split("\n")
		for i in lines.size():
			var line: String = lines[i]
			# A comment may name a query method by concept, only code may not.
			if line.strip_edges().begins_with("#"):
				continue
			for pattern in REGISTRY_QUERY_PATTERNS:
				if line.contains(pattern):
					offenders.append(
						"%s:%d queries the registry with '%s'" % [path, i + 1, pattern],
					)
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


func test_core_interfaces_never_name_a_connect_kit_class() -> void:
	# The connect kit is script space forever, so a machine-tier file that names
	# one of its classes is a type the port would have to carry across. The kit
	# names are scanned rather than listed, so a new one is covered on the day it
	# is declared.
	var kit_names := _class_names(CONNECT_ROOT)
	assert_that(kit_names).is_not_empty()
	var offenders: Array[String] = []
	for path in _gd_files(REPLICATION_ROOT):
		var lines := FileAccess.get_file_as_string(path).split("\n")
		for i in lines.size():
			var line: String = lines[i]
			# A doc or comment may name a kit class by concept, only code may not.
			if line.strip_edges().begins_with("#"):
				continue
			for kit_name in kit_names:
				if _names_symbol(line, kit_name):
					offenders.append(
						"%s:%d names the connect kit class '%s'" % [
							path,
							i + 1,
							kit_name,
						],
					)
	assert_that(offenders).is_empty()


func test_the_connect_kit_never_reaches_a_core_private() -> void:
	# The kit is a client of the session, never a peer of its internals. Every
	# core collaborator it wants has a public door, so a private reach is the
	# kit asking for something the port would not be able to hand it.
	var offenders: Array[String] = []
	for path in _gd_files(CONNECT_ROOT):
		var lines := FileAccess.get_file_as_string(path).split("\n")
		for i in lines.size():
			var line: String = lines[i]
			if line.strip_edges().begins_with("#"):
				continue
			for reach in CORE_PRIVATE_REACHES:
				if line.contains(reach):
					offenders.append(
						"%s:%d reaches '%s' on a core object" % [path, i + 1, reach],
					)
	assert_that(offenders).is_empty()


# Every class_name declared under [param root], which is what makes the kit
# boundary self-maintaining rather than a list that drifts.
func _class_names(root: String) -> Array[String]:
	var out: Array[String] = []
	for path in _gd_files(root):
		for line in FileAccess.get_file_as_string(path).split("\n"):
			var text: String = line.strip_edges()
			if text.begins_with("class_name "):
				out.append(text.substr("class_name ".length()).strip_edges())
				break
	return out


# Whether [param line] names [param symbol] as a symbol rather than as the
# prefix of a longer one, so NetwConnectResult never matches NetwConnect.
func _names_symbol(line: String, symbol: String) -> bool:
	var from := 0
	while true:
		var at := line.find(symbol, from)
		if at < 0:
			return false
		var after := at + symbol.length()
		var tail := line.substr(after, 1)
		var head := line.substr(at - 1, 1) if at > 0 else ""
		var boundary_before := head.is_empty() or not _is_symbol_char(head)
		var boundary_after := tail.is_empty() or not _is_symbol_char(tail)
		if boundary_before and boundary_after:
			return true
		from = after
	return false


func _is_symbol_char(c: String) -> bool:
	return c == "_" or c.to_lower() != c.to_upper() or c.is_valid_int()


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
