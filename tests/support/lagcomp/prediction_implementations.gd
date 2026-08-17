## The prediction implementations a run states its kernel laws over.
##
## The laws are the specification, not one implementation. What certifies an
## implementation -- a game's own reconciliation policy, the native kernel, or
## the stock one itself -- is that the same suite is green against it
## and the shadow diff reads zero. That only means something if the suite can be
## pointed at more than one, which is what this file is for.
##
## [b]Certifying your own.[/b] Point [code]NETW_LAW_EXTENSION[/code] at a script
## extending [NetwMultiplayer] and run the suite:
## [codeblock]
## NETW_LAW_EXTENSION=res://my_game/my_policy.gd godot --path . -s \
##     res://addons/gdUnit4/bin/GdUnitCmdTool.gd --headless \
##     --ignoreHeadlessMode -c -a res://tests
## [/codeblock]
## Every law then runs against it, because the session hook points the
## project's multiplayer script setting at the named script before the first
## suite, so every peer a scenario stands up constructs it. No law is written
## twice and none is opted in: a law that exists is a law a candidate must
## pass.
##
## [b]Why an environment variable and not a parameter.[/b] Conformance is a
## property of a whole run. A suite that looped over implementations inside each
## law would certify the candidate only where an author remembered to loop, and
## the laws worth the most are the ones nobody thought to mark.
class_name PredictionImplementations
extends RefCounted

## Environment variable naming the implementation a run certifies.
##
## The name and the reading both belong to
## [method NetwMultiplayer.law_extension_script]. This is an alias so a suite
## can say what it is asking about without a second spelling of the variable.
const ENV_EXTENSION := NetwMultiplayer.LAW_EXTENSION_ENV

# Resolved once so a run cannot change implementations halfway through.
static var _resolved: NetwMultiplayer
static var _resolved_once := false
static var _stock: NetwMultiplayer


## The implementation this run certifies: the one
## [constant ENV_EXTENSION] names, or the stock one.
static func under_test() -> NetwMultiplayer:
	if not _resolved_once:
		_resolved_once = true
		_resolved = _from_environment()
	return _resolved if _resolved else stock()


## Returns the stock prediction decision host.
static func stock() -> NetwMultiplayer:
	if _stock == null:
		_stock = NetwMultiplayer.new()
	return _stock


## True when this run is certifying something other than the stock
## implementation, which is the one condition a law may legitimately relax for.
static func is_certifying() -> bool:
	return under_test() != stock()


## Every implementation a cross-check may compare, stock first.
##
## Stock first always, so a law reading the first entry reads the reference
## rather than whatever a run happened to add.
static func all() -> Array[NetwMultiplayer]:
	var made: Array[NetwMultiplayer] = [stock()]
	if is_certifying():
		made.append(under_test())
	return made


## Names [param implementation] in a failure message, so a law comparing several
## says which one broke rather than that one did.
static func blame(implementation: NetwMultiplayer) -> String:
	if implementation == null:
		return "<null implementation>"
	if implementation == stock():
		return "the stock implementation"
	var script := implementation.get_script() as Script
	var path := script.resource_path if script else ""
	return path if not path.is_empty() else "<anonymous implementation>"


# The addon resolves and validates the named script, so this only has to build
# the decision host from it. Reading the environment here as well would be a
# second mechanism that could disagree with the first.
static func _from_environment() -> NetwMultiplayer:
	var script := NetwMultiplayer.law_extension_script()
	if script == null:
		return null
	return NetwMultiplayer.make(null, script)
