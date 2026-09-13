## Spawns and supervises the child processes of a two-process regime capture.
##
## A regime arm runs two real OS processes speaking real sockets, so the test
## that drives it is an orchestrator: build each child's argument list, spawn
## it with a controlled environment, wait for both to exit, and read the
## [code]summary.json[/code] each [NetwRegimePeer] left behind. This class is
## that orchestrator's process half, game-agnostic and framework-agnostic.
## [codeblock]
## var rig := NetwProcessRig.new()
## rig.spawn(SCENE, { "regime-role": "host", "regime-port": "31411" })
## await suite.get_tree().create_timer(5.0).timeout
## rig.spawn(SCENE, { "regime-role": "client", "regime-port": "31411" })
## var all_exited := await rig.await_exit(suite, 60.0)
## var summary := NetwProcessRig.read_summary(host_dir)
## [/codeblock]
## Children inherit the parent's environment, so [method spawn] takes an
## explicit environment map and restores the parent's values afterward: an
## empty value unsets, anything else sets. Instruments that read only the
## environment ([code]NETW_PREDICT_TAP[/code]) are cleared by default so an
## armed parent can never silently arm its children.
class_name NetwProcessRig
extends RefCounted

# Environment keys cleared for every child unless the caller sets them, so a
# capture's instruments are always a decision rather than an inheritance.
const _CLEARED_ENV: Array[String] = [
	"NETW_PREDICT_TAP",
	"NETW_PREDICT_TAP_EVERY",
	"NETW_TEST_LOG",
]

var _pids: Array[int] = []


## Launches one headless child running [param scene] with [param regime_args]
## as [code]--key=value[/code] user arguments. Returns the pid, or -1 when
## the spawn failed. [param env] entries override the cleared-instrument
## defaults for this child only.
func spawn(
		scene: String,
		regime_args: Dictionary,
		env: Dictionary = { },
) -> int:
	var args := PackedStringArray([
		"--headless",
		"--path",
		ProjectSettings.globalize_path("res://"),
		scene,
		"--",
	])
	for key: Variant in regime_args:
		args.append("--%s=%s" % [key, regime_args[key]])
	var wanted := { }
	for key in _CLEARED_ENV:
		wanted[key] = ""
	wanted.merge(env, true)
	var restore := _apply_env(wanted)
	var pid := OS.create_process(OS.get_executable_path(), args)
	_apply_env(restore)
	if pid > 0:
		_pids.append(pid)
	return pid


## Waits until every spawned child has exited, polling four times a second
## against [param timeout_s]. Returns [code]false[/code] on timeout, with the
## stragglers killed so a wedged arm never leaks processes into the next one.
func await_exit(host: Node, timeout_s: float) -> bool:
	var deadline := Time.get_ticks_msec() + int(timeout_s * 1000.0)
	while Time.get_ticks_msec() < deadline:
		if not _any_running():
			return true
		await host.get_tree().create_timer(0.25).timeout
	stop_all()
	return false


## Kills every child still running. [method await_exit] does this on timeout,
## so callers only need it for early aborts.
func stop_all() -> void:
	for pid in _pids:
		if OS.is_process_running(pid):
			OS.kill(pid)


## Reads and parses the [code]summary.json[/code] a [NetwRegimePeer] wrote
## under [param dir], or an empty [Dictionary] when it is missing or invalid,
## which an arm treats as a failed child.
static func read_summary(dir: String) -> Dictionary:
	var file := FileAccess.open(dir.path_join("summary.json"), FileAccess.READ)
	if file == null:
		return { }
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed if parsed is Dictionary else { }


func _any_running() -> bool:
	for pid in _pids:
		if OS.is_process_running(pid):
			return true
	return false


# Applies an environment map and returns the map that undoes it. An empty
# value unsets the variable, mirroring how the return value restores one that
# did not exist.
func _apply_env(wanted: Dictionary) -> Dictionary:
	var restore := { }
	for key: String in wanted:
		restore[key] = OS.get_environment(key) if OS.has_environment(key) else ""
		var value := String(wanted[key])
		if value.is_empty():
			OS.unset_environment(key)
		else:
			OS.set_environment(key, value)
	return restore
